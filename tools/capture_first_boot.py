#!/usr/bin/env python3

import argparse
import sys
import threading
import time
from pathlib import Path

import serial


REQUIRED_MARKERS = (
    "RodakRecovery: Starting immutable SD recovery runtime",
    "RodakOS: Starting RodakOS with Board Manager HAL",
    "RodakOS: Board manager initialized",
    "RodakOS: Touch input registered with cached polling",
    "RodakOS: LVGL port initialized",
    "BacklightAdapter: Backlight adapter initialized",
    "PhoneSystem: Starting Phone OS",
    "HomeApp: Phone desktop ready with",
    "OtaUpdate: Local boot confirmation complete",
    "RodakOS: RodakOS started successfully",
)

FAILURE_MARKERS = (
    "bootloader rejected the main image",
    "app registry validation failed",
    "phonesystem start failed",
    "cannot confirm a non-ota_0",
    "cannot confirm image",
    "failed to confirm running image",
    "failed to persist ota boot confirmation",
    "failed to schedule ota state retry",
    "local boot confirmation did not complete",
    "guru meditation error",
    "assert failed",
    "abort() was called",
    "panic'ed",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture and validate the first RodakOS boot after a merged-image flash."
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    parser.add_argument("--log", type=Path, required=True)
    return parser.parse_args()


def validate_log(text: str) -> tuple[str | None, list[str]]:
    lowered = text.lower()
    for marker in FAILURE_MARKERS:
        if marker in lowered:
            return marker, []

    recovery_marker = "RodakRecovery: Starting immutable SD recovery runtime"
    main_marker = "RodakOS: Starting RodakOS with Board Manager HAL"
    if text.count(recovery_marker) > 1:
        return "Recovery started more than once", []
    if text.count(main_marker) > 1:
        return "main system started more than once", []

    main_start = text.find(main_marker)
    if main_start >= 0:
        after_main = text[main_start + len(main_marker) :]
        if recovery_marker in after_main:
            return "main system returned to Recovery", []
        if "ESP-ROM:" in after_main or "\nrst:" in after_main:
            return "device reset after main-system startup", []

    missing = []
    cursor = 0
    for marker in REQUIRED_MARKERS:
        position = text.find(marker, cursor)
        if position >= 0:
            cursor = position + len(marker)
        elif marker in text:
            return f"boot marker out of order: {marker}", []
        else:
            missing.append(marker)
    return None, missing


def main() -> int:
    args = parse_args()
    if (
        args.timeout <= 0
        or args.settle_seconds < 0
        or args.settle_seconds >= args.timeout
    ):
        print("Settle time must be non-negative and shorter than timeout", file=sys.stderr)
        return 2

    args.log.parent.mkdir(parents=True, exist_ok=True)
    captured = bytearray()
    capture_lock = threading.Lock()
    stop = threading.Event()
    read_errors: list[BaseException] = []

    port = serial.serial_for_url(
        args.port, baudrate=args.baud, timeout=0.05, do_not_open=True
    )
    port.dtr = False
    port.rts = True

    with args.log.open("wb") as log:
        try:
            port.open()
            port.dtr = False
            port.rts = True
            time.sleep(0.2)
            port.reset_input_buffer()

            def read_serial() -> None:
                try:
                    while not stop.is_set():
                        data = port.read(port.in_waiting or 1)
                        if not data:
                            continue
                        with capture_lock:
                            captured.extend(data)
                        log.write(data)
                        log.flush()
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                except BaseException as error:
                    read_errors.append(error)
                    stop.set()

            reader = threading.Thread(target=read_serial, name="serial-reader", daemon=True)
            reader.start()
            time.sleep(0.05)
            port.rts = False

            deadline = time.monotonic() + args.timeout
            success_seen_at: float | None = None
            result = 5
            timed_out_missing: list[str] = []
            while time.monotonic() < deadline and not stop.is_set():
                if read_errors:
                    print(f"\nSerial capture failed: {read_errors[0]}", file=sys.stderr)
                    result = 4
                    break
                with capture_lock:
                    text = bytes(captured).decode("utf-8", errors="replace")
                failure, missing = validate_log(text)
                if failure is not None:
                    print(f"\nFirst boot failed: {failure}", file=sys.stderr)
                    result = 3
                    break
                if not missing:
                    if success_seen_at is None:
                        success_seen_at = time.monotonic()
                    elif time.monotonic() - success_seen_at >= args.settle_seconds:
                        result = 0
                        break
                time.sleep(0.05)
            else:
                with capture_lock:
                    text = bytes(captured).decode("utf-8", errors="replace")
                failure, timed_out_missing = validate_log(text)
                if failure is not None:
                    print(f"\nFirst boot failed: {failure}", file=sys.stderr)
                    result = 3

            stop.set()
            if hasattr(port, "cancel_read"):
                port.cancel_read()
            reader.join()
            if read_errors:
                print(f"\nSerial capture failed: {read_errors[0]}", file=sys.stderr)
                result = 4
            elif result == 5:
                if timed_out_missing:
                    print("\nFirst boot timed out; missing markers:", file=sys.stderr)
                    for marker in timed_out_missing:
                        print(f"  - {marker}", file=sys.stderr)
                else:
                    print(
                        "\nFirst boot markers arrived, but the stability window did not complete",
                        file=sys.stderr,
                    )
            return result
        except KeyboardInterrupt:
            return 130
        except BaseException as error:
            print(f"Serial capture failed: {error}", file=sys.stderr)
            return 4
        finally:
            stop.set()
            if "reader" in locals():
                if hasattr(port, "cancel_read"):
                    port.cancel_read()
                reader.join()
            if port.is_open:
                port.rts = False
                port.dtr = False
                port.close()


if __name__ == "__main__":
    raise SystemExit(main())
