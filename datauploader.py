import asyncio
import random
import struct
import time
import websockets

WS_URL = "ws://13.58.232.73:8000/api/ws/send"

# struct pi_to_server:
# uint32_t timestamp; uint8_t id; uint8_t length; uint8_t bytes[8];
# Packed, little-endian -> "<I B B 8s" = 14 bytes
FMT = "<IBB8s"
assert struct.calcsize(FMT) == 14

def now_ms() -> int:
    return int(time.monotonic() * 1000) & 0xFFFFFFFF

def make_packet() -> bytes:
    ts = now_ms()
    msg_id = random.randint(0, 255)

    length = random.randint(0, 8)
    data = bytearray(8)
    for i in range(length):
        data[i] = random.randint(0, 255)

    return struct.pack(FMT, ts, msg_id, length, bytes(data))

async def main():
    print(f"Connecting to {WS_URL}")
    async with websockets.connect(WS_URL, ping_interval=20, ping_timeout=20) as ws:
        print("Connected. Sending fake packets (Ctrl+C to stop).")
        while True:
            pkt = make_packet()
            await ws.send(pkt)  # sends as binary frame
            await asyncio.sleep(0.05)  # 20 Hz. change as needed

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")
