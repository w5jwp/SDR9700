#!/usr/bin/env python3
"""Shared discovery and transport helpers for SDR9700 automation tools."""

import glob
import json
import os
import platform
import socket


def _config_roots():
    override = os.environ.get("XDG_CONFIG_HOME")
    if override:
        yield override
    if platform.system() == "Darwin":
        yield os.path.expanduser("~/Library/Preferences")
    yield os.path.expanduser("~/.config")


def discovery_files():
    candidates = []
    for root in _config_roots():
        candidates.extend(glob.glob(os.path.join(root, "SDR9700", "automation",
                                                 "sdr9700-automation-*.json")))
    return sorted(set(candidates), key=os.path.getmtime, reverse=True)


def load_endpoint():
    failures = []
    for path in discovery_files():
        try:
            with open(path, encoding="utf-8") as stream:
                endpoint = json.load(stream)
            if endpoint.get("application") != "SDR9700":
                raise RuntimeError("unexpected application identity")
            if endpoint.get("protocol") != 1:
                raise RuntimeError(f"unsupported protocol {endpoint.get('protocol')!r}")
            if endpoint.get("transmitAllowed") is not False:
                raise RuntimeError("endpoint does not explicitly prohibit transmit")
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(1)
                client.connect(endpoint["socket"])
            endpoint["discoveryFile"] = path
            return endpoint
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            failures.append(f"{path}: {error}")
    details = "; ".join(failures) if failures else "no discovery records found"
    raise RuntimeError(
        "No live SDR9700 automation bridge found; start the app with "
        f"--enable-automation ({details})"
    )


def request(endpoint, payload, timeout=5):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(timeout)
        client.connect(endpoint["socket"])
        client.sendall(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                raise RuntimeError("automation bridge closed before returning a response")
            data.extend(chunk)
    return json.loads(data)
