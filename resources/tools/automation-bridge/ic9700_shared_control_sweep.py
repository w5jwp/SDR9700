#!/usr/bin/env python3
"""Sweep shared main-window controls without invoking PTT."""

import glob
import json
import os
import socket
import tempfile
import time


discovery = max(glob.glob(os.path.join(tempfile.gettempdir(), "sdr9700-automation-*.json")),
                key=os.path.getmtime)
with open(discovery, encoding="utf-8") as stream:
    endpoint = json.load(stream)
if endpoint.get("transmitAllowed") is not False:
    raise RuntimeError("automation endpoint permits transmit")


def request(payload):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(endpoint["socket"])
        client.sendall(json.dumps(payload, separators=(",", ":")).encode() + b"\n")
        data = bytearray()
        while not data.endswith(b"\n"):
            data.extend(client.recv(65536))
    response = json.loads(data)
    if not response.get("ok"):
        raise RuntimeError(f"request failed: {payload}: {response}")
    return response


def controls():
    return request({"action": "ui_list"})["controls"]


def find(**criteria):
    matches = [item for item in controls()
               if item.get("visible") and item.get("enabled") and
               all(item.get(key) == value for key, value in criteria.items())]
    if len(matches) != 1:
        raise RuntimeError(f"expected one control {criteria}: {matches}")
    return matches[0]


def set_and_verify(control, value):
    request({"action": "ui_set", "controlId": control["id"], "value": value})
    time.sleep(0.10)
    current = find(type=control["type"], objectName=control.get("objectName", ""),
                   accessibleName=control.get("accessibleName", ""))
    if current.get("value") != value:
        raise RuntimeError(f"value did not settle at {value}: {current}")


def sweep_persistent(accessible_name=None, object_name=None):
    criteria = {"type": "QSlider"}
    if accessible_name is not None:
        criteria["accessibleName"] = accessible_name
    if object_name is not None:
        criteria["objectName"] = object_name
    original = find(**criteria)["value"]
    for value in (0, 64, 128, 192, 255, original):
        set_and_verify(find(**criteria), value)
    print(f"SWEEP {accessible_name or object_name or 'unnamed slider'} complete", flush=True)


def sweep_popup(button_name, slider_name):
    original = None
    for value in (0, 64, 128, 192, 255):
        button = find(accessibleName=button_name)
        request({"action": "ui_activate", "controlId": button["id"]})
        time.sleep(0.08)
        slider = find(type="QSlider", accessibleName=slider_name)
        if original is None:
            original = slider["value"]
        request({"action": "ui_set", "controlId": slider["id"], "value": value})
        time.sleep(0.12)
    button = find(accessibleName=button_name)
    request({"action": "ui_activate", "controlId": button["id"]})
    time.sleep(0.08)
    slider = find(type="QSlider", accessibleName=slider_name)
    request({"action": "ui_set", "controlId": slider["id"], "value": original})
    time.sleep(0.12)
    print(f"SWEEP {slider_name} complete", flush=True)


sweep_persistent(accessible_name="LAN modulation level")

# The AF slider predates accessible-name coverage. It is the only visible,
# unnamed persistent slider in the main window.
unnamed = [item for item in controls() if item.get("type") == "QSlider" and item.get("visible") and
           not item.get("accessibleName") and not item.get("objectName")]
if len(unnamed) != 1:
    raise RuntimeError(f"expected one unnamed AF slider: {unnamed}")
original_af = unnamed[0]["value"]
for value in (0, 64, 128, 192, 255, original_af):
    slider = [item for item in controls() if item.get("type") == "QSlider" and item.get("visible") and
              not item.get("accessibleName") and not item.get("objectName")]
    if len(slider) != 1:
        raise RuntimeError(f"AF slider became ambiguous: {slider}")
    set_and_verify(slider[0], value)
print("SWEEP AF gain complete", flush=True)

sweep_popup("MAIN VFO TX PWR control", "MAIN VFO TX PWR level")

final_state = request({"action": "get_state"})["state"]
if final_state.get("transmitAllowed") is not False or final_state.get("transmitting") is not False:
    raise RuntimeError(f"unsafe final state: {final_state}")
print("SHARED VALUE SWEEP COMPLETE", flush=True)
