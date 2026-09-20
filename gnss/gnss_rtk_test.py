# Filename: gnss_base_station.py
# Author:   Blake Hill

import threading
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

port = "/dev/ttyACM0"
baud = 115200

WS_PREFIX = "ws://"
WS_SUFFIX = ":8080/rtk"
ws_ip = "localhost"
ws_url = WS_PREFIX + ws_ip + WS_SUFFIX

ser = Serial(port, baud, timeout=1)
ubx = UBXReader(ser, protfilter=UBX_PROTOCOL)

ws = connect(ws_url)

def config_ubx():
    config = [
        # Set normal mode
        ("CFG_TMODE_MODE", 0),
        # UBX and RTCM3 protocol over USB
        ("CFG_USBOUTPROT_UBX", 1),  # Enable UBX protocol over USB
        ("CFG_USBOUTPROT_RTCM3X", 1),  # Enable RTCM3 protocol over USB
        # Allox UBX results out over USB
        ("CFG_USBOUTPROT_UBX", 1),
        # Output corrected pos and RTK status
        ("CFG_MSGOUT_UBX_NAV_PVT_USB", 1),
        # Report whether received RTCM messages were accepted
        ("CFG_MSGOUT_UBX_RXM_RTCM_USB", 1),
    ]
    
    command = UBXMessage(SET_LAYER_RAM, TXN_NONE, config)
    ser.write(command.serialize())
    ser.flush()

while True:
    raw, msg = ubx.read()
    
    if msg.identity == 