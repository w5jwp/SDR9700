"""Shared Icom RS-BA1 transport used by the IC-9700 test tools."""

from __future__ import annotations

import os
import random
import select
import signal
import socket
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime


CONTROL_PORT = 50001
SERIAL_PORT = 50002
AUDIO_PORT = 50003
CONTROLLER_ADDRESS = 0xE1
WAKE_BOOT_SECONDS = 10.0
COMMAND_PROBE_SECONDS = 3.0
TOKEN_RENEW_SECONDS = 20.0
TOKEN_ACK_SECONDS = 2.5
# Three renewals occur near 20, 40, and 60 seconds. Five additional seconds
# leave a complete acknowledgement window after the third request.
DEFAULT_LIFECYCLE_SECONDS = 65.0

PASSCODE = bytes([
    0x47, 0x5D, 0x4C, 0x42, 0x66, 0x20, 0x23, 0x46, 0x4E, 0x57,
    0x45, 0x3D, 0x67, 0x76, 0x60, 0x41, 0x62, 0x39, 0x59, 0x2D,
    0x68, 0x7E, 0x7C, 0x65, 0x7D, 0x49, 0x29, 0x72, 0x73, 0x78,
    0x21, 0x6E, 0x5A, 0x5E, 0x4A, 0x3E, 0x71, 0x2C, 0x2A, 0x54,
    0x3C, 0x3A, 0x63, 0x4F, 0x43, 0x75, 0x27, 0x79, 0x5B, 0x35,
    0x70, 0x48, 0x6B, 0x56, 0x6F, 0x34, 0x32, 0x6C, 0x30, 0x61,
    0x6D, 0x7B, 0x2F, 0x4B, 0x64, 0x38, 0x2B, 0x2E, 0x50, 0x40,
    0x3F, 0x55, 0x33, 0x37, 0x25, 0x77, 0x24, 0x26, 0x74, 0x6A,
    0x28, 0x53, 0x4D, 0x69, 0x22, 0x5C, 0x44, 0x31, 0x36, 0x58,
    0x3B, 0x7A, 0x51, 0x5F, 0x52,
])


class PowerToolError(RuntimeError):
    """A bounded RS-BA1 or CI-V operation failed."""


def log_event(event: str, detail: str = "") -> None:
    """Print one timestamped, credential-free lifecycle observation."""
    timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
    suffix = f" {detail}" if detail else ""
    print(f"{timestamp} {event}{suffix}", flush=True)


def now() -> float:
    return time.monotonic()


def install_abrupt_interrupt_handler() -> None:
    """Make Ctrl-C emulate a client process disappearing without teardown.

    os._exit() is intentional here. Raising KeyboardInterrupt would unwind
    context managers and call PowerSession.close(), which sends CI-V pipe-close,
    media departure, token-removal, and control departure packets. The test
    tools instead need SIGINT to leave the radio-side session untouched while
    the operating system silently reclaims only this process's local sockets.
    """
    def abandon_session(_signal_number, _frame) -> None:
        log_event(
            "ABORT",
            "interrupt received; process disappearing without disconnect")
        # Do not raise SystemExit or KeyboardInterrupt here: either would run
        # session context-manager cleanup and tell the radio we disconnected.
        os._exit(130)

    signal.signal(signal.SIGINT, abandon_session)


def framed(length: int, kind: int, sequence: int, local_id: int,
           remote_id: int) -> bytearray:
    packet = bytearray(length)
    struct.pack_into("<IHH", packet, 0, length, kind, sequence & 0xFFFF)
    struct.pack_into(">II", packet, 8, local_id, remote_id)
    return packet


def header(packet: bytes) -> tuple[int, int, int, int, int]:
    if len(packet) < 16:
        return (0, 0, 0, 0, 0)
    length, kind, sequence = struct.unpack_from("<IHH", packet, 0)
    sender, recipient = struct.unpack_from(">II", packet, 8)
    return length, kind, sequence, sender, recipient


def set_inner(packet: bytearray, payload_length: int, request: int,
              kind: int, sequence: int) -> None:
    struct.pack_into(">I", packet, 0x10, payload_length)
    packet[0x14], packet[0x15] = request, kind
    struct.pack_into(">H", packet, 0x16, sequence & 0xFFFF)


def encode_passcode(text: str) -> bytes:
    encoded = bytearray(16)
    for index, character in enumerate(text.encode()[:16]):
        position = character + index
        if position > 126:
            position = 32 + position % 127
        if position >= 32:
            encoded[index] = PASSCODE[position - 32]
    return bytes(encoded)


def civ_frame(destination: int, command: int, payload: bytes = b"") -> bytes:
    return bytes((0xFE, 0xFE, destination, CONTROLLER_ADDRESS, command)) + payload + b"\xFD"


@dataclass
class Credentials:
    host: str
    username: str
    password: str


def read_credentials(program: str, purpose: str) -> Credentials:
    import argparse

    parser = argparse.ArgumentParser(prog=program, description=purpose)
    parser.add_argument(
        "--radio-ip", required=True,
        help="IC-9700 IPv4 address")
    parser.add_argument(
        "--radio-username", required=True,
        help="user configured under Network > Network User")
    parser.add_argument(
        "--radio-password", required=True,
        help="password for the configured radio network user")
    args = parser.parse_args()
    if not args.radio_username or not args.radio_password:
        parser.error("username and password must not be empty")
    return Credentials(args.radio_ip, args.radio_username, args.radio_password)


class Stream:
    def __init__(self, host: str, remote_port: int, role: str):
        self.host = host
        self.remote_port = remote_port
        self.role = role
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setblocking(False)
        self.socket.bind(("0.0.0.0", 0))
        self.socket.connect((host, remote_port))
        local_port = self.socket.getsockname()[1]
        self.local_id = (random.getrandbits(16) << 16) | local_port
        self.remote_id = 0
        self.outer_sequence = 1
        self.last_idle = now()

    @property
    def local_port(self) -> int:
        return self.socket.getsockname()[1]

    def send(self, packet: bytes, tracked: bool = False,
             copies: int = 1) -> None:
        outgoing = bytearray(packet)
        if tracked:
            struct.pack_into("<H", outgoing, 6, self.outer_sequence)
            self.outer_sequence = (self.outer_sequence + 1) & 0xFFFF
        for _ in range(copies):
            self.socket.send(outgoing)

    def receive(self, timeout: float = 0.0) -> list[bytes]:
        packets = []
        readable, _, _ = select.select([self.socket], [], [], timeout)
        if not readable:
            return packets
        while True:
            try:
                packet = self.socket.recv(65535)
            except BlockingIOError:
                break
            # Reply to the radio's idle probe immediately. These keepalives are
            # transport liveness only and are not CI-V command-plane proof.
            if (len(packet) == 21 and packet[4:6] == b"\x07\x00"
                    and packet[0x10] == 0):
                reply = bytearray(packet)
                reply[0x10] = 1
                struct.pack_into(">II", reply, 8,
                                 self.local_id, self.remote_id)
                self.socket.send(reply)
            else:
                packets.append(packet)
        return packets

    def handshake(self, timeout: float = 6.0) -> None:
        deadline = now() + timeout
        next_discovery = 0.0
        ready_sent = False
        while now() < deadline:
            if now() >= next_discovery:
                log_event("ACTION", f"{self.role} discovery -> UDP/{self.remote_port}")
                self.send(framed(16, 3, 0, self.local_id, 0), copies=2)
                next_discovery = now() + 1.0
            for packet in self.receive(0.15):
                _, kind, _, sender, _ = header(packet)
                if kind == 4 and len(packet) == 16:
                    log_event("RESPONSE", f"{self.role} discovery acknowledged")
                    self.remote_id = sender
                    log_event("ACTION", f"{self.role} ready confirmation")
                    self.send(framed(16, 6, 1, self.local_id,
                                     self.remote_id), copies=2)
                    ready_sent = True
                elif kind == 6 and ready_sent:
                    log_event("RESPONSE", f"{self.role} transport ready")
                    return
        raise PowerToolError(
            f"no {self.role} handshake response on UDP/{self.remote_port}")

    def idle(self) -> None:
        if self.remote_id and now() - self.last_idle >= 0.5:
            self.send(framed(16, 0, 0, self.local_id,
                             self.remote_id), tracked=True)
            self.last_idle = now()

    def depart(self) -> None:
        if self.remote_id:
            log_event("ACTION", f"{self.role} transport departure")
            self.send(framed(16, 5, 0, self.local_id,
                             self.remote_id), copies=2)

    def close(self) -> None:
        self.socket.close()


class PowerSession:
    """One short-lived, fully owned IC-9700 RS-BA1 session."""

    def __init__(self, credentials: Credentials):
        self.credentials = credentials
        self.serial = Stream(credentials.host, SERIAL_PORT, "CI-V")
        self.audio = Stream(credentials.host, AUDIO_PORT, "audio")
        self.control = Stream(credentials.host, CONTROL_PORT, "control")
        self.auth_id = b"\0" * 6
        self.radio_id = b"\0" * 16
        self.radio_name = ""
        self.inner_sequence = 0
        self.serial_sequence = 0
        self.stream_owned = False
        self.civ_address = None
        self.pending_renewal = None
        self.renewal_deadline = 0.0
        self.civ_buffer = bytearray()
        self.closed = False
        # open_awake_session() records why this particular session became the
        # usable one. Callers surface these fields in their final test summary
        # so normal startup, retained-session recovery, and standby wake are
        # never indistinguishable in captured logs.
        self.startup_outcome = "unclassified"
        self.recovered_hung_session = False
        self.radio_woken = False
        self.wake_attempts = 0

    def _tracked_control(self, packet: bytes) -> None:
        self.control.send(packet, tracked=True)

    def _auth_packet(self, kind: int) -> bytes:
        packet = framed(64, 0, 0, self.control.local_id,
                        self.control.remote_id)
        set_inner(packet, 48, 1, kind, self.inner_sequence)
        self.inner_sequence += 1
        packet[0x1A:0x20] = self.auth_id
        return bytes(packet)

    def _login_packet(self) -> bytes:
        packet = framed(128, 0, 0, self.control.local_id,
                        self.control.remote_id)
        set_inner(packet, 112, 1, 0, self.inner_sequence)
        self.inner_sequence += 1
        struct.pack_into("<H", packet, 0x1A, random.randint(1, 0xFFFF))
        packet[0x40:0x50] = encode_passcode(self.credentials.username)
        packet[0x50:0x60] = encode_passcode(self.credentials.password)
        packet[0x60:0x67] = b"icom-pc"
        return bytes(packet)

    def _stream_request(self) -> bytes:
        packet = framed(144, 0, 0, self.control.local_id,
                        self.control.remote_id)
        set_inner(packet, 128, 1, 3, self.inner_sequence)
        self.inner_sequence += 1
        packet[0x1A:0x20], packet[0x20:0x30] = self.auth_id, self.radio_id
        name = self.radio_name.encode()[:32]
        packet[0x40:0x40 + len(name)] = name
        packet[0x60:0x70] = encode_passcode(self.credentials.username)
        # Exercise a complete IC-9700 RS-BA1 session: enable both receive and
        # transmit audio, use LPCM mono 16-bit (0x04), and advertise 48 kHz RX,
        # 16 kHz TX, and an 80 ms TX buffer. Negotiating the TX direction does
        # not key PTT or send audio.
        packet[0x70:0x74] = bytes((1, 1, 4, 4))
        struct.pack_into(">IIIII", packet, 0x74, 48000, 16000,
                         self.serial.local_port, self.audio.local_port, 80)
        packet[0x88] = 1
        return bytes(packet)

    def open(self, timeout: float = 12.0) -> None:
        log_event("ACTION", f"open session to {self.credentials.host}")
        self.control.handshake()
        log_event("ACTION", "send radio login")
        self._tracked_control(self._login_packet())
        authenticated = False
        request_sent = False
        deadline = now() + timeout
        while now() < deadline:
            for packet in self.control.receive(0.1):
                _, kind, sequence, _, _ = header(packet)
                inner_kind = packet[0x15] if len(packet) > 0x15 else -1
                log_event(
                    "RESPONSE",
                    f"control packet bytes={len(packet)} type={kind} sequence={sequence} inner={inner_kind}")
                if len(packet) == 96:
                    if packet[0x30:0x34] == b"\xFF\xFF\xFF\xFE":
                        raise PowerToolError("radio rejected username/password")
                    self.auth_id = bytes(packet[0x1A:0x20])
                    log_event("RESPONSE", "login challenge accepted")
                    log_event("ACTION", "request radio list and authentication token")
                    self._tracked_control(self._auth_packet(2))
                    self._tracked_control(self._auth_packet(5))
                elif len(packet) == 168:
                    self.radio_id = bytes(packet[0x42:0x52])
                    self.radio_name = bytes(packet[0x52:0x72]).split(
                        b"\0", 1)[0].decode(errors="replace")
                    log_event("STATE", f"radio found model={self.radio_name or 'unknown'}")
                elif (len(packet) == 64 and packet[0x14] == 2
                      and packet[0x15] == 5):
                    if struct.unpack_from("<I", packet, 0x30)[0] != 0:
                        raise PowerToolError("radio rejected authentication token")
                    self.auth_id = bytes(packet[0x1A:0x20])
                    authenticated = True
                    log_event("RESPONSE", "authentication token granted")
                elif (len(packet) == 80
                      and packet[0x30:0x33] == b"\xFF\xFF\xFF"):
                    raise PowerToolError("radio returned authentication failure")
                elif len(packet) == 144 and packet[0x60] == 1:
                    # A delayed stream grant cannot establish ownership until
                    # this login authenticated and issued its own request.
                    if authenticated and request_sent:
                        self.auth_id = bytes(packet[0x1A:0x20])
                        self.stream_owned = True
                        log_event("RESPONSE", "CI-V/audio stream request granted")
            if (authenticated and self.radio_id != b"\0" * 16
                    and not request_sent):
                log_event("ACTION", "request CI-V/audio streams")
                self._tracked_control(self._stream_request())
                request_sent = True
            if self.stream_owned:
                break
            self.control.idle()
        if not self.stream_owned:
            raise PowerToolError("timed out waiting for stream grant")

        self.serial.handshake()
        self.audio.handshake()
        log_event("ACTION", "open CI-V data pipe twice")
        self.serial.send(self._serial_open(True), copies=2)

    def _serial_open(self, opened: bool) -> bytes:
        packet = framed(22, 0, 0, self.serial.local_id,
                        self.serial.remote_id)
        packet[0x10:0x13] = b"\xC0\x01\x00"
        struct.pack_into(">H", packet, 0x13, self.serial_sequence)
        self.serial_sequence = (self.serial_sequence + 1) & 0xFFFF
        packet[0x15] = 5 if opened else 0
        return bytes(packet)

    def send_civ(self, frame: bytes) -> None:
        log_event("ACTION", f"CI-V TX {frame.hex(' ')}")
        packet = framed(21 + len(frame), 0, self.serial.outer_sequence,
                        self.serial.local_id, self.serial.remote_id)
        self.serial.outer_sequence = (self.serial.outer_sequence + 1) & 0xFFFF
        packet[0x10] = 0xC1
        struct.pack_into("<H", packet, 0x11, len(frame))
        struct.pack_into(">H", packet, 0x13, self.serial_sequence)
        self.serial_sequence = (self.serial_sequence + 1) & 0xFFFF
        packet[0x15:] = frame
        self.serial.send(bytes(packet))

    def _feed_civ(self, payload: bytes) -> list[bytes]:
        frames = []
        for byte in payload:
            if not self.civ_buffer:
                if byte == 0xFE:
                    self.civ_buffer.append(byte)
                continue
            self.civ_buffer.append(byte)
            if len(self.civ_buffer) == 2 and self.civ_buffer != b"\xFE\xFE":
                self.civ_buffer[:] = b"\xFE" if byte == 0xFE else b""
            elif byte in (0xFC, 0xFD):
                if len(self.civ_buffer) >= 6:
                    frames.append(bytes(self.civ_buffer))
                self.civ_buffer.clear()
            elif len(self.civ_buffer) > 1200:
                self.civ_buffer.clear()
        return frames

    def _receive_civ(self) -> list[bytes]:
        frames = []
        for packet in self.serial.receive(0):
            if len(packet) < 22 or packet[0x10] != 0xC1:
                continue
            declared = struct.unpack_from("<H", packet, 0x11)[0]
            if declared == len(packet) - 21:
                received = self._feed_civ(packet[21:])
                for frame in received:
                    log_event("RESPONSE", f"CI-V RX {frame.hex(' ')}")
                frames.extend(received)
        return frames

    def pump(self, duration: float) -> list[bytes]:
        frames = []
        deadline = now() + duration
        while now() < deadline:
            for packet in self.control.receive(0):
                _, kind, sequence, _, _ = header(packet)
                inner_kind = packet[0x15] if len(packet) > 0x15 else -1
                log_event(
                    "RESPONSE",
                    f"control packet bytes={len(packet)} type={kind} sequence={sequence} inner={inner_kind}")
                if (len(packet) == 64 and packet[0x14] == 2
                        and packet[0x15] == 5):
                    inner_sequence = struct.unpack_from(">H", packet, 0x16)[0]
                    if inner_sequence == self.pending_renewal:
                        response = struct.unpack_from("<I", packet, 0x30)[0]
                        if response != 0:
                            raise PowerToolError(
                                f"radio rejected token renewal response=0x{response:08x}")
                        self.auth_id = bytes(packet[0x1A:0x20])
                        self.pending_renewal = None
                        log_event("VALIDATION", "authentication token renewal acknowledged")
            self.audio.receive(0)
            frames.extend(self._receive_civ())
            for stream in (self.control, self.serial, self.audio):
                stream.idle()
            time.sleep(0.02)
        return frames

    def hold(self, duration: float) -> None:
        """Keep an owned session healthy for a requested test interval."""
        started = now()
        deadline = started + duration
        next_renewal = started + TOKEN_RENEW_SECONDS
        log_event("ACTION", f"hold connected session for {duration:g} seconds")
        while now() < deadline:
            current = now()
            if self.pending_renewal is None and current >= next_renewal:
                if deadline - current <= TOKEN_ACK_SECONDS:
                    # Do not begin a renewal that the requested run interval
                    # cannot keep alive long enough to validate.
                    next_renewal = float("inf")
                else:
                    self.pending_renewal = self.inner_sequence
                    self.renewal_deadline = current + TOKEN_ACK_SECONDS
                    log_event(
                        "ACTION",
                        f"renew authentication token sequence={self.pending_renewal}")
                    self._tracked_control(self._auth_packet(5))
                    next_renewal += TOKEN_RENEW_SECONDS
            self.pump(min(0.25, deadline - now()))
            if (self.pending_renewal is not None
                    and now() > self.renewal_deadline):
                raise PowerToolError(
                    f"token renewal sequence={self.pending_renewal} was not acknowledged")
        log_event("VALIDATION", f"requested run time completed elapsed={now() - started:.3f}s")

    def directed_identity(self,
                          timeout: float = COMMAND_PROBE_SECONDS) -> bool:
        # Only a reply directed back to E1 proves that commands from this
        # replacement session reach the radio. Audio and unsolicited scope
        # frames are deliberately insufficient.
        deadline = now() + timeout
        next_probe = 0.0
        next_open = 0.0
        while now() < deadline:
            current = now()
            # Keep sending the untracked pipe-open pair during the readiness
            # window. A single lost or ignored open must not make an awake
            # radio look like standby.
            if current >= next_open:
                log_event("ACTION", "retry CI-V data-pipe open twice")
                self.serial.send(self._serial_open(True), copies=2)
                next_open = current + 0.1
            if current >= next_probe:
                self.send_civ(civ_frame(0x00, 0x19, b"\x00"))
                next_probe = current + 0.5
            for frame in self.pump(min(0.10, deadline - now())):
                if (len(frame) >= 8 and frame[2] == CONTROLLER_ADDRESS
                        and frame[3] != CONTROLLER_ADDRESS
                        and frame[4:6] == b"\x19\x00"):
                    self.civ_address = frame[3]
                    log_event("VALIDATION", "directed CI-V identity reply received")
                    return True
        log_event("VALIDATION", "directed CI-V identity reply timed out")
        return False

    def send_standby(self) -> None:
        if self.civ_address is None:
            raise PowerToolError("radio CI-V address was not discovered")
        log_event("ACTION", "request radio standby")
        self.send_civ(civ_frame(self.civ_address, 0x18, b"\x00"))

    def send_wake(self) -> None:
        # Icom requires a long FE synchronization fill for power-on. The 150
        # extra FE bytes safely exceed the guide's approximate minimum while
        # the already-open media pipe remains alive.
        log_event("ACTION", "request radio wake with padded CI-V power-on frame")
        self.send_civ((b"\xFE" * 150) + civ_frame(0xA2, 0x18, b"\x01"))

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        try:
            # Authentication alone is not ownership. Radio-side teardown is
            # legal only after this process received its own stream grant.
            if self.stream_owned:
                if self.serial.remote_id:
                    log_event("ACTION", "close CI-V data pipe twice")
                    self.serial.send(self._serial_open(False), copies=2)
                self.pump(0.05)
                self.serial.depart()
                self.audio.depart()
                log_event("ACTION", "remove authentication token")
                self._tracked_control(self._auth_packet(1))
                self.pump(0.20)
                self.control.depart()
                log_event("ACTION", "owned session cleanup completed")
        finally:
            for stream in (self.serial, self.audio, self.control):
                try:
                    stream.close()
                except OSError:
                    pass

    def __enter__(self) -> "PowerSession":
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        self.close()


def open_session(credentials: Credentials) -> PowerSession:
    session = PowerSession(credentials)
    try:
        session.open()
        return session
    except Exception:
        session.close()
        raise


def enter_standby(credentials: Credentials) -> None:
    with open_session(credentials) as session:
        if not session.directed_identity():
            raise PowerToolError("radio command plane is not ready")
        model = session.radio_name or "IC-9700"
        session.send_standby()
        log_event("ACTION", "wait 8 seconds for standby transition")
        session.pump(8.0)
        if session.directed_identity():
            raise PowerToolError(
                "radio still answers CI-V; remote-control standby may be disabled")
        log_event("PASS", f"{model} entered standby and stopped answering directed CI-V")


def open_awake_session(credentials: Credentials) -> PowerSession:
    """Return an owned session whose directed CI-V command path is proven."""
    # One silent fresh-session replacement rules out the common retained-pipe
    # ambiguity before any state-changing wake command is sent.
    for session_number in range(4):
        session = open_session(credentials)
        keep_session = False
        try:
            if session.directed_identity():
                if session_number == 0:
                    session.startup_outcome = "normal"
                elif session_number == 1:
                    # The original authenticated stream was silent, while an
                    # otherwise identical fresh session immediately became
                    # command-ready without a power-on command. That observed
                    # transition is what distinguishes a hung/retained session
                    # from a radio that was actually in standby.
                    session.startup_outcome = "hung-session-recovered"
                    session.recovered_hung_session = True
                    log_event(
                        "RECOVERY",
                        "hung or retained session recovered by fresh-session replacement")
                else:
                    session.startup_outcome = "radio-woken"
                    session.radio_woken = True
                    session.wake_attempts = session_number - 1
                    log_event(
                        "WAKE",
                        f"radio wake completed attempts={session.wake_attempts}")
                keep_session = True
                return session
            if session_number == 0:
                log_event(
                    "RECOVERY",
                    "command plane silent; test for hung or retained session with fresh-session replacement")
                continue
            if session_number <= 2:
                log_event(
                    "WAKE",
                    f"fresh replacement also silent; standby wake attempt {session_number}/2")
                session.send_wake()
                # Do not tear down immediately: the wake frame travels through
                # this CI-V pipe and the radio needs a bounded boot interval.
                log_event("ACTION", f"keep wake transport alive for {WAKE_BOOT_SECONDS:g} seconds")
                session.pump(WAKE_BOOT_SECONDS)
                continue
            raise PowerToolError(
                "radio did not answer CI-V after two standby wake attempts")
        finally:
            if not keep_session:
                session.close()
    raise PowerToolError("radio wake sequence ended without a usable session")


def wake_from_standby(credentials: Credentials) -> None:
    with open_awake_session(credentials) as session:
        log_startup_summary(session)


def log_startup_summary(session: PowerSession) -> None:
    """Emit one combined human-readable and machine-readable startup state."""
    model = session.radio_name or "IC-9700"
    descriptions = {
        "normal": f"{model} was already awake",
        "hung-session-recovered": f"{model} answered directed CI-V after hung-session recovery",
        "radio-woken": f"{model} woke and answered directed CI-V",
    }
    description = descriptions.get(
        session.startup_outcome,
        f"{model} startup outcome was not classified")
    log_event("STATE", description)
    log_event(
        "STATE",
        f"startup={session.startup_outcome} "
        f"hung-session-recovered={'yes' if session.recovered_hung_session else 'no'} "
        f"radio-woken={'yes' if session.radio_woken else 'no'} "
        f"wake-attempts={session.wake_attempts}")


if __name__ == "__main__":
    print(
        "error: this module provides shared IC-9700 RS-BA1 functionality used by other scripts; "
        "it is not a standalone tool",
        file=sys.stderr,
    )
    raise SystemExit(2)
