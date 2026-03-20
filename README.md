# NCU-SW
NCU Software

## Features

### Dual Core Setup

The STM32H745 has a dual-core architecture, consisting of an ARM Cortex M7 core alongside an M4 core.
The M7 core is more powerful and runs at a higher clock speed, but has L1 cache which reduces determinism, making it less suitable for real-time applications.
Therefore, the cores are assigned the following roles:

- M4 Core: Realtime core, running application code and handling realtime interfaces such as CAN, ADCs and GPIO.
- M7 Core: System core, running the network stack for Ethernet, WiFi, 4G and running an XCP over IP server alongside a web server for telemetry and diagnostics.


