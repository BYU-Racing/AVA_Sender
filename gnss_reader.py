from pyubx2 import UBXReader
from serial import Serial

port = "/dev/ttyACM0"
baud = 115200

stream = Serial(port, baud, timeout=1)
ubr = UBXReader(stream)

while True:
    raw, msg = ubr.read()

    if msg is None:
        continue

    if msg.identity == "NAV-PVT":
        print(
            f"Lat: {msg.lat} | Lon: {msg.lon} | Satellites: {msg.numSV}",
            flush=True,
        )
