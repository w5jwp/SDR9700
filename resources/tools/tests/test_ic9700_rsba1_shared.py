#!/usr/bin/env python3
"""Offline tests for the generic Icom session lifecycle helpers."""

import contextlib
import io
import os
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import ic9700_rsba1_shared as shared


class SessionPacketTest(unittest.TestCase):
    def test_stream_request_matches_complete_audio_session(self) -> None:
        session = shared.PowerSession.__new__(shared.PowerSession)
        session.credentials = shared.Credentials("192.0.2.1", "operator", "secret")
        session.control = types.SimpleNamespace(local_id=1, remote_id=2)
        session.serial = types.SimpleNamespace(local_port=41002)
        session.audio = types.SimpleNamespace(local_port=41003)
        session.auth_id = bytes.fromhex("341278563412")
        session.radio_id = bytes(range(16))
        session.radio_name = "IC-9700"
        session.inner_sequence = 7

        packet = session._stream_request()

        self.assertEqual(packet[0x70:0x74], bytes((1, 1, 4, 4)))
        self.assertEqual(struct.unpack_from(">II", packet, 0x74), (48000, 16000))
        self.assertEqual(struct.unpack_from(">II", packet, 0x7C), (41002, 41003))
        self.assertEqual(struct.unpack_from(">I", packet, 0x84)[0], 80)

    def test_open_uses_correlated_status_as_ownership_boundary(self) -> None:
        streams = {}

        class FakeStream:
            def __init__(self, _host, remote_port, role):
                self.remote_port = remote_port
                self.role = role
                self.local_id = {"CI-V": 11, "audio": 12, "control": 13}[role]
                self.remote_id = {"CI-V": 21, "audio": 22, "control": 23}[role]
                self.local_port = {"CI-V": 41002, "audio": 41003, "control": 41001}[role]
                self.outer_sequence = 1
                self.receive_count = 0
                streams[role] = self

            @property
            def identity(self):
                return shared.TransportIdentity(
                    self.local_port, self.remote_port,
                    self.local_id, self.remote_id)

            def handshake(self):
                return None

            def send(self, _packet, tracked=False, copies=1):
                del tracked, copies

            def idle(self):
                return None

            def set_remote_port(self, value):
                self.remote_port = value

            def receive(self, _timeout=0):
                if self.role != "control":
                    return []
                self.receive_count += 1
                if self.receive_count == 1:
                    login = shared.framed(96, 0, 0, self.remote_id, self.local_id)
                    shared.set_inner(login, 80, 2, 0, session.login_sequence)
                    struct.pack_into("<H", login, 0x1A, session.login_token_request)
                    login[0x1A:0x20] = bytes.fromhex("341278563412")
                    # The tokrequest occupies the first two bytes of auth_id.
                    struct.pack_into("<H", login, 0x1A, session.login_token_request)
                    return [bytes(login)]
                if self.receive_count == 2:
                    capabilities = shared.framed(
                        168, 0, 0, self.remote_id, self.local_id)
                    capabilities[0x42:0x52] = bytes(range(16))
                    capabilities[0x52:0x59] = b"IC-9700"
                    token = shared.framed(64, 0, 0, self.remote_id, self.local_id)
                    shared.set_inner(
                        token, 48, 2, 5, session.initial_auth_sequence)
                    token[0x1A:0x20] = bytes.fromhex("785634127856")
                    return [bytes(capabilities), bytes(token)]
                if self.receive_count == 3:
                    stale = shared.framed(80, 0, 0, self.remote_id, self.local_id)
                    shared.set_inner(
                        stale, 64, 2, 3,
                        (session.stream_request_sequence + 1) & 0xFFFF)
                    stale[0x1A:0x20] = session.stream_request_auth
                    current = shared.framed(80, 0, 0, self.remote_id, self.local_id)
                    shared.set_inner(
                        current, 64, 2, 3, session.stream_request_sequence)
                    current[0x1A:0x20] = session.stream_request_auth
                    struct.pack_into(">H", current, 0x42, 50002)
                    struct.pack_into(">H", current, 0x46, 50003)
                    return [bytes(stale), bytes(current)]
                return []

            def close(self):
                return None

        with (mock.patch.object(shared, "Stream", FakeStream),
              mock.patch.object(shared, "save_recovery_record")):
            session = shared.PowerSession(
                shared.Credentials("192.0.2.1", "operator", "secret"))
            session.open()

        self.assertTrue(session.stream_owned)
        self.assertEqual(streams["CI-V"].remote_port, 50002)
        self.assertEqual(streams["audio"].remote_port, 50003)


class RecoveryJournalTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.temp_patch = mock.patch.object(
            shared.tempfile, "gettempdir", return_value=self.temporary.name)
        self.temp_patch.start()
        self.credentials = shared.Credentials("192.0.2.1", "operator", "secret")
        self.auth_id = bytes.fromhex("341278563412")
        self.identities = (
            shared.TransportIdentity(41001, 50001, 0x11111111, 0xAAAAAAAA),
            shared.TransportIdentity(41002, 50002, 0x22222222, 0xBBBBBBBB),
            shared.TransportIdentity(41003, 50003, 0x33333333, 0xCCCCCCCC),
        )

    def tearDown(self) -> None:
        self.temp_patch.stop()
        self.temporary.cleanup()

    def test_round_trips_token_only_then_complete_record(self) -> None:
        shared.save_recovery_record(self.credentials, self.auth_id)
        token_only = shared.load_recovery_record(self.credentials)
        self.assertEqual(token_only.auth_id, self.auth_id)
        self.assertIsNone(token_only.control)

        shared.save_recovery_record(
            self.credentials, self.auth_id, *self.identities)
        complete = shared.load_recovery_record(self.credentials)
        self.assertEqual(complete.control, self.identities[0])
        self.assertEqual(complete.serial, self.identities[1])
        self.assertEqual(complete.audio, self.identities[2])
        self.assertEqual(os.stat(shared.recovery_file(self.credentials)).st_mode & 0o777, 0o600)

    def test_rejects_partial_transport_identity_set(self) -> None:
        with self.assertRaises(shared.PowerToolError):
            shared.save_recovery_record(
                self.credentials, self.auth_id, self.identities[0])

    def test_recovery_retires_all_transports_before_token_removal(self) -> None:
        record = shared.RecoveryRecord(
            self.credentials.host, 999999, self.auth_id, *self.identities)
        operations = []
        with (mock.patch.object(shared, "load_recovery_record", return_value=record),
              mock.patch.object(
                  shared, "_retire_transport",
                  side_effect=lambda _host, _identity, role: operations.append(f"retire:{role}")),
              mock.patch.object(
                  shared, "_remove_predecessor_token",
                  side_effect=lambda *_args: operations.append("remove-token")),
              mock.patch.object(
                  shared, "remove_recovery_record",
                  side_effect=lambda *_args: operations.append("remove-journal"))):
            recovered = shared.recover_journaled_session(self.credentials)

        self.assertTrue(recovered)
        self.assertEqual(operations, [
            "retire:control", "retire:CI-V", "retire:audio",
            "remove-token", "remove-journal",
        ])


class RecoveryFallbackTest(unittest.TestCase):
    def test_reissued_token_is_removed_before_one_fresh_login(self) -> None:
        first = mock.Mock()
        first.authenticated = True
        first.open.side_effect = shared.RetainedTokenStreamRejected("retained")
        first.remove_authentication.return_value = True
        second = mock.Mock()
        sessions = iter((first, second))

        with (mock.patch.object(shared, "acquire_session_lock"),
              mock.patch.object(shared, "recover_journaled_session", return_value=False),
              mock.patch.object(shared, "PowerSession", side_effect=lambda *_args: next(sessions))):
            result = shared.open_session(
                shared.Credentials("192.0.2.1", "operator", "secret"))

        self.assertIs(result, second)
        first.remove_authentication.assert_called_once_with()
        first.control.depart.assert_called_once_with()
        first.close.assert_called_once_with()
        second.open.assert_called_once_with()


class StartupSummaryTest(unittest.TestCase):
    def test_normal_summary_uses_two_state_lines(self) -> None:
        session = types.SimpleNamespace(
            radio_name="IC-9700", startup_outcome="normal",
            recovered_hung_session=False, radio_woken=False, wake_attempts=0)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            shared.log_startup_summary(session)
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[0].endswith("STATE IC-9700 was already awake"))
        self.assertTrue(lines[1].endswith(
            "STATE startup=normal hung-session-recovered=no "
            "radio-woken=no wake-attempts=0"))


if __name__ == "__main__":
    unittest.main()
