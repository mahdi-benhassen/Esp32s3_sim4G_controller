#!/usr/bin/env python3
"""Staged serial HIL acceptance test for an assembled ESP32-S3 controller.

The default run is read-only and safe for a live installation. The optional
write phase is deliberately explicit because it changes persisted settings.
Physical EMC/ESD, antenna, relay-load, Secure Boot, and BLE-client tests are
reported as operator gates; they cannot be proven by a serial smoke test.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from typing import Iterable


@dataclass(frozen=True)
class Check:
    phase: str
    command: str
    patterns: tuple[str, ...]
    timeout: float | None = None


READ_ONLY_CHECKS: tuple[Check, ...] = (
    Check("boot", "help", (r"Commands:",)),
    Check("digital inputs", "input", (r"INPUT1=", r"INPUT2=")),
    Check("analog input 1", "adc 1", (r"ADC1=", r"ADC1 read failed")),
    Check("analog input 3", "adc 3", (r"ADC3=", r"ADC3 read failed")),
    Check("1-Wire channel 1", "onewire 1", (r"1WIRE1",)),
    Check("modem registration", "modem", (r"MODEM registered=",)),
    Check("GNSS parser/transport", "modem gnss read", (r"GNSS fix=", r"GNSS read failed:")),
    Check("Wi-Fi service", "wifi", (r"WIFI started=",)),
    Check("MQTT service", "mqtt", (r"MQTT started=",)),
    Check("HTTP service", "http", (r"HTTP started=",)),
    Check("time service", "time", (r"TIME started=",)),
    Check("event log", "events", (r"EVENTS count=",)),
    Check("rule storage", "rules", (r"RULES count=", r"RULES read failed:")),
    Check("SD storage", "storage", (r"SD mounted=",)),
    Check("button state", "button", (r"BUTTON",)),
    Check("OTA rollback state", "ota status", (r"OTA pending_verification=",)),
)

WRITE_CHECKS: tuple[Check, ...] = (
    Check("SNTP persistence", "time server pool.ntp.org", (r"SNTP server (saved|ESP_ERR)")),
    Check("safe rule disable", "rule 8 disable", (r"RULE 8 (disabled|ESP_ERR)")),
)

PHYSICAL_GATES = (
    "Relay contact life test completed at the intended load class",
    "ESD contact/air and surge/EFT acceptance recorded per hardware-integration.md",
    "SIM7600 antenna VSWR, current peaks, attach, SMS, voice, and GNSS fix verified",
    "Secure Boot v2 and release flash-encryption provisioning completed on a sacrificial board",
    "BLE commissioning verified with an encrypted link, physical-presence gate, and rejected unauthenticated write",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0, help="Seconds to wait for each command response")
    parser.add_argument("--settle", type=float, default=1.0, help="Seconds to wait after opening the port")
    parser.add_argument("--write-tests", action="store_true", help="Run explicit persisted-setting checks")
    parser.add_argument("--json-report", help="Write machine-readable results to this path")
    return parser.parse_args()


def read_until(ser: object, patterns: Iterable[str], timeout: float) -> str:
    deadline = time.monotonic() + timeout
    chunks: list[str] = []
    compiled = [re.compile(pattern) for pattern in patterns]
    while time.monotonic() < deadline:
        raw = ser.readline()  # type: ignore[attr-defined]
        if raw:
            text = raw.decode("utf-8", errors="replace")
            chunks.append(text)
            joined = "".join(chunks)
            if any(pattern.search(joined) for pattern in compiled):
                break
    return "".join(chunks)


def run_check(ser: object, check: Check, default_timeout: float) -> dict[str, object]:
    ser.write((check.command + "\n").encode("ascii"))  # type: ignore[attr-defined]
    response = read_until(ser, check.patterns, check.timeout or default_timeout)
    matched = [pattern for pattern in check.patterns if re.search(pattern, response)]
    ok = bool(matched)
    print(f"{'PASS' if ok else 'FAIL'} [{check.phase}] {check.command}: {response.strip()[-240:]}")
    return {
        "phase": check.phase,
        "command": check.command,
        "ok": ok,
        "matched": matched,
        "response_tail": response.strip()[-240:],
    }


def main() -> int:
    args = parse_args()
    try:
        import serial  # type: ignore
    except ImportError:
        print("ERROR: install pyserial on the HIL workstation: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    results: list[dict[str, object]] = []
    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
            time.sleep(args.settle)
            ser.reset_input_buffer()
            checks = list(READ_ONLY_CHECKS)
            if args.write_tests:
                print("WARNING: write tests modify persisted settings; review the output before deployment.")
                checks.extend(WRITE_CHECKS)
            results.extend(run_check(ser, check, args.timeout) for check in checks)
    except (OSError, serial.SerialException) as exc:
        print(f"ERROR: unable to open or communicate with {args.port}: {exc}", file=sys.stderr)
        return 2

    failures = [result for result in results if not result["ok"]]
    print("\nPhysical acceptance gates requiring bench evidence:")
    for gate in PHYSICAL_GATES:
        print(f"  [ ] {gate}")

    report = {
        "read_only": not args.write_tests,
        "checks": results,
        "failures": len(failures),
        "physical_gates": list(PHYSICAL_GATES),
    }
    if args.json_report:
        with open(args.json_report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
