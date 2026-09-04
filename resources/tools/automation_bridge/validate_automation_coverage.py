#!/usr/bin/env python3
"""Audit static radio commands and live UI controls against hardware coverage."""

import argparse
import json
import pathlib
import re

from automation_common import load_endpoint, request

ROOT = pathlib.Path(__file__).resolve().parents[3]

# Every IRadioBackend operation must stay in exactly one disposition. Adding or
# removing an interface method makes this audit fail until the change is
# deliberately classified.
BACKEND_COVERAGE = {
    "hardware-confirmed": {
        "setAfGain", "setDialLockEnabled", "setLanModLevel",
        "setTxPower", "setVfoAgcMode", "setVfoAttenuatorEnabled", "setVfoFilter", "setVfoFrequencyHz",
        "setVfoMode", "setVfoNbEnabled", "setVfoNbLevel", "setVfoNotch", "setVfoNrEnabled", "setVfoNrLevel",
        "setVfoPreampLevel", "setVfoRfGain", "setVfoSquelch", "setDualWatchEnabled", "selectVfo",
        "exchangeMainSub",
    },
    "transmit-prohibited": {"setPtt", "setTxAudioDevice", "sendDtmf"},
    "destructive-opt-in": {"writeRadioMemory"},
    "lifecycle-manual": {"connectToRadio", "disconnectFromRadio", "stopLocalAudio"},
    "read-only-or-internal": {
        "applyVfoBandRecall", "pollFrequency", "requestVfoState", "selectVfoMode", "setRxAudioDevice",
    },
    "legacy-or-indirect": {
        "setAgcMode", "setAttenuatorEnabled", "setAutoNotch", "setFilterWidth", "setFrequencyHz",
        "setManualNotch", "setMode", "setNbEnabled", "setNbLevel", "setNrEnabled", "setNrLevel",
        "setPreampEnabled", "setPreampLevel", "setRfGain", "setSquelch",
    },
    "coverage-gap": {
        "requestRadioMemory", "requestSatelliteMemory", "selectRadioMemory", "setCompressor", "setCompressorLevel",
        "setDtcsCode", "setDuplexMode", "setRepeaterOffsetHz", "setRitEnabled", "setRitOffset", "setScopeEnabled",
        "setScopeFixedRangeHz", "setScopeMode", "setScopeSpanHz", "setScopeVfo", "setToneAccessMode",
        "setToneFrequency", "setTuningStep", "setXfcEnabled",
    },
}

RADIO_UI_NAMES = {
    *(f"{vfo} VFO {name}" for vfo in ("MAIN", "SUB") for name in (
        "AGC control", "ATT control", "FILTERS control", "PRE control", "RFG control", "SQL control", "band",
        "frequency", "mode", "receive filter", "NB level", "NOTCH level", "NR level")),
    "MAIN VFO TX PWR control", "MAIN VFO TX PWR level", "MAIN VFO MOD control", "MAIN VFO MOD level",
    "Select MAIN VFO", "Select SUB VFO", "Exchange MAIN and SUB VFOs", "Toggle dual watch",
}


def backend_methods():
    header = (ROOT / "src/backend/IRadioBackend.h").read_text(encoding="utf-8")
    return set(re.findall(r"virtual\s+(?:bool|void)\s+(\w+)\s*\(", header))


def static_inventory():
    methods = backend_methods()
    declared = set().union(*BACKEND_COVERAGE.values())
    duplicates = sorted(name for name in declared if sum(name in group for group in BACKEND_COVERAGE.values()) != 1)
    return {"counts": {key: len(value) for key, value in BACKEND_COVERAGE.items()},
            "unclassified": sorted(methods - declared), "stale": sorted(declared - methods),
            "duplicates": duplicates, "coverageGaps": sorted(BACKEND_COVERAGE["coverage-gap"])}


def control_key(control):
    return "|".join(str(control.get(field, "")) for field in
                    ("window", "type", "objectName", "accessibleName", "text"))


def classify_control(control):
    name = control.get("accessibleName", "")
    object_name = control.get("objectName", "")
    text = control.get("text", "").replace("&", "")
    if control.get("pttProhibited"):
        return "transmit-prohibited"
    if name in RADIO_UI_NAMES or name in ("Dial locked", "Dial unlocked"):
        return "hardware-script"
    if control.get("type") == "QSlider" and not name and not object_name:
        return "hardware-script"
    if object_name in {"spectrumStepSelector", "spectrumPeakHoldSelector", "spectrumRecenterButton"}:
        return "coverage-gap"
    if name in {"MAIN VFO COMP control", "MAIN VFO OFFSET control", "SUB VFO OFFSET control",
                "MAIN VFO TONE control", "SUB VFO TONE control", "MAIN VFO XFC control",
                "Spectrum Scope pan", "Previous peak hold", "Next peak hold", "Previous span", "Next span",
                "Previous step", "Next step", "Recenter spectrum"}:
        return "coverage-gap"
    if name in {"MAIN VFO indicator", "SUB VFO indicator"}:
        return "status-only"
    if object_name in {"radioConnectionAction", "quitAction"} or name in {
            "Audio mute", "Minimize window", "Close window"}:
        return "local-or-lifecycle"
    if control.get("type") == "QPushButton" and text == "00:00:00":
        return "local-or-lifecycle"
    if text in {"Data Decoder", "Memory Manager", "Meters", "Application Log", "About", "Settings…"}:
        return "dialog-inventory-entry"
    if control.get("type") == "QAction" and text in {
            "", "File", "Help", "Settings", "View", "Window", "Main Window"}:
        return "menu-plumbing"
    if control.get("type") == "QAction" and text == "DTMF":
        return "transmit-prohibited"
    return "unclassified"


def open_inventory_windows(endpoint):
    for text in ("Data Decoder", "DTMF", "Memory Manager", "Meters", "Application Log", "Settings…", "About"):
        response = request(endpoint, {"action": "ui_list"})
        matches = [item for item in response.get("controls", [])
                   if item.get("type") == "QAction" and item.get("text", "").replace("&", "") == text]
        if len(matches) != 1:
            raise RuntimeError(f"expected one action to inventory {text}: {matches}")
        activated = request(endpoint, {"action": "ui_activate", "controlId": matches[0]["id"]})
        if not activated.get("ok"):
            raise RuntimeError(f"could not open {text}: {activated}")


def runtime_inventory(open_windows):
    endpoint = load_endpoint()
    if open_windows:
        open_inventory_windows(endpoint)
    response = request(endpoint, {"action": "ui_list"})
    if not response.get("ok"):
        raise RuntimeError(response)
    classified = {}
    for control in response["controls"]:
        if not control.get("visible") and control.get("type") != "QAction":
            continue
        disposition = classify_control(control)
        classified.setdefault(disposition, []).append(control_key(control))
    return {key: sorted(values) for key, values in sorted(classified.items())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static-only", action="store_true")
    parser.add_argument("--main-window-only", action="store_true",
                        help="Do not open safe dialogs before collecting controls")
    args = parser.parse_args()
    report = {"static": static_inventory()}
    if not args.static_only:
        report["runtime"] = runtime_inventory(not args.main_window_only)
    print(json.dumps(report, indent=2, sort_keys=True))
    static = report["static"]
    invalid = (static["unclassified"] or static["stale"] or static["duplicates"] or
               static["coverageGaps"])
    if not args.static_only:
        invalid = invalid or report["runtime"].get("unclassified")
    return 1 if invalid else 0


if __name__ == "__main__":
    raise SystemExit(main())
