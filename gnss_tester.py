# Filename: gnss_tester.py
# Author: Blake Hill
# Desc: Reads and parses data from GNSS rec chip over serial conn
#       Uses NMEA protocol for easy debugging, it's easily readable

from pyubx2 import NMEA_PROTOCOL, UBXReader
from serial import Serial

port = "/dev/ttyACM0"
baud = 115200

ser = Serial(port, baud, timeout=1)
ubx = UBXReader(ser, protfilter=NMEA_PROTOCOL)


def read_sat_data():
    raw, msg = ubx.read()

    if msg is None:
        print("No GNSS message received (read timeout)", flush=True)

    elif msg.msgID == "GLL" and msg.status == "A":  # Geographic position - lat/lon
        print(f"1-GLL Time: {msg.time} | Lat: {msg.lat}, Lon: {msg.lon}")

    elif msg.msgID == "GGA":  # GNSS fix data
        print(
            f"2-GGA Time: {msg.time} | Lat: {msg.lat}, Lon: {msg.lon} | "
            f"Num Sat: {msg.numSV}, Alti: {msg.alt}, Fix: {msg.quality}"
        )
    # quality:  fix type—commonly 0 invalid, 1 standalone,
    #           2 differential, 4 RTK fixed, 5 RTK float.

    # elif msg.msgID == "GSV":  # Diagnostics on GNSS satellites in view
    #     print(f"3-GSV Num Sat: {msg.numSV}")
    #     for i in range(1, msg.numSV + 1):
    #         print(
    #             f"   Sat {i}: PRN: {msg.svPRN[i - 1]}, Elev: {msg.elevation[i - 1]}, "
    #             f"Azim: {msg.azimuth[i - 1]}, C/N0: {msg.cn0[i - 1]}"
    #         )

    elif msg.msgID == "RMC" and msg.status == "A":  # Speed and travel direction
        speed_kmh = float(msg.spd) * 1.852
        print(
            f"4-RMC Time: {msg.time} | Lat: {msg.lat}, Lon: {msg.lon} | "
            f"Speed (km/h): {speed_kmh} | N/S?: {msg.NS}, E/W?: {msg.EW}, "
            f"Deg: {msg.cog}"
        )

    # print("Raw message: ", raw, flush=True)


while True:
    read_sat_data()
