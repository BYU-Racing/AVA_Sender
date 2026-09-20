# Filename: gnss_base_station.py
# Author:   Blake Hill

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
from websockets.sync.client import connect

# port = "/dev/ttyACM0" # Linux
port = "COM5"  # Windows
baud = 115200

ser = Serial(port, baud, timeout=1)
ubx = UBXReader(ser, protfilter=RTCM3_PROTOCOL | UBX_PROTOCOL)

WS_PREFIX = "ws://"
WS_SUFFIX = ":8080/rtk"
ws_ip = "localhost"
ws_url = WS_PREFIX + ws_ip + WS_SUFFIX


# Configure the F9P for survey-in mode to find base station location
def start_survey_in():
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
        ("CFG_TMODE_SVIN_MIN_DUR", 300),  # Minimum survey-in duration (seconds)
        ("CFG_TMODE_SVIN_ACC_LIMIT", 20000),  # Accuracy limit, 5m (0.1 mm units)
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
def check_survey_in() -> bool:
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


# Main Code starts
start_survey_in()

while not check_survey_in():
    pass

with connect(ws_url) as ws:
    print(f"Connected to WS at {ws_url}", flush=True)
    while True:  # Main loop
        raw, msg = ubx.read()

        if msg is None:
            print("No GNSS message received (read timeout)", flush=True)
            continue

        if protocol(raw) == RTCM3_PROTOCOL:
            ws.send(raw)
            print(
                f"RTCM3 sent: {msg.identity}, length: {len(raw)} bytes",
                flush=True,
            )
