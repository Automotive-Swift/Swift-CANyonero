# Swift-CANyonero

A Swift package for the CANyonero CANbus/ENET adapter protocol.

## Hardware

This adapter is currently in development. For now, it's an ESP32-C3 based CANbus/ENET adapter
especially suited for ECU reprogramming.

## Software

The hardware runs the proprietary CANyonerOS, an operating system based
on FreeRTOS implementing the CANyonero communication protocol.

## PDU Examples

### Request Information

```1F 10 0000` ­– Request sending device information.

### Open Channel

`1F 30 0005 00 0007A120 02` ­– Open ISOTP channel w/ Bitrate 500000, 0 milliseconds separation time for RX, 2 milliseconds separation time for TX.

### Send Periodic Message

`1F 35 0012 02 000007E0 00 000007E8 FFFFFFFF 00 023E80` ­– Send every 1 second `023E80` (*UDS Tester Present, don't send response*) to 0x7E0.

### Stop Periodic Message

`1F 36 0001 00` ­– Stop periodic message with number 0.
