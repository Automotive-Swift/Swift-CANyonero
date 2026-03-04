# ECUconnect SocketCAN Userspace Bridge

This tool exposes ECUconnect's native CANyonero ProtocolMachine transport as a Linux SocketCAN endpoint by bridging:

- `SocketCAN` interface (for local Linux apps like `candump`, `cangen`, `isotpdump`)
- `ECUconnect TCP` endpoint (default `192.168.42.42:129`)

It uses the native CANyonero PDUs (`openChannel`/`openFDChannel`, `setArbitration`, `send`, `received`) and does not require firmware changes.

## Build

```bash
cd Sources/ecuconnect-socketcan
cmake -S . -B build
cmake --build build -j
```

Binary:

```bash
Sources/ecuconnect-socketcan/build/ecos-socketcan-bridge
```

## Quick Start

Create a local virtual CAN interface (if you don't already have one):

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

Run bridge (classic CAN):

```bash
Sources/ecuconnect-socketcan/build/ecos-socketcan-bridge \
  --can-if vcan0 \
  --endpoint 192.168.42.42:129 \
  --bitrate 500000
```

Then use SocketCAN tools normally:

```bash
candump vcan0
cansend vcan0 7E0#02010C0000000000
```

## CAN FD

Use `--fd` to open a CAN FD channel on ECUconnect and forward SocketCAN CAN FD frames:

```bash
Sources/ecuconnect-socketcan/build/ecos-socketcan-bridge \
  --can-if vcan0 \
  --fd \
  --bitrate 500000 \
  --data-bitrate 2000000
```

## Deployment Modes (`vcan` vs `canX`)

### 1) Userland bridge mode (recommended): `vcan`

Use a virtual CAN interface (`vcan0`) when this tool is the source of CAN frames for local Linux applications.
This is the standard and most practical setup for a user-space SocketCAN bridge.

Why:

- No extra hardware driver required
- Works with standard SocketCAN tools (`candump`, `cansend`, `isotpdump`, ...)
- Clean separation between remote ECU transport (TCP) and local CAN consumers

Setup:

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

### 2) Kernel-backed hardware mode: `can0` / `can1` / ...

If you have physical CAN hardware and a Linux kernel CAN driver, you can run the bridge on a real CAN netdev:

```bash
Sources/ecuconnect-socketcan/build/ecos-socketcan-bridge --can-if can0 ...
```

Notes:

- A "real" CAN interface cannot be created purely from user space
- Real interfaces are provided by kernel drivers for specific hardware
- Prefer real `canX` when you need direct physical bus behavior (timing/error states/bus-off handling)

## Important Behavior Notes

- TX CAN IDs from SocketCAN are supported. The bridge updates ECUconnect arbitration request ID as needed.
- By default, RX filtering is disabled (`--reply-pattern 0 --reply-mask 0`) to accept all frames from ECUconnect.
- If ECUconnect disconnects, the bridge reconnects automatically (`--reconnect-ms`).
- `--request-id` can force all outgoing ECU TX to one fixed ID (useful for strict diagnostic sessions).

## Options

```text
--endpoint HOST:PORT      ECUconnect endpoint (default 192.168.42.42:129)
--can-if IFACE            SocketCAN interface (default vcan0)
--bitrate BPS             Nominal bitrate for openChannel/openFDChannel
--data-bitrate BPS        Data bitrate for CAN FD mode
--fd                      Use raw_fd channel
--reconnect-ms MS         Reconnect interval
--connect-timeout-ms MS   TCP connect timeout
--command-timeout-ms MS   Protocol handshake timeout
--rx-stmin-us US          RX STmin for channel open
--tx-stmin-us US          TX STmin for channel open
--request-id ID           Fixed arbitration request ID
--reply-pattern ID        Arbitration reply pattern
--reply-mask MASK         Arbitration reply mask
--recv-own                Receive own CAN frames on bridge socket
--stats-interval SEC      Periodic stats print interval (0 = off)
--verbose                 Verbose logs
```
