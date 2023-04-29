# Swift-CANyonero

A Swift package for the CANyonero CANbus/ENET adapter protocol.

## Hardware

This adapter is currently in development. It's an ESP32-C3 based CANbus/ENET adapter
especially suited for ECU reprogramming.

## Software

The hardware runs the proprietary CANyonerOS, an operating system based
on FreeRTOS implementing the CANyonero communication protocol.

## PDU Examples

### Open Channel

`1F 30 0005 00 0007A120` ­– Open ISOTP channel w/ Bitrate 500000.

### Send Periodic Message

`1F 35 000E 02 000007E0 00 000007E8 00 023E80` ­– Send every 1 second `023E80` (UDS Tester Present, don't send response) to 0x7E0.
