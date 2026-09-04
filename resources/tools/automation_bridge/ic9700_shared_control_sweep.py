#!/usr/bin/env python3
"""Sweep shared main-window controls without invoking PTT."""

import json
import time

from automation_common import load_endpoint, request as send_request


endpoint = load_endpoint()


def request(payload):
    response = send_request(endpoint, payload)
    if not response.get("ok"):
        raise RuntimeError(f"request failed: {payload}: {response}")
    return response


def controls():
    return request({"action": "ui_list"})["controls"]


def state():
    value = request({"action": "get_state"})["state"]
    if value.get("transmitAllowed") is not False or value.get("transmitting") is not False:
        raise RuntimeError(f"unsafe transmit state: {value}")
    if not value.get("connected") or not value.get("ready"):
        raise RuntimeError(f"radio unavailable: {value}")
    return value


def wait_for(label, predicate, timeout=12):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = state()
        if predicate(last):
            return last
        time.sleep(0.05)
    raise RuntimeError(f"timeout waiting for {label}: {json.dumps(last, sort_keys=True)}")


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


def sweep_popup(state_field, button_name, slider_name):
    original = None
    for value in (0, 64, 128, 192, 255):
        button = find(accessibleName=button_name)
        request({"action": "ui_activate", "controlId": button["id"]})
        time.sleep(0.08)
        slider = find(type="QSlider", accessibleName=slider_name)
        if original is None:
            original = slider["value"]
        request({"action": "ui_set", "controlId": slider["id"], "value": value})
        wait_for(f"{state_field}={value}", lambda s, value=value: s.get(state_field) == value)
    button = find(accessibleName=button_name)
    request({"action": "ui_activate", "controlId": button["id"]})
    time.sleep(0.08)
    slider = find(type="QSlider", accessibleName=slider_name)
    request({"action": "ui_set", "controlId": slider["id"], "value": original})
    wait_for(f"{state_field} restored", lambda s: s.get(state_field) == original)
    print(f"SWEEP {slider_name} complete", flush=True)


sweep_popup("lanModLevel", "MAIN VFO MOD control", "MAIN VFO MOD level")

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
# LAN AF is deliberately local Qt playback volume, not the radio's physical
# AF register, so UI settlement is the authoritative assertion for this one control.
print("SWEEP AF gain complete (local audio control)", flush=True)

original_tx_power = state().get("txPower")
if original_tx_power is None:
    raise RuntimeError("radio did not report initial TX power")
for value in (0, 64, 128, 192, 255, original_tx_power):
    button = find(accessibleName="MAIN VFO TX PWR control")
    request({"action": "ui_activate", "controlId": button["id"]})
    time.sleep(0.08)
    slider = find(type="QSlider", accessibleName="MAIN VFO TX PWR level")
    request({"action": "ui_set", "controlId": slider["id"], "value": value})
    wait_for(f"txPower={value}", lambda s, value=value: s.get("txPower") == value)
print("SWEEP MAIN VFO TX PWR level complete", flush=True)

initial_lock = state().get("dialLock")
if initial_lock is None:
    raise RuntimeError("radio did not report initial dial-lock state")
for expected in (not initial_lock, initial_lock):
    lock = [item for item in controls() if item.get("visible") and item.get("enabled") and
            item.get("accessibleName") in ("Dial locked", "Dial unlocked")]
    if len(lock) != 1:
        raise RuntimeError(f"expected one dial-lock control: {lock}")
    request({"action": "ui_activate", "controlId": lock[0]["id"]})
    wait_for(f"dialLock={expected}", lambda s, expected=expected: s.get("dialLock") is expected)
print("TOGGLE dial lock complete", flush=True)

final_state = request({"action": "get_state"})["state"]
if final_state.get("transmitAllowed") is not False or final_state.get("transmitting") is not False:
    raise RuntimeError(f"unsafe final state: {final_state}")
print("SHARED VALUE SWEEP COMPLETE", flush=True)
