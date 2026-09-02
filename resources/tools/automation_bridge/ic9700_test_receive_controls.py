#!/usr/bin/env python3
"""Test every receive-side VFO control through SDR9700 automation."""

import glob
import json
import os
import socket
import tempfile
import time


DISCOVERY = max(glob.glob(os.path.join(tempfile.gettempdir(), "sdr9700-automation-*.json")), key=os.path.getmtime)
with open(DISCOVERY, encoding="utf-8") as stream:
    ENDPOINT = json.load(stream)
if ENDPOINT.get("transmitAllowed") is not False:
    raise RuntimeError("automation endpoint permits transmit")

requests = 0


def request(payload):
    global requests
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(ENDPOINT["socket"])
        client.sendall(json.dumps(payload, separators=(",", ":")).encode() + b"\n")
        data = bytearray()
        while not data.endswith(b"\n"):
            data.extend(client.recv(65536))
    requests += 1
    return json.loads(data)


def state():
    response = request({"action": "get_state"})
    if not response.get("ok"):
        raise RuntimeError(response)
    value = response["state"]
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


def accepted(payload):
    response = request(payload)
    if not response.get("ok"):
        raise RuntimeError(f"request rejected: {payload}: {response}")
    return response


def controls():
    response = request({"action": "ui_list"})
    if not response.get("ok"):
        raise RuntimeError(response)
    return response["controls"]


def find_control(accessible_name=None, text=None, type_name=None, object_name=None):
    matches = []
    for item in controls():
        if not item.get("visible") or not item.get("enabled"):
            continue
        if accessible_name is not None and item.get("accessibleName") != accessible_name:
            continue
        if text is not None and item.get("text", "").replace("&", "") != text:
            continue
        if type_name is not None and item.get("type") != type_name:
            continue
        if object_name is not None and item.get("objectName") != object_name:
            continue
        matches.append(item)
    if len(matches) != 1:
        raise RuntimeError(f"expected one control name={accessible_name} text={text} type={type_name}: {matches}")
    return matches[0]


def activate_name(name):
    item = find_control(accessible_name=name)
    accepted({"action": "ui_activate", "controlId": item["id"]})
    time.sleep(0.08)


def choose_menu(name, option):
    activate_name(name)
    if " VFO AGC control" in name:
        vfo = name.split(" ", 1)[0].lower()
        matches = [item for item in controls()
                   if item.get("visible") and item.get("enabled") and item.get("type") == "QAction"
                   and item.get("text") == option
                   and item.get("objectName") == f"{vfo}VfoAgc{option}Action"]
        if not matches:
            raise RuntimeError(f"missing {vfo} AGC {option} action")
        item = matches[-1]
    else:
        vfo = name.split(" ", 1)[0].lower()
        matches = [item for item in controls()
                   if item.get("visible") and item.get("enabled") and item.get("type") == "QAction"
                   and item.get("text") == option
                   and item.get("objectName") == f"{vfo}VfoMode{option}Action"]
        if not matches:
            raise RuntimeError(f"missing {vfo} mode {option} action")
        item = matches[-1]
    accepted({"action": "ui_activate", "controlId": item["id"]})
    time.sleep(0.08)


def set_popup_slider(button_name, slider_name, value):
    activate_name(button_name)
    slider = find_control(accessible_name=slider_name, type_name="QSlider")
    accepted({"action": "ui_set", "controlId": slider["id"], "value": value})
    time.sleep(0.08)


def set_dual(enabled):
    current = state()
    if current.get("dualWatch") == enabled and (not enabled or current["receivers"]["SUB"].get("mode")):
        return current
    deadline = time.monotonic() + 12
    while True:
        response = request({"action": "set_dual_watch", "enabled": enabled})
        if response.get("ok"):
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(response)
        time.sleep(0.05)
    if enabled:
        return wait_for("Dual Watch identity", lambda s: s.get("dualWatch") is True and
                        s["receivers"]["SUB"].get("mode") is not None and
                        s["receivers"]["SUB"].get("filter") is not None)
    return wait_for("Dual Watch off", lambda s: s.get("dualWatch") is False and
                    s["receivers"]["SUB"].get("frequencyHz") is None)


def select_band(vfo, band):
    current = state()
    if current["receivers"][vfo].get("band") == band.upper():
        return current
    deadline = time.monotonic() + 12
    while True:
        response = request({"action": "select_band", "vfo": vfo, "band": band})
        if response.get("ok"):
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(response)
        time.sleep(0.05)
    result = wait_for(f"{vfo} {band}", lambda s: s["receivers"][vfo].get("band") == band.upper() and
                      s["receivers"][vfo].get("mode") is not None and
                      s["receivers"][vfo].get("filter") is not None)
    time.sleep(0.3)
    return result


def normalize_pair(main_band, sub_band):
    current = state()
    if current["receivers"]["SUB"].get("band") == main_band.upper():
        temporary = next(item for item in ("2m", "70cm", "23cm")
                         if item.upper() not in (current["receivers"]["MAIN"].get("band"), main_band.upper()))
        select_band("SUB", temporary)
    select_band("MAIN", main_band)
    select_band("SUB", sub_band)


def tune(vfo, hz):
    other = "SUB" if vfo == "MAIN" else "MAIN"
    other_before = state()["receivers"][other].get("frequencyHz")
    accepted({"action": "set_frequency", "vfo": vfo, "frequencyHz": hz})
    result = wait_for(f"{vfo} tune {hz}", lambda s: s["receivers"][vfo].get("frequencyHz") == hz)
    if result["receivers"][other].get("frequencyHz") != other_before:
        raise RuntimeError(f"{vfo} tuning changed {other}: {result}")


def toggle(vfo, label, field):
    other = "SUB" if vfo == "MAIN" else "MAIN"
    before = wait_for(f"initial {field} on {vfo}", lambda s: s["receivers"][vfo].get(field) is not None, timeout=25)
    own_before = before["receivers"][vfo].get(field)
    other_before = before["receivers"][other].get(field)
    if own_before is None:
        raise RuntimeError(f"missing initial {field}: {before}")
    activate_name(f"{vfo} VFO {label} control")
    changed = wait_for(f"{vfo} {field} changed", lambda s: s["receivers"][vfo].get(field) != own_before)
    if changed["receivers"][other].get(field) != other_before:
        raise RuntimeError(f"{vfo} {field} bled into {other}: {changed}")
    activate_name(f"{vfo} VFO {label} control")
    restored = wait_for(f"{vfo} {field} restored", lambda s: s["receivers"][vfo].get(field) == own_before)
    if restored["receivers"][other].get(field) != other_before:
        raise RuntimeError(f"{vfo} {field} restore bled into {other}: {restored}")


def sweep_slider(vfo, label, field):
    other = "SUB" if vfo == "MAIN" else "MAIN"
    before = wait_for(f"initial {field} on {vfo}", lambda s: s["receivers"][vfo].get(field) is not None, timeout=25)
    original = before["receivers"][vfo].get(field)
    other_before = before["receivers"][other].get(field)
    if original is None:
        raise RuntimeError(f"missing initial {field}: {before}")
    for value in (0, 64, 192, 255, 32, original):
        set_popup_slider(f"{vfo} VFO {label} control", f"{vfo} VFO {label} level", value)
        changed = wait_for(f"{vfo} {field}={value}", lambda s: s["receivers"][vfo].get(field) == value)
        if changed["receivers"][other].get(field) != other_before:
            raise RuntimeError(f"{vfo} {field} bled into {other}: {changed}")


started = time.monotonic()
set_dual(True)

# Walk each supported band upward and downward on each receiver. A companion
# band remains assigned to the other receiver so every state is legal.
band_frequencies = {
    "2m": [144_100_000, 144_500_000, 145_000_000, 145_500_000, 146_000_000, 146_500_000, 147_000_000, 147_500_000],
    "70cm": [430_100_000, 432_100_000, 435_000_000, 438_000_000, 440_000_000, 443_000_000, 446_000_000, 449_900_000],
    "23cm": [1_240_100_000, 1_250_000_000, 1_260_000_000, 1_270_000_000, 1_280_000_000, 1_290_000_000, 1_296_100_000, 1_299_900_000],
}
companion = {"2m": "70cm", "70cm": "23cm", "23cm": "2m"}
frequency_transitions = 0
for vfo in ("MAIN", "SUB"):
    other = "SUB" if vfo == "MAIN" else "MAIN"
    for band, values in band_frequencies.items():
        if vfo == "MAIN":
            normalize_pair(band, companion[band])
        else:
            normalize_pair(companion[band], band)
        for hz in values + list(reversed(values)):
            tune(vfo, hz)
            frequency_transitions += 1
        print(f"FREQUENCY {vfo} {band} complete total={frequency_transitions}", flush=True)

normalize_pair("2m", "70cm")
for vfo in ("MAIN", "SUB"):
    selector = find_control(accessible_name=f"Select {vfo} VFO", object_name="vfoIdentityButton")
    accepted({"action": "ui_activate", "controlId": selector["id"]})
    time.sleep(0.08)
    wait_for(f"select {vfo}", lambda s, vfo=vfo: s.get("selectedVfo") == vfo)
    for label, field in (("ATT", "attenuatorEnabled"), ("NB", "nbEnabled"),
                         ("NOTCH", "autoNotchEnabled"), ("NR", "nrEnabled"),
                         ("PRE", "preampLevel")):
        toggle(vfo, label, field)
        print(f"TOGGLE {vfo} {label} complete", flush=True)
    sweep_slider(vfo, "SQL", "squelch")
    print(f"SWEEP {vfo} SQL complete", flush=True)
    sweep_slider(vfo, "RFG", "rfGain")
    print(f"SWEEP {vfo} RFG complete", flush=True)

# AGC is not operator-selectable in FM. Exercise every choice after moving
# each receiver to USB, then restore FM.
for vfo in ("MAIN", "SUB"):
    choose_menu(f"{vfo} VFO mode", "USB")
    wait_for(f"{vfo} USB", lambda s: s["receivers"][vfo].get("mode") == "USB")
    for option, expected in (("FAST", 1), ("MID", 2), ("SLOW", 3), ("FAST", 1)):
        choose_menu(f"{vfo} VFO AGC control", option)
        wait_for(f"{vfo} AGC {option}", lambda s, expected=expected: s["receivers"][vfo].get("agcMode") == expected)
    choose_menu(f"{vfo} VFO mode", "FM")
    wait_for(f"{vfo} FM", lambda s: s["receivers"][vfo].get("mode") == "FM")
    print(f"MODE/AGC {vfo} complete", flush=True)

final = state()
print("CONTROL MATRIX COMPLETE", json.dumps({"frequencyTransitions": frequency_transitions,
                                              "requests": requests,
                                              "elapsedSeconds": round(time.monotonic() - started, 2),
                                              "final": final}, sort_keys=True), flush=True)
