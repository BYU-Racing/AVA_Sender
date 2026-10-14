# AVA Raspberry Pi Sender

## About

This code goes on the Telemetry Raspberry Pi. It reads data from the CAN lines,
saves it into a log file using `can_dumpy.sh`, and sends it live to the Website
using `data_uploader.cpp`. 

## Usage

Build `data_uploader.cpp` using `./build.sh`. 

Then run `./run.sh` to start `data_uploader.cpp`.

## Code Info

Standard vs Extended Frames
CAN_EFF_FLAG = 0x80000000U for extended frames

CAN Standard Frame Format (SFF)
CAN_SFF_MASK = 0x000007FFU

CAN Extended Frame Format (EFF)
CAN_EFF_MASK = 0x1FFFFFFFU

## Authors

Query the Data and Live Telemetry Team for info on this repo.

## GNSS RTK over WebSocket

`gnss/gnss_base_station.py` hosts a WebSocket server at `/rtk` and sends
binary RTCM corrections directly to connected rovers. No separate relay is needed.
The server starts before survey-in completes; corrections start after a valid survey.
Either script can start first. The rover retries every second and reconnects if the
base restarts. The base keeps reading corrections when no rover is connected and
only sends current frames to newly connected rovers.

On the base computer (the default receiver port is `COM5`):

```sh
uv run gnss/gnss_base_station.py --ws-port 8081
```

On the rover:

```sh
uv run gnss/gnss_rtk_test.py --ws-url ws://100.121.126.33:8081/rtk
```

Replace the IP with the base computer's reachable address. Use `--port` to select
each receiver's serial port, such as `/dev/ttyACM0` on Linux. Both scripts default
to WebSocket port `8080`; these examples use `8081` to avoid the existing service
on `8080`. The base listens on all IPv4 interfaces by default (`--host 0.0.0.0`).

Run hardware-free connection tests with:

```sh
uv run python -m unittest gnss.test_rtk_connection -v
```
