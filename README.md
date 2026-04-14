# NCU-SW
NCU Software

## Features

### Dual Core Setup

The STM32H745 has a dual-core architecture, consisting of an ARM Cortex M7 core alongside an M4 core.
The M7 core is more powerful and runs at a higher clock speed, but has L1 cache which reduces determinism, making it less suitable for real-time applications.
Therefore, the cores are assigned the following roles:

- M4 Core: Realtime core, running application code and handling realtime interfaces such as CAN, ADCs and GPIO.
- M7 Core: System core, running the network stack for Ethernet, WiFi, 4G and running an XCP over IP server alongside a web server for telemetry and diagnostics.

## I/O and Module Allocations

### Timers

- TIM1: LSDs

- TIM4: HSDs

- TIM16: M7 task A
- TIM17: M7 task B

- TIM6: M4 task A
- TIM7: M4 task B
