#!/usr/bin/env python3
"""Run a local two-receiver RTK test without modifying gnss_base_station.py."""

from __future__ import annotations

import argparse
import threading

from pyubx2 import (
    NMEA_PROTOCOL,
    RTCM3_PROTOCOL,
    SET_LAYER_RAM,
    TXN_NONE,
    UBX_PROTOCOL,
    UBXMessage,
    UBXReader,
    protocol,
)
from serial import Serial


def send_config(serial_port: Serial, values: list[tuple[str, int]]) -> None:
    message = UBXMessage.config_set(SET_LAYER_RAM, TXN_NONE, values)
    serial_port.write(message.serialize())
    serial_port.flush()


def configure_base(
    serial_port: Serial, survey_seconds: int, accuracy_metres: float
) -> None:
    send_config(
        serial_port,
        [
            ("CFG_USBOUTPROT_UBX", 1),
            ("CFG_USBOUTPROT_RTCM3X", 1),
            ("CFG_MSGOUT_UBX_NAV_SVIN_USB", 1),
            ("CFG_TMODE_SVIN_MIN_DUR", survey_seconds),
            ("CFG_TMODE_SVIN_ACC_LIMIT", round(accuracy_metres * 10_000)),
            ("CFG_TMODE_MODE", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1005_USB", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1074_USB", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1084_USB", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1094_USB", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1124_USB", 1),
            ("CFG_MSGOUT_RTCM_3X_TYPE1230_USB", 1),
        ],
    )


def configure_rover(serial_port: Serial) -> None:
    send_config(
        serial_port,
        [
            ("CFG_USBINPROT_UBX", 1),
            ("CFG_USBINPROT_NMEA", 1),
            ("CFG_USBINPROT_RTCM3X", 1),
            ("CFG_USBOUTPROT_UBX", 1),
            ("CFG_MSGOUT_UBX_NAV_PVT_USB", 1),
        ],
    )


def wait_for_survey(base_reader: UBXReader) -> None:
    print("Waiting for the base survey-in to complete...")
    while True:
        _, message = base_reader.read()
        if message is None or message.identity != "NAV-SVIN":
            continue
        accuracy = message.meanAcc / 10_000
        print(
            f"Survey: {message.dur}s, accuracy {accuracy:.3f}m, "
            f"observations {message.obs}, active={message.active}, valid={message.valid}",
            flush=True,
        )
        if message.valid == 1 and message.active == 0:
            print("Survey complete; forwarding RTCM corrections to the rover.")
            return


def forward_corrections(
    base_reader: UBXReader, rover_serial: Serial, stopped: threading.Event
) -> None:
    try:
        while not stopped.is_set():
            raw, message = base_reader.read()
            if raw and message is not None and protocol(raw) == RTCM3_PROTOCOL:
                rover_serial.write(raw)
                print(
                    f"RTCM {message.identity} -> rover ({len(raw)} bytes)",
                    flush=True,
                )
    except Exception as exc:
        if not stopped.is_set():
            print(f"Base forwarding stopped: {exc}", flush=True)
        stopped.set()


def print_rover_status(rover_reader: UBXReader, stopped: threading.Event) -> None:
    solution_names = {0: "none", 1: "RTK float", 2: "RTK fixed"}
    while not stopped.is_set():
        _, message = rover_reader.read()
        if message is None or message.identity != "NAV-PVT":
            continue
        solution = solution_names.get(message.carrSoln, str(message.carrSoln))
        print(
            f"ROVER: {solution}, fixType={message.fixType}, satellites={message.numSV}, "
            f"lat={message.lat:.8f}, lon={message.lon:.8f}, "
            f"height={message.hMSL / 1000:.3f}m",
            flush=True,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local dual-u-blox RTK test")
    parser.add_argument("--base-port", default="COM5")
    parser.add_argument("--rover-port", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--survey-seconds", type=int, default=300)
    parser.add_argument("--accuracy-m", type=float, default=2.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.base_port.casefold() == args.rover_port.casefold():
        raise SystemExit("Base and rover ports must be different.")

    stopped = threading.Event()
    with (
        Serial(args.base_port, args.baud, timeout=1) as base_serial,
        Serial(args.rover_port, args.baud, timeout=1) as rover_serial,
    ):
        base_reader = UBXReader(
            base_serial, protfilter=RTCM3_PROTOCOL | UBX_PROTOCOL
        )
        rover_reader = UBXReader(
            rover_serial, protfilter=NMEA_PROTOCOL | RTCM3_PROTOCOL | UBX_PROTOCOL
        )

        configure_rover(rover_serial)
        configure_base(base_serial, args.survey_seconds, args.accuracy_m)
        wait_for_survey(base_reader)

        forwarding_thread = threading.Thread(
            target=forward_corrections,
            args=(base_reader, rover_serial, stopped),
            daemon=True,
        )
        forwarding_thread.start()
        try:
            print_rover_status(rover_reader, stopped)
        finally:
            stopped.set()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
