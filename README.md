# CECS 460 Lab 3: ESP32 Core Timing and Task Interference

## Overview

In this lab, you will investigate how task placement and heavy background work can affect timing stability on a multicore embedded system.

The ESP32 has two CPU cores. This makes it possible to separate time-sensitive work from heavier background work, but multicore systems do not automatically guarantee better timing. Poor task placement, shared-resource contention, and CPU-heavy code can still cause jitter, latency, or missed timing deadlines.

This lab focuses on a simple producer-style timing task and a heavy background task.

## Learning Goals

By the end of this lab, you should be able to:

- Explain why timing-critical embedded tasks need predictable scheduling.
- Describe how heavy background work can interfere with timing-sensitive code.
- Use ESP32 core placement to separate tasks across cores.
- Recognize symptoms such as jitter, missed deadlines, and unstable timing.
- Explain why multicore systems improve concurrency but also add scheduling and debugging complexity.

## Hardware Required

- ESP32 development board
- 1 LED
- 1 current-limiting resistor, such as 220 ohms
- 1 pushbutton
- Breadboard
- Jumper wires

## Wiring

### LED

Connect the LED to GPIO 2:

```text
GPIO 2 -> resistor -> LED anode (+)
LED cathode (-) -> GND
