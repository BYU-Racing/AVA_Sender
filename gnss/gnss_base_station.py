# Filename: gnss_base_station.py
# Author:   Blake Hill

<<<<<<< HEAD
import argparse
import threading
from http import HTTPStatus
=======

import os
import threading
import time
from queue import Empty, Full, Queue
>>>>>>> d34a913affabf15e0b2cd4c668b49778b915cb89

from pyubx2 import (
    RTCM3_PROTOCOL,
    SET_LAYER_RAM,
    TXN_NONE,
    UBX_PROTOCOL,
    UBXMessage,
    UBXReader,
    protocol,
)
from serial import Serial
from websockets.exceptions import ConnectionClosed
from websockets.sync.server import serve

# port = "/dev/ttyACM0" # Linux
port = "COM6"  # Windows
baud = 115200

class RTKServer:
    """Broadcast live RTCM frames to currently connected rovers."""

<<<<<<< HEAD
    def __init__(self):
        self.clients = set()
        self.lock = threading.Lock()

    @staticmethod
    def process_request(connection, request):
        if request.path != "/rtk":
            return connection.respond(HTTPStatus.NOT_FOUND, "Use /rtk\n")

    def handle_rover(self, ws):
        address = ws.remote_address
        with self.lock:
            self.clients.add(ws)
        print(f"Rover connected: {address}", flush=True)
        try:
            # Receiving keeps the handler alive and detects disconnects.
            # Corrections are sent by broadcast() from the serial reader.
            for _ in ws:
                pass
        except ConnectionClosed:
            pass
        finally:
            with self.lock:
                self.clients.discard(ws)
            print(f"Rover disconnected: {address}", flush=True)

    def broadcast(self, raw):
        with self.lock:
            clients = tuple(self.clients)
        sent = 0
        for ws in clients:
            try:
                ws.send(raw)
                sent += 1
            except ConnectionClosed:
                with self.lock:
                    self.clients.discard(ws)
        return sent
=======
WS_HOST = os.getenv("WS_BIND_HOST", "0.0.0.0")
WS_PORT = int(os.getenv("WS_PORT", "8081"))
SURV_MIN_TIME_SEC = 10
ACC_LIM_TENTH_MM = 25000

WS_PREFIX = "ws://"
WS_SUFFIX = ":8081/rtk"
ws_ip = "localhost"

client_lock = threading.Lock()
client_queue = None
stop_event = threading.Event()


def handle_client(ws):
    global client_queue

    if ws.request.path != "/rtk":
        ws.close(code=1008, reason="Use /rtk")
        return

    corrections = Queue(maxsize=32)

    with client_lock:
        client_spot_free = client_queue is None
        if client_spot_free:
            client_queue = corrections

    if not client_spot_free:  # Rover already connected
        ws.close(code=1008, reason="A rover is already connected")
        return

    print(f"Rover connected: {ws.remote_address}", flush=True)

    try:
        while not stop_event.is_set():
            try:
                time_created, raw = corrections.get(block=True, timeout=0.5)
            except Empty:
                try:
                    ws.recv(timeout=0)
                except TimeoutError:
                    pass
                continue

            if time.monotonic() - time_created > 1:
                continue

            ws.send(raw)

    except ConnectionClosed:
        pass
    finally:
        with client_lock:
            client_queue = None
        print("Rover disconnected; waiting for a connection", flush=True)
>>>>>>> d34a913affabf15e0b2cd4c668b49778b915cb89


# Configure the F9P for survey-in mode to find base station location
def start_survey_in(ser):
    config = [
        # Set survey-in (Base Station) mode on
        ("CFG_TMODE_MODE", 1),
        # UBX and RTCM3 protocol over USB
        ("CFG_USBOUTPROT_UBX", 1),  # Enable UBX protocol over USB
        ("CFG_USBOUTPROT_RTCM3X", 1),  # Enable RTCM3 protocol over USB
        # Survey Status output to USB
        ("CFG_MSGOUT_UBX_NAV_SVIN_USB", 1),  # Enable NAV-SVIN message output
        # Survey-in mode configuration
        ## Quick test, 60 50000. Temp testing, 300 20000. Longer-term 18000 10000.
        (
            "CFG_TMODE_SVIN_MIN_DUR",
            SURV_MIN_TIME_SEC,
        ),  # Minimum survey-in duration (seconds)
        (
            "CFG_TMODE_SVIN_ACC_LIMIT",
            ACC_LIM_TENTH_MM,
        ),  # Accuracy limit, 5m (0.1 mm units)
        # Output RTCM3 messages over USB
        ("CFG_MSGOUT_RTCM_3X_TYPE1005_USB", 1),
        ("CFG_MSGOUT_RTCM_3X_TYPE1074_USB", 1),  # GPS MSM4
        ("CFG_MSGOUT_RTCM_3X_TYPE1084_USB", 1),  # GLONASS MSM4
        ("CFG_MSGOUT_RTCM_3X_TYPE1094_USB", 1),  # Galileo MSM4
        ("CFG_MSGOUT_RTCM_3X_TYPE1124_USB", 1),  # BeiDou MSM4
        ("CFG_MSGOUT_RTCM_3X_TYPE1230_USB", 1),  # GLONASS code-phase biases
    ]

    command = UBXMessage.config_set(SET_LAYER_RAM, TXN_NONE, config)
    ser.write(command.serialize())
    ser.flush()


# Check NAV-SVIN message to see if survey-in is complete
def check_survey_in(ubx) -> bool:
    raw, msg = ubx.read()

    if msg is None:
        print("No GNSS message received (read timeout)", flush=True)

    elif msg.identity in ("ACK-ACK", "ACK-NAK"):
        print(msg, flush=True)

    elif msg.identity == "NAV-SVIN":
        print(
            f"NAV-SVIN: Dur: {msg.dur}s, Est. Acc: {msg.meanAcc / 10000:.3f}m, "
            f"Num Observations: {msg.obs}, Active: {msg.active}, Valid: {msg.valid}"
        )

        if msg.valid == 1 and msg.active == 0:
            print(
                f"Survey-in complete in {msg.dur}s, "
                f"Mean Position: Lat {msg.meanXHP}, Lon {msg.meanYHP}, Alt {msg.meanZHP}"
            )
            return True

    return False


<<<<<<< HEAD
def main():
    parser = argparse.ArgumentParser(description="Serve base-station RTCM corrections")
    parser.add_argument("--port", default=port, help="Base receiver serial port")
    parser.add_argument("--baud", type=int, default=baud)
    parser.add_argument("--host", default="0.0.0.0", help="WebSocket bind address")
    parser.add_argument("--ws-port", type=int, default=8080)
    args = parser.parse_args()

    relay = RTKServer()
    with Serial(args.port, args.baud, timeout=1) as ser:
        ubx = UBXReader(ser, protfilter=RTCM3_PROTOCOL | UBX_PROTOCOL)
        with serve(
            relay.handle_rover,
            args.host,
            args.ws_port,
            process_request=relay.process_request,
        ) as server:
            server_thread = threading.Thread(target=server.serve_forever, daemon=True)
            server_thread.start()
            print(
                f"RTK server listening on {args.host}:{args.ws_port}/rtk; "
                "rovers may connect while survey-in runs.",
                flush=True,
            )
            try:
                start_survey_in(ser)
                while not check_survey_in(ubx):
                    pass

                while True:
                    raw, msg = ubx.read()
                    if raw and msg is not None and protocol(raw) == RTCM3_PROTOCOL:
                        sent = relay.broadcast(raw)
                        print(
                            f"RTCM3 {msg.identity}: {len(raw)} bytes -> {sent} rover(s)",
                            flush=True,
                        )
            finally:
                server.shutdown()
                server_thread.join(timeout=2)
=======
# Main Code starts
def main():
    try:
        start_survey_in()
        while not check_survey_in():
            pass

        with serve(
            handle_client,
            WS_HOST,
            WS_PORT,
            ping_interval=10,
            ping_timeout=10,
            close_timeout=2,
        ) as server:
            server_thread = threading.Thread(target=server.serve_forever)
            server_thread.start()
            print("RTK server started")

            try:
                while not stop_event.is_set():
                    raw, msg = ubx.read()

                    if msg is None:
                        print("GNSS read timeout", flush=True)
                        continue

                    if protocol(raw) != RTCM3_PROTOCOL:
                        continue

                    # print(
                    #     f"RTCM generated: {msg.identity}, {len(raw)} bytes", flush=True
                    # )

                    with client_lock:
                        corrections = client_queue

                    if corrections is not None:
                        try:
                            corrections.put_nowait((time.monotonic(), raw))
                        except Full:
                            pass  # Drop packet when queue is full

            finally:
                stop_event.set()
                server.shutdown()
                server_thread.join()
    finally:
        stop_event.set()
>>>>>>> d34a913affabf15e0b2cd4c668b49778b915cb89


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
<<<<<<< HEAD
        print("\nStopped by Ctrl+C")
=======
        print("\nStopped by Ctrl+C", flush=True)
    finally:
        ser.close()
>>>>>>> d34a913affabf15e0b2cd4c668b49778b915cb89
