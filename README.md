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