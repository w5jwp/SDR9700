"""Shared Icom RS-BA1 transport used by the IC-9700 test tools."""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import random
import select
import signal
import socket
import struct
import sys
import tempfile
import time
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


CONTROL_PORT = 50001
SERIAL_PORT = 50002
AUDIO_PORT = 50003
CONTROLLER_ADDRESS = 0xE1
WAKE_BOOT_SECONDS = 10.0
COMMAND_PROBE_SECONDS = 3.0
TOKEN_RENEW_SECONDS = 20.0
TOKEN_ACK_SECONDS = 2.5
TOKEN_REMOVAL_ATTEMPTS = 8
TOKEN_REMOVAL_INTERVAL = 0.5
# Three renewals occur near 20, 40, and 60 seconds. Five additional seconds
# leave a complete acknowledgement window after the third request.
DEFAULT_LIFECYCLE_SECONDS = 65.0
_SESSION_LOCKS: dict[Path, int] = {}

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


class RetainedTokenStreamRejected(PowerToolError):
    """A reissued predecessor token authenticated but could not own streams."""


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


@contextmanager
def defer_interrupt_for_journal():
    """Defer Ctrl-C until an ownership-changing journal update is durable."""
    previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})
    try:
        yield
    finally:
        # A pending SIGINT is delivered here, after the journal describes the
        # token that the radio currently considers authoritative.
        signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)


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


@dataclass(frozen=True)
class TransportIdentity:
    local_port: int
    remote_port: int
    local_id: int
    remote_id: int


@dataclass(frozen=True)
class RecoveryRecord:
    host: str
    owner_pid: int
    auth_id: bytes
    control: TransportIdentity | None
    serial: TransportIdentity | None
    audio: TransportIdentity | None


def recovery_file(credentials: Credentials) -> Path:
    """Return a private journal path unique to this radio account."""
    identity = hashlib.sha256(
        f"{credentials.host}\0{credentials.username}".encode()).hexdigest()[:24]
    return Path(tempfile.gettempdir()) / "icom-rsba1-lifecycle" / f"{identity}.json"


def acquire_session_lock(credentials: Credentials) -> None:
    """Prevent one live test process from reclaiming another live process."""
    journal = recovery_file(credentials)
    lock_path = journal.with_suffix(".lock")
    if lock_path in _SESSION_LOCKS:
        return
    lock_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(lock_path.parent, 0o700)
    descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        os.close(descriptor)
        raise PowerToolError(
            "another lifecycle process is already using this radio account") from error
    os.fchmod(descriptor, 0o600)
    _SESSION_LOCKS[lock_path] = descriptor


def _identity_to_json(identity: TransportIdentity) -> dict[str, int]:
    return {
        "localPort": identity.local_port,
        "remotePort": identity.remote_port,
        "localID": identity.local_id,
        "remoteID": identity.remote_id,
    }


def _identity_from_json(value: object) -> TransportIdentity:
    if not isinstance(value, dict):
        raise ValueError("transport identity is not an object")
    identity = TransportIdentity(
        int(value["localPort"]), int(value["remotePort"]),
        int(value["localID"]), int(value["remoteID"]))
    if (not 1 <= identity.local_port <= 65535
            or not 1 <= identity.remote_port <= 65535
            or not 1 <= identity.local_id <= 0xFFFFFFFF
            or not 1 <= identity.remote_id <= 0xFFFFFFFF):
        raise ValueError("transport identity is outside the wire range")
    return identity


def save_recovery_record(credentials: Credentials,
                         auth_id: bytes,
                         control: TransportIdentity | None = None,
                         serial: TransportIdentity | None = None,
                         audio: TransportIdentity | None = None) -> None:
    """Atomically preserve everything a replacement process must retire."""
    if len(auth_id) != 6 or auth_id == b"\0" * 6:
        raise PowerToolError("cannot journal an incomplete authentication token")
    path = recovery_file(credentials)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    payload = {
        "version": 1,
        "host": credentials.host,
        "ownerPID": os.getpid(),
        "authID": auth_id.hex(),
    }
    identities = (control, serial, audio)
    if any(identity is not None for identity in identities):
        if not all(identity is not None for identity in identities):
            raise PowerToolError("cannot journal a partial transport identity set")
        payload.update({
            "control": _identity_to_json(control),
            "serial": _identity_to_json(serial),
            "audio": _identity_to_json(audio),
        })
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    os.fchmod(descriptor, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as file:
            json.dump(payload, file, separators=(",", ":"))
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, path)
        os.chmod(path, 0o600)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    log_event("STATE", f"crash-recovery journal saved path={path}")


def load_recovery_record(credentials: Credentials) -> RecoveryRecord | None:
    """Load only a complete, private journal for the requested radio."""
    path = recovery_file(credentials)
    try:
        stat = path.stat()
        if stat.st_mode & 0o077:
            raise PowerToolError(f"recovery journal permissions are not private path={path}")
        value = json.loads(path.read_text(encoding="utf-8"))
        auth_id = bytes.fromhex(value["authID"])
        identity_keys = ("control", "serial", "audio")
        has_any_identity = any(key in value for key in identity_keys)
        if has_any_identity and not all(key in value for key in identity_keys):
            raise ValueError("journal contains a partial transport identity set")
        record = RecoveryRecord(
            host=str(value["host"]), owner_pid=int(value["ownerPID"]),
            auth_id=auth_id,
            control=_identity_from_json(value["control"]) if has_any_identity else None,
            serial=_identity_from_json(value["serial"]) if has_any_identity else None,
            audio=_identity_from_json(value["audio"]) if has_any_identity else None)
        if value.get("version") != 1 or record.host != credentials.host or len(auth_id) != 6:
            raise ValueError("journal identity does not match this invocation")
        return record
    except FileNotFoundError:
        return None
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise PowerToolError(f"invalid recovery journal path={path}: {error}") from error


def remove_recovery_record(credentials: Credentials) -> None:
    try:
        recovery_file(credentials).unlink()
    except FileNotFoundError:
        pass


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

    @property
    def identity(self) -> TransportIdentity:
        return TransportIdentity(
            self.local_port, self.remote_port, self.local_id, self.remote_id)

    def set_remote_port(self, remote_port: int) -> None:
        if not 1 <= remote_port <= 65535:
            raise PowerToolError(
                f"radio returned invalid {self.role} port={remote_port}")
        if remote_port != self.remote_port:
            self.remote_port = remote_port
            self.socket.connect((self.host, remote_port))
            log_event("STATE", f"{self.role} remote port={remote_port}")

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
                    and packet[0x10] == 0
                    and header(packet)[3:] == (self.remote_id, self.local_id)):
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
                _, kind, _, sender, recipient = header(packet)
                if kind == 4 and len(packet) == 16 and recipient == self.local_id:
                    log_event("RESPONSE", f"{self.role} discovery acknowledged")
                    self.remote_id = sender
                    log_event("ACTION", f"{self.role} ready confirmation")
                    self.send(framed(16, 6, 1, self.local_id,
                                     self.remote_id), copies=2)
                    ready_sent = True
                elif (kind == 6 and ready_sent and sender == self.remote_id
                      and recipient == self.local_id):
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


def _retire_transport(host: str, identity: TransportIdentity,
                      role: str) -> None:
    """Send departure as the vanished transport from its exact local port."""
    transport = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        transport.bind(("0.0.0.0", identity.local_port))
        transport.connect((host, identity.remote_port))
        departure = framed(
            16, 5, 0, identity.local_id, identity.remote_id)
        transport.send(departure)
        transport.send(departure)
        log_event(
            "RECOVERY",
            f"retired predecessor {role} transport local-port={identity.local_port}")
    except OSError as error:
        raise PowerToolError(
            f"could not retire predecessor {role} transport: {error}") from error
    finally:
        transport.close()


def _remove_predecessor_token(credentials: Credentials,
                              auth_id: bytes) -> None:
    """Remove a saved token through a fresh, correlated control association."""
    control = Stream(credentials.host, CONTROL_PORT, "recovery control")
    try:
        control.handshake()
        inner_sequence = random.randint(0, 0xFFFF)
        packet = framed(
            64, 0, 0, control.local_id, control.remote_id)
        set_inner(packet, 48, 1, 1, inner_sequence)
        packet[0x1A:0x20] = auth_id
        for attempt in range(1, TOKEN_REMOVAL_ATTEMPTS + 1):
            log_event(
                "RECOVERY",
                f"remove predecessor token attempt={attempt}/{TOKEN_REMOVAL_ATTEMPTS}")
            control.send(packet, tracked=True)
            deadline = now() + TOKEN_REMOVAL_INTERVAL
            while now() < deadline:
                for response in control.receive(min(0.05, max(0, deadline - now()))):
                    _, _, _, sender, recipient = header(response)
                    if sender != control.remote_id or recipient != control.local_id:
                        continue
                    if (len(response) == 64 and response[0x14:0x16] == b"\x02\x01"
                            and struct.unpack_from(">H", response, 0x16)[0] == inner_sequence):
                        result = struct.unpack_from("<I", response, 0x30)[0]
                        log_event(
                            "RECOVERY",
                            f"predecessor token removal acknowledged response=0x{result:08x}")
                        return
                    if (len(response) == 80 and response[0x40] == 1
                            and struct.unpack_from("<I", response, 0x30)[0] == 0
                            and response[0x1A:0x20] == auth_id):
                        log_event(
                            "RECOVERY",
                            "predecessor disconnect status acknowledged")
                        return
                control.idle()
        raise PowerToolError("radio did not acknowledge predecessor token removal")
    finally:
        control.depart()
        control.close()


def recover_journaled_session(credentials: Credentials) -> bool:
    """Retire a prior crashed invocation before creating replacement streams."""
    record = load_recovery_record(credentials)
    if record is None:
        return False
    log_event(
        "RECOVERY",
        f"dead session found owner-pid={record.owner_pid}")
    # The radio identifies a transport by both endpoint and opaque ID pair.
    # Replaying departure from merely a new socket or merely the old IDs is not
    # equivalent; all four recorded values must be reproduced together.
    if record.control and record.serial and record.audio:
        _retire_transport(credentials.host, record.control, "control")
        _retire_transport(credentials.host, record.serial, "CI-V")
        _retire_transport(credentials.host, record.audio, "audio")
    else:
        log_event(
            "RECOVERY",
            "journal contains token only; no predecessor transports to retire")
    _remove_predecessor_token(credentials, record.auth_id)
    remove_recovery_record(credentials)
    log_event("RECOVERY", "dead session recovery completed")
    return True


class PowerSession:
    """One short-lived, fully owned IC-9700 RS-BA1 session."""

    def __init__(self, credentials: Credentials,
                 recovered_predecessor: bool = False):
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
        self.authenticated = False
        self.initial_token_reissued = False
        self.login_sequence = None
        self.login_token_request = None
        self.initial_auth_sequence = None
        self.stream_request_sequence = None
        self.stream_request_auth = b""
        self.recovered_predecessor = recovered_predecessor
        self.journal_saved = False
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
        request_sequence = self.inner_sequence
        set_inner(packet, 48, 1, kind, request_sequence)
        self.inner_sequence += 1
        packet[0x1A:0x20] = self.auth_id
        if kind == 5:
            self.pending_renewal = request_sequence
        return bytes(packet)

    def _login_packet(self) -> bytes:
        packet = framed(128, 0, 0, self.control.local_id,
                        self.control.remote_id)
        self.login_sequence = self.inner_sequence
        set_inner(packet, 112, 1, 0, self.login_sequence)
        self.inner_sequence += 1
        self.login_token_request = random.randint(1, 0xFFFF)
        struct.pack_into("<H", packet, 0x1A, self.login_token_request)
        packet[0x40:0x50] = encode_passcode(self.credentials.username)
        packet[0x50:0x60] = encode_passcode(self.credentials.password)
        packet[0x60:0x67] = b"icom-pc"
        return bytes(packet)

    def _stream_request(self) -> bytes:
        packet = framed(144, 0, 0, self.control.local_id,
                        self.control.remote_id)
        self.stream_request_sequence = self.inner_sequence
        self.stream_request_auth = self.auth_id
        set_inner(packet, 128, 1, 3, self.stream_request_sequence)
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
        request_sent = False
        deadline = now() + timeout
        while now() < deadline:
            for packet in self.control.receive(0.1):
                _, kind, sequence, sender, recipient = header(packet)
                inner_kind = packet[0x15] if len(packet) > 0x15 else -1
                log_event(
                    "RESPONSE",
                    f"control packet bytes={len(packet)} type={kind} sequence={sequence} inner={inner_kind}")
                if len(packet) == 96:
                    if (sender != self.control.remote_id
                            or recipient != self.control.local_id
                            or struct.unpack_from(">H", packet, 0x16)[0] != self.login_sequence
                            or struct.unpack_from("<H", packet, 0x1A)[0] != self.login_token_request):
                        log_event("RESPONSE", "ignored stale or unrelated login response")
                        continue
                    if packet[0x30:0x34] == b"\xFF\xFF\xFF\xFE":
                        raise PowerToolError("radio rejected username/password")
                    self.auth_id = bytes(packet[0x1A:0x20])
                    log_event("RESPONSE", "login challenge accepted")
                    log_event("ACTION", "request radio list and authentication token")
                    self._tracked_control(self._auth_packet(2))
                    self.initial_auth_sequence = self.inner_sequence
                    self._tracked_control(self._auth_packet(5))
                elif len(packet) == 168:
                    if (sender != self.control.remote_id
                            or recipient != self.control.local_id):
                        log_event("RESPONSE", "ignored stale or unrelated radio list")
                        continue
                    self.radio_id = bytes(packet[0x42:0x52])
                    self.radio_name = bytes(packet[0x52:0x72]).split(
                        b"\0", 1)[0].decode(errors="replace")
                    log_event("STATE", f"radio found model={self.radio_name or 'unknown'}")
                elif (len(packet) == 64 and packet[0x14] == 2
                      and packet[0x15] == 5):
                    response_sequence = struct.unpack_from(">H", packet, 0x16)[0]
                    if (sender != self.control.remote_id
                            or recipient != self.control.local_id
                            or response_sequence != self.initial_auth_sequence):
                        log_event("RESPONSE", "ignored stale or unrelated authentication response")
                        continue
                    response = struct.unpack_from("<I", packet, 0x30)[0]
                    self.auth_id = bytes(packet[0x1A:0x20])
                    self.authenticated = True
                    self.pending_renewal = None
                    self.initial_token_reissued = response != 0
                    if self.initial_token_reissued:
                        log_event(
                            "RECOVERY",
                            f"radio reissued retained authentication response=0x{response:08x}")
                    else:
                        log_event("RESPONSE", "authentication token granted")
                elif len(packet) == 80:
                    response_sequence = struct.unpack_from(">H", packet, 0x16)[0]
                    same_stream = (
                        sender == self.control.remote_id
                        and recipient == self.control.local_id
                        and response_sequence == self.stream_request_sequence
                        and packet[0x1A:0x20] == self.stream_request_auth)
                    if not same_stream:
                        log_event("RESPONSE", "ignored stale or unrelated stream failure")
                        continue
                    error = struct.unpack_from(">I", packet, 0x30)[0]
                    disconnected = packet[0x40] == 1
                    if error != 0 or disconnected:
                        if self.initial_token_reissued and error != 0:
                            raise RetainedTokenStreamRejected(
                                "reissued retained token was denied stream ownership")
                        raise PowerToolError(
                            f"radio rejected stream request error=0x{error:08x} "
                            f"disconnected={'yes' if disconnected else 'no'}")
                    serial_port = struct.unpack_from(">H", packet, 0x42)[0]
                    audio_port = struct.unpack_from(">H", packet, 0x46)[0]
                    self.serial.set_remote_port(serial_port)
                    self.audio.set_remote_port(audio_port)
                    # Ownership begins at this correlated success response.
                    # Persist the token immediately so even a crash before the
                    # media handshakes leaves enough material for removal.
                    with defer_interrupt_for_journal():
                        save_recovery_record(self.credentials, self.auth_id)
                        self.journal_saved = True
                        self.stream_owned = True
                    log_event(
                        "RESPONSE",
                        f"CI-V/audio stream request granted CI-V-port={serial_port} "
                        f"audio-port={audio_port}")
                elif (len(packet) == 144
                      and packet[0x14:0x16] == b"\x02\x03"):
                    response_sequence = struct.unpack_from(">H", packet, 0x16)[0]
                    if (sender == self.control.remote_id
                            and recipient == self.control.local_id
                            and response_sequence == self.stream_request_sequence
                            and packet[0x1A:0x20] == self.stream_request_auth):
                        log_event("RESPONSE", "current stream-grant advertisement received")
                    else:
                        log_event("RESPONSE", "ignored stale or unrelated stream grant")
            if (self.authenticated and self.radio_id != b"\0" * 16
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
        save_recovery_record(
            self.credentials, self.auth_id, self.control.identity,
            self.serial.identity, self.audio.identity)
        self.journal_saved = True
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
                    _, _, _, sender, recipient = header(packet)
                    if (inner_sequence == self.pending_renewal
                            and sender == self.control.remote_id
                            and recipient == self.control.local_id):
                        response = struct.unpack_from("<I", packet, 0x30)[0]
                        if response != 0:
                            raise PowerToolError(
                                f"radio rejected token renewal response=0x{response:08x}")
                        with defer_interrupt_for_journal():
                            self.auth_id = bytes(packet[0x1A:0x20])
                            self.pending_renewal = None
                            if self.stream_owned:
                                save_recovery_record(
                                    self.credentials, self.auth_id,
                                    self.control.identity, self.serial.identity,
                                    self.audio.identity)
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

    def remove_authentication(self) -> bool:
        """Remove this session's token and require a correlated acknowledgement."""
        request_sequence = self.inner_sequence
        packet = self._auth_packet(1)
        for attempt in range(1, TOKEN_REMOVAL_ATTEMPTS + 1):
            log_event(
                "ACTION",
                f"remove authentication token attempt={attempt}/{TOKEN_REMOVAL_ATTEMPTS}")
            self._tracked_control(packet)
            deadline = now() + TOKEN_REMOVAL_INTERVAL
            while now() < deadline:
                for response in self.control.receive(min(0.05, max(0, deadline - now()))):
                    _, _, _, sender, recipient = header(response)
                    if sender != self.control.remote_id or recipient != self.control.local_id:
                        continue
                    if (len(response) == 64 and response[0x14:0x16] == b"\x02\x01"
                            and struct.unpack_from(">H", response, 0x16)[0] == request_sequence):
                        result = struct.unpack_from("<I", response, 0x30)[0]
                        log_event(
                            "RESPONSE",
                            f"token removal acknowledged response=0x{result:08x}")
                        return True
                    if (len(response) == 80 and response[0x40] == 1
                            and struct.unpack_from("<I", response, 0x30)[0] == 0
                            and response[0x1A:0x20] == self.auth_id):
                        log_event("RESPONSE", "disconnect status acknowledged")
                        return True
                self.control.idle()
        log_event("VALIDATION", "token removal acknowledgement timed out")
        return False

    def settle_departed_streams(self, duration: float) -> None:
        """Wait out the close interval without touching departed media."""
        deadline = now() + duration
        while now() < deadline:
            self.control.receive(0)
            self.control.idle()
            time.sleep(min(0.02, max(0, deadline - now())))

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
                self.serial.depart()
                self.audio.depart()
                # Match the radio's required teardown ordering: allow the CI-V
                # close and media departures to settle before token removal.
                # Do not call pump(): it emits media keepalives that would
                # contradict the departures we just sent.
                self.settle_departed_streams(TOKEN_REMOVAL_INTERVAL)
                acknowledged = self.remove_authentication()
                self.control.depart()
                log_event("ACTION", "owned session cleanup completed")
                if acknowledged:
                    remove_recovery_record(self.credentials)
                    self.journal_saved = False
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
    acquire_session_lock(credentials)
    recovered_predecessor = recover_journaled_session(credentials)
    for attempt in range(2):
        session = PowerSession(credentials, recovered_predecessor)
        try:
            session.open()
            return session
        except RetainedTokenStreamRejected:
            # With no usable predecessor journal, the radio may still reissue
            # the old authentication identifier during this login. Once a
            # correlated stream rejection proves it cannot own media, remove
            # that reissued token and perform one entirely fresh login.
            if attempt != 0 or not session.authenticated:
                session.close()
                raise
            log_event(
                "RECOVERY",
                "reissued token denied stream ownership; remove it before fresh login")
            if not session.remove_authentication():
                session.close()
                raise PowerToolError(
                    "could not acknowledge removal of reissued retained token")
            session.control.depart()
            session.close()
            recovered_predecessor = True
        except Exception:
            session.close()
            raise
    raise PowerToolError("retained-session recovery exhausted")


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
                if session_number == 0 and not session.recovered_predecessor:
                    session.startup_outcome = "normal"
                elif (session_number == 0 and session.recovered_predecessor
                      or session_number == 1):
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
