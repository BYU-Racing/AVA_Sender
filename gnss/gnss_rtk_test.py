# Filename: gnss_rtk_test.py
# Author:   Blake Hill

import threading

from pyubx2 import (
    SET_LAYER_RAM,
    TXN_NONE,
    UBX_PROTOCOL,
    UBXMessage,
    UBXReader,
)
from serial import Serial
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect

port = "/dev/ttyACM0"
baud = 115200

WS_PREFIX = "ws://"
WS_SUFFIX = ":8080/rtk"
ws_ip = "localhost"
ws_url = WS_PREFIX + ws_ip + WS_SUFFIX

ser = Serial(port, baud, timeout=1)
ubx = UBXReader(ser, protfilter=UBX_PROTOCOL)

stop_event = threading.Event()


def config_ubx():
    config = [
        # Set normal mode
        ("CFG_TMODE_MODE", 0),
        # UBX and RTCM3 protocol over USB
        ("CFG_USBINPROT_UBX", 1),  # Enable UBX protocol over USB
        ("CFG_USBINPROT_RTCM3X", 1),  # Enable RTCM3 protocol over USB
        # Allox UBX results out over USB
        ("CFG_USBOUTPROT_UBX", 1),
        # Output corrected pos and RTK status
        ("CFG_MSGOUT_UBX_NAV_PVT_USB", 1),
        # Report whether received RTCM messages were accepted
        ("CFG_MSGOUT_UBX_RXM_RTCM_USB", 1),
    ]

    command = UBXMessage.config_set(SET_LAYER_RAM, TXN_NONE, config)
    ser.write(command.serialize())
    ser.flush()


# Called in a separate thread
def read_from_ubx(ubx: UBXReader, stop_event: threading.Event) -> None:
    # carrSoln = carrier-range solution status
    solution_status = {0: "No RTK", 1: "RTK float", 2: "RTK fixed"}
    fix_types = {
        0: "No fix",
        1: "Dead reckoning",
        2: "2D fix",
        3: "3D fix",
        4: "GNSS + dead reckoning",
        5: "Time-only fix",
    }
    while not stop_event.is_set():
        raw, msg = ubx.read()

        if msg is None:
            continue

        if msg.identity == "NAV-PVT":
            solu = solution_status.get(msg.carrSoln, f"Unknown ({msg.carrSoln})")
            fix = fix_types.get(msg.fixType, f"Unknown ({msg.fixType})")
            pos_info = (
                f"Lat: {msg.lat} | Lon: {msg.lon} | Acc: {msg.hAcc} | "
                f"Elev: {msg.hMSL / 1000}m\n"
                if not msg.invalidLlh
                else "Pos Invalid\n"
            )
            dir_info = f"Speed: {msg.gSpeed / 1000}m/s | Heading: {msg.headMot}deg\n\n"
            print(
                f"{solu} | DiffCorrections: {bool(msg.diffSoln)} | "
                f"FixOK: {msg.gnssFixOk} | Fix Type: {fix} | NumSat: {msg.numSV}\n"
                + pos_info
                + dir_info,
                flush=True,
            )
        elif msg.identity == "RXM-RTCM":
            rtcm_usage = {
                0: "Unknown",
                1: "Not used",
                2: "Used successfully",
            }
            usage = rtcm_usage.get(msg.msgUsed, f"Unknown ({msg.msgUsed})")
            print(
                f"RTCM recv from read: type={msg.msgType}, "
                f"used={usage}, CRC fail?={bool(msg.crcFailed)}",
                flush=True,
            )


def main() -> None:
    config_ubx()
    reader_thread = threading.Thread(
        target=read_from_ubx, args=(ubx, stop_event), daemon=True
    )
    reader_thread.start()
    try:
        while not stop_event.is_set():
            try:
                with connect(ws_url) as ws:
                    print("Connected to Base Station WS")
                    for data in ws:
                        if stop_event.is_set():
                            break

                        if not isinstance(data, bytes):
                            print("Ignoring non-binary WS msg")
                            continue

                        ser.write(data)

            except ConnectionClosed as err:
                print(f"WS Disconnected: {err}")
            except OSError as err:
                print(f"Couldn't connect: {err}")

            if not stop_event.is_set():
                print("Reconnecting to WS in 1s")
                stop_event.wait(1)
                continue
    finally:
        print("Closing WS")
        stop_event.set()
        reader_thread.join(timeout=2)
        ser.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        stop_event.set()
        print("\nStopped by Ctrl+C")
