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

port = "/dev/ttyACM0"
baud = 115200

WS_PREFIX = "ws://"
WS_SUFFIX = ":8080/rtk"
ws_ip = "localhost"
ws_url = WS_PREFIX + ws_ip + WS_SUFFIX

ser = Serial(port, baud, timeout=1)
ubx = UBXReader(ser, protfilter=UBX_PROTOCOL)

url = "url"

ws = connect(url)
