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

def make_packet(msg_id=0) -> bytes:
    ts = now_ms()

    length = 8
    data = bytearray(8)
    match msg_id:
        case 0:
            data[0] = random.randint(0, 1)
        case 1 | 2 | 3:
            data[0] = random.randint(0, 255)
            data[1] = random.randint(0, 255)
        case 4:
            data[0] = random.randint(0, 5)
            data[1] = random.randint(0, 255)
        case 5:
            data[0] = random.randint(0, 3)
            for i in range(1, 4):
                data[i] = random.randint(0, 255)
        case 6:
            data[0] = random.randint(0, 3)
            for i in range(1, 6):
                data[i] = random.randint(0, 255)
        case 7: 
            data[0] = random.randint(0, 100)
        case 8:
            data[0] = random.randint(0, 150)
        case 9: # GPS
            for i in range(0, 7):
                data[i] = random.randint(0, 255)
        case 10:
            data[0] = random.randint(0, 4)

    # for i in range(length):
    #     data[i] = random.randint(0+20, 255-200)

    return struct.pack(FMT, ts, msg_id, length, bytes(data))

async def main():
    msg_id = 0
    print(f"Connecting to {WS_URL}")
    async with websockets.connect(WS_URL, ping_interval=20, ping_timeout=20) as ws:
        print("Connected. Sending fake packets (Ctrl+C to stop).")
        while True:
            pkt = make_packet(msg_id)
            await ws.send(pkt)  # sends as binary frame
            await asyncio.sleep(0.05)  # 20 Hz. change as needed
            msg_id = msg_id + 1 if msg_id < 10 else 0

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")
