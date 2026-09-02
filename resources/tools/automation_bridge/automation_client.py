#!/usr/bin/env python3
"""Send one allowlisted JSON request to an opted-in SDR9700 process."""

import argparse
import glob
import json
import os
import socket
import tempfile
import time


def newest_discovery_file() -> str:
    candidates = glob.glob(os.path.join(tempfile.gettempdir(), "sdr9700-automation-*.json"))
    if not candidates:
        raise RuntimeError("No SDR9700 automation bridge found; start the app with --enable-automation")
    return max(candidates, key=os.path.getmtime)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", help='JSON object, for example: {"action":"get_state"}')
    parser.add_argument("--discovery", help="Explicit automation discovery JSON file")
    parser.add_argument("--hold", type=float, default=0.0,
                        help="Keep the client connected for this many seconds after receiving the response")
    parser.add_argument("--match", help="For ui_list, print only controls whose description contains this text")
    args = parser.parse_args()

    request = json.loads(args.request)
    if not isinstance(request, dict):
        raise ValueError("request must be a JSON object")

    with open(args.discovery or newest_discovery_file(), encoding="utf-8") as discovery_file:
        discovery = json.load(discovery_file)
    if discovery.get("transmitAllowed") is not False:
        raise RuntimeError("Refusing a bridge that does not explicitly prohibit transmit operations")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(discovery["socket"])
        client.sendall(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")
        response = bytearray()
        while not response.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                raise RuntimeError("Automation bridge closed before returning a response")
            response.extend(chunk)
        if args.hold > 0:
            time.sleep(args.hold)
    decoded = json.loads(response)
    if args.match and isinstance(decoded.get("controls"), list):
        needle = args.match.casefold()
        decoded["controls"] = [control for control in decoded["controls"]
                               if needle in json.dumps(control, sort_keys=True).casefold()]
    print(json.dumps(decoded, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
