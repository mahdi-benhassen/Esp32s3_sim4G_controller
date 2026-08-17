#!/usr/bin/env python3
"""Minimal serial HIL acceptance test for an assembled ESP32-S3 controller.

The script intentionally uses only the Python standard library until execution;
pyserial is required only on the technician's test workstation. It sends the
same safe, read-only commissioning commands documented in the user manual and
fails if expected response markers are not observed.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from typing import Iterable


COMMANDS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("help", ("Commands:",)),
    ("input", ("INPUT1=", "INPUT2=")),
    ("adc 1", ("ADC1=", "ADC1 read failed")),
    ("adc 3", ("ADC3=", "ADC3 read failed")),
    ("modem", ("MODEM",)),
    ("storage", ("SD mounted=",)),
    ("button", ("BUTTON",)),
    ("time", ("TIME",)),
    ("events", ("EVENTS",)),
    ("ota status", ("OTA pending_verification=",)),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0, help="Seconds to wait for each command response")
    parser.add_argument("--settle", type=float, default=1.0, help="Seconds to wait after opening the port")
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
            if any(pattern.search("".join(chunks)) for pattern in compiled):
                break
    return "".join(chunks)


def main() -> int:
    args = parse_args()
    try:
        import serial  # type: ignore
    except ImportError:
        print("ERROR: install pyserial on the HIL workstation: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
            time.sleep(args.settle)
            ser.reset_input_buffer()
            failures = 0
            for command, patterns in COMMANDS:
                ser.write((command + "\n").encode("ascii"))
                response = read_until(ser, patterns, args.timeout)
                ok = any(re.search(pattern, response) for pattern in patterns)
                print(f"{'PASS' if ok else 'FAIL'} {command}: {response.strip()[-240:]}")
                failures += not ok
            return 1 if failures else 0
    except (OSError, serial.SerialException) as exc:
        print(f"ERROR: unable to open or communicate with {args.port}: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

