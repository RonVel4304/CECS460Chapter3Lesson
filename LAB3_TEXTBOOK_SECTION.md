# Lab 3 - CPU Architecture, Core Placement, and Timing Stability on the ESP32

## Overview

This lab aligns with **Chapter 3 - CPU Architectures and Core Comparisons** by using a real ESP32 timing problem instead of a purely theoretical benchmark. The revised chapter asks you to think like an embedded-system designer:

> What does this architecture choice make easier, and what does it make harder?

In this lab, the architecture choice is **where timing-critical work and heavy background work run** on an ESP32. The provided firmware creates two FreeRTOS tasks:

- a **timing-critical blink task** that toggles an LED at a fixed interval and measures timing jitter;
- a **heavy load task** that performs repeated computation and can interfere with the timing-critical task if it is placed poorly or allowed to hog CPU time.

The starting code intentionally contains two problems:

1. The heavy load task is pinned to the **same core** as the timing-critical blink task.
2. The heavy load task performs a long loop without yielding after each burst of work.

Your job is to fix those problems, run the timing test, and explain the result using Chapter 3 concepts: **ISA vs. microarchitecture, RISC vs. CISC, modified Harvard memory organization, single-core vs. multicore design, scheduling, contention, jitter, missed deadlines, and workload isolation**.

The goal is not to prove that one core is always better than another. The goal is to observe that architecture and software placement affect timing behavior in real embedded systems.

---

## 3.9.1 Objectives

By the end of this lab you will be able to:

1. Distinguish between **ISA** and **microarchitecture** using the ESP32 as an example.
2. Explain why ESP32-class devices use **embedded RISC-style cores** without claiming that RISC automatically means highest raw performance.
3. Describe the ESP32's **modified Harvard-style memory organization** at a high level.
4. Explain why timing behavior depends on core placement, scheduling, memory behavior, task priority, and workload behavior.
5. Use FreeRTOS task pinning to separate timing-critical work from heavier background work on a dual-core ESP32.
6. Interpret telemetry fields such as `blink_count`, `max_jitter_us`, `missed_deadlines`, `blink_task_core`, `load_task_core`, and `status`.
7. Explain how long-running background work can cause **jitter** or **missed deadlines** even when the average workload seems reasonable.
8. Decide when a **single-core**, **dual-core**, or **heterogeneous** SoC is the better engineering choice.
9. Connect processor-family choices, including MSP430, Arm Cortex-M, ESP32 Xtensa/RISC-V, Arm Cortex-A/R, and x86, to power, determinism, compatibility, and software ecosystem constraints.

---

## 3.9.2 Hardware and Software Requirements

| Item | Requirement |
|------|-------------|
| Board | ESP32 development board, preferably a classic dual-core ESP32 DevKit |
| Framework | Arduino-ESP32 using Arduino IDE or PlatformIO |
| Main sketch | `CECS460_Lab3_AES.ino` |
| Helper files | `ClassroomClient.cpp`, `ClassroomClient.h`, `LabConfig.h` |
| Libraries | `PubSubClient` and `ArduinoJson` |
| Serial Monitor | 115200 baud |
| Network | Classroom WiFi/MQTT server, if using live submission |
| LED | GPIO 2 -> 220 ohm resistor -> LED anode; LED cathode -> GND |
| Button | One side -> GPIO 4; other side -> GND |

The button uses `INPUT_PULLUP`:

| Button state | GPIO reading |
|--------------|--------------|
| Not pressed | HIGH |
| Pressed | LOW |

> **Board note:** The provided task-placement fix assumes a dual-core ESP32 with cores 0 and 1. Classic ESP32 boards usually fit this model. ESP32-C3 boards are single-core RISC-V and cannot run the same dual-core pinning experiment directly. ESP32-C6-class devices include different RISC-V cores intended for different roles and should not be treated as ordinary symmetric dual-core ESP32 boards. If your board is not a classic dual-core ESP32, complete the code sections that apply and answer the multicore questions conceptually.

---

## 3.9.3 Background Connection to Chapter 3

This lab is built around five Chapter 3 ideas.

### ISA vs. Microarchitecture

The **instruction set architecture (ISA)** is the software-visible contract. It defines the instructions, registers, memory behavior, interrupt model, and toolchain expectations that software can rely on.

The **microarchitecture** is the hardware strategy used to implement that ISA. It includes pipeline structure, cache behavior, clock frequency, interrupt latency, memory buses, and power-management behavior.

The ESP32 is useful because it reminds us that an ISA label does not fully explain observed behavior. A timing problem in this lab is not solved by saying “Xtensa is RISC-style” or “RISC is fast.” The timing behavior depends on the **implementation**, the **FreeRTOS scheduler**, the **core assignment**, task priorities, and how often a long-running task yields.

### RISC vs. CISC

Chapter 3 warns against the misconception that **RISC is always faster than CISC**. Embedded RISC-style cores are often attractive because they can be compact, power-efficient, predictable, and easy to integrate with peripherals. Desktop-class x86 processors can still be much faster in raw wall-clock performance because they use high clock speeds, large caches, branch prediction, out-of-order execution, and wide execution pipelines.

A better engineering question is:

> Which processor meets the timing requirement with acceptable power, cost, software risk, and design complexity?

### Modified Harvard Memory Organization

A simple **von Neumann** model uses one shared path for instructions and data. A pure **Harvard** model separates instruction and data memory. Real SoCs often use a **modified Harvard** approach: separate instruction and data paths near the core, caches or fast internal memory regions, and a programming model that still feels mostly unified to C code.

On ESP32-class devices, timing can be affected by instruction fetch, cache behavior, internal memory, flash access, and shared memory traffic. This lab does not require you to rewrite linker scripts or place functions in IRAM. Instead, you should recognize that the CPU core is not isolated from the surrounding SoC. Timing depends on the core, the scheduler, and the memory system.

### Single-Core, Multicore, and Workload Isolation

A single-core system is often simpler, lower power, easier to debug, and easier to reason about. A multicore system can improve responsiveness when tasks are mostly independent, but it also introduces synchronization, shared-data bugs, memory contention, scheduling surprises, and jitter.

This lab demonstrates a practical multicore lesson:

> Multicore can improve responsiveness when timing-critical work and heavy background work are placed carefully.

The correct lesson is **not** “core 1 is always better than core 0.” The correct lesson is that workload placement changes timing behavior.

### Jitter and Missed Deadlines

The blink task is designed to run periodically. It uses `vTaskDelayUntil()` to request a fixed timing interval. During the test, it compares the actual elapsed time with the target interval and records:

- **jitter**, meaning how far the actual timing moved away from the target timing;
- **missed deadlines**, meaning the jitter exceeded the configured failure threshold;
- **pass/fail status**, based on whether the measured timing stayed within the allowed limit.

This is the key embedded-systems idea: average speed is not enough. For timing-critical systems, consistency matters.

---

## 3.9.4 What You Will Do

You will complete four activities:

1. **Inspect and classify the ESP32 architecture** used in the lab.
2. **Run the provided timing test with the intentional bug** and record the result.
3. **Fix the task placement and yielding behavior**, rerun the timing test, and compare the result.
4. **Explain the results using Chapter 3 tradeoffs** instead of simply saying that the code passed or failed.

The central lab takeaway is:

> Architecture affects responsiveness, not just peak speed. Poor task placement and long-running background work can make a system miss deadlines even when the CPU appears fast enough.

---

## 3.9.5 Part 1 - Inspect the Provided Firmware and Classify the System

Open the provided files:

- `CECS460_Lab3_AES.ino`
- `ClassroomClient.cpp`
- `ClassroomClient.h`
- `LabConfig.h`

Most of your work is in `CECS460_Lab3_AES.ino`. The helper files handle WiFi, MQTT, classroom slot/token assignment, student URL printing, and answer publishing.

In `LabConfig.h`, confirm the hardware and test settings:

| Setting | Meaning |
|---------|---------|
| `LED_PIN = 2` | LED output pin |
| `BUTTON_PIN = 4` | Pushbutton input pin |
| `LED_PERIOD_MS = 250` | Target blink-task period |
| `TEST_DURATION_MS = 10000` | Test duration, in milliseconds |
| `JITTER_FAIL_US = 20000` | Jitter threshold for a missed deadline |

Record your board information:

| Observation | Your value |
|-------------|------------|
| Chip/board model | ________ |
| CPU frequency, if known | ________ |
| Number of cores | ________ |
| Core family, such as Xtensa or RISC-V | ________ |
| Development framework/version | ________ |

Answer these questions:

1. Is your board best described as a **microcontroller-class SoC** or an **application processor**?
2. Which parts of your answer describe the **ISA or core family**, and which describe the **specific implementation or SoC**?
3. Is this board closer to the embedded RISC-style examples from Chapter 3, or to desktop/server x86 systems?
4. What products would this kind of ESP32-class device be appropriate for?
5. What products would this kind of device be a poor fit for?

---

## 3.9.6 Part 2 - Run the Buggy Timing Test

Wire the LED and button as described in the requirements section. Install the required Arduino libraries if needed:

- `PubSubClient`
- `ArduinoJson`

Upload the starting version of the sketch. In the student TODO area, notice the intentional bug:

```cpp
const int BLINK_TASK_CORE = 0;
const int LOAD_TASK_CORE = 0;   // BUG: change this to 1
```

This means both tasks start on core 0:

- `BlinkTask` is timing-critical and has priority 2.
- `LoadTask` is background load and has priority 1.

Even though the blink task has a higher priority, the load task performs a large compute loop repeatedly. In the buggy version, the heavy background task is placed where it can interfere with the timing-critical task.

Open the Serial Monitor at **115200 baud**. Wait for the system to print that it is ready. Press the button to start the timing test.

The test runs for `TEST_DURATION_MS`, which is 10 seconds in the provided configuration.

At the end, record the printed results:

| Measurement | Buggy version result |
|-------------|----------------------|
| `blink_count` | ________ |
| `max_jitter_us` | ________ |
| `missed_deadlines` | ________ |
| `button_press_count` | ________ |
| `blink_task_core` | ________ |
| `load_task_core` | ________ |
| `status` | ________ |

The firmware also builds a classroom telemetry string similar to this:

```text
[timing:student_id=... produced=... consumed=... missed=... backlog=0 producer_core=... consumer_core=... max_jitter_us=... status=...]
```

For this specific sketch, `produced` and `consumed` both map to the blink count, while `missed` maps to missed deadlines. `backlog` is reported as 0 because this version does not use an actual queue. The producer/consumer labels are used to match the classroom telemetry format and the Chapter 3 discussion of task interaction.

### Buggy-version questions

1. Did the starting version pass or fail?
2. What was the maximum measured jitter?
3. How many missed deadlines were recorded?
4. Which cores did the blink task and load task actually run on?
5. Why can a background task interfere with a timing-critical task even when the timing-critical task has higher priority?
6. Why is this an architecture and scheduling problem, not just a “slow code” problem?

---

## 3.9.7 Part 3 - Fix the Lab

Make both required fixes.

### Fix 1: move the heavy load task to the other core

Change this:

```cpp
const int BLINK_TASK_CORE = 0;
const int LOAD_TASK_CORE = 0;   // BUG: change this to 1
```

To this:

```cpp
const int BLINK_TASK_CORE = 0;
const int LOAD_TASK_CORE = 1;
```

This separates the timing-critical blink task from the heavy background load task on a dual-core ESP32.

### Fix 2: add a small yield/delay after the heavy loop

In `loadTask()`, find this section:

```cpp
for (uint32_t i = 0; i < 500000; i++) {
  dummy += (i ^ 0x55AA55AA);
}

// TODO 2:
// Add a small delay/yield here so the heavy task does not hog CPU time.
// Example fix:
// vTaskDelay(1);
```

Add the delay after the loop:

```cpp
for (uint32_t i = 0; i < 500000; i++) {
  dummy += (i ^ 0x55AA55AA);
}

vTaskDelay(1);
```

This gives the scheduler a chance to run other work and prevents the load task from behaving like a constant CPU hog.

Upload the fixed version, open the Serial Monitor, and press the button to rerun the timing test.

Record the fixed-version results:

| Measurement | Fixed version result |
|-------------|----------------------|
| `blink_count` | ________ |
| `max_jitter_us` | ________ |
| `missed_deadlines` | ________ |
| `button_press_count` | ________ |
| `blink_task_core` | ________ |
| `load_task_core` | ________ |
| `status` | ________ |

Now compare both runs:

| Metric | Buggy version | Fixed version | Improved? |
|--------|---------------|---------------|-----------|
| Blink task core | ________ | ________ | ________ |
| Load task core | ________ | ________ | ________ |
| Maximum jitter | ________ | ________ | ________ |
| Missed deadlines | ________ | ________ | ________ |
| Status | ________ | ________ | ________ |

### Fixed-version questions

1. Did moving the load task to the other core improve timing stability?
2. Did adding `vTaskDelay(1)` improve scheduler behavior? Explain.
3. Which fix do you think mattered more on your board, and why?
4. Why is the correct lesson not simply “always use core 1 for background work”?
5. What would happen on a single-core board where the load task could not be moved to another core?
6. Why might a real design use queues, mutexes, DMA buffers, or interrupt-safe memory regions instead of only task pinning?

---

## 3.9.8 Part 4 - Connect the Results to Chapter 3 Concepts

Use your measurements to answer the following.

### ISA vs. microarchitecture

1. Which parts of this lab are related to the ISA or core family?
2. Which parts are related to microarchitecture, SoC integration, or runtime behavior?
3. Why would it be incorrect to say, “The code passed because RISC is fast”?

### RISC vs. CISC

1. Why does the ESP32 being RISC-style not automatically make it faster than an x86 processor?
2. Why might the ESP32 still be a better choice than x86 for this lab setup?
3. What matters more for this lab: raw peak performance or predictable timing? Explain.

### Modified Harvard memory organization

1. At a high level, what does it mean for an ESP32-class system to use a modified Harvard-style organization?
2. Why can separate instruction and data paths, caches, internal memory, and flash behavior affect timing?
3. Why is it incorrect to say Harvard always means “two completely separate physical memories visible to the programmer”?

### Single-core vs. multicore

1. Why did separating the blink task and load task help, or why would it be expected to help?
2. What costs appear when moving from single-core to multicore?
3. Why can a single-core design still be better when predictability, power, and simplicity matter most?
4. Why might a dual-core ESP32 be useful for wireless communication plus application logic?
5. Why might a dual-core ESP32 be unnecessary for a tiny sensor node?

---

## 3.9.9 Part 5 - Processor Family Comparison

Using the processor families discussed in Chapter 3, complete the table below.

| Processor family | Best-fit application | Why | Main tradeoff |
|------------------|----------------------|-----|---------------|
| MSP430 | ________ | ________ | ________ |
| Arm Cortex-M | ________ | ________ | ________ |
| ESP32 Xtensa / RISC-V | ________ | ________ | ________ |
| Arm Cortex-R | ________ | ________ | ________ |
| Arm Cortex-A | ________ | ________ | ________ |
| x86 / x86-64 | ________ | ________ | ________ |

Then answer:

1. Why would a battery-powered environmental sensor usually avoid x86?
2. Why might an industrial gateway choose x86 anyway?
3. Why is an ESP32-class SoC a good fit for connected embedded devices?
4. Why is an ESP32-class SoC usually not the right choice for a rich desktop operating-system workload?
5. Why might a single-core MCU be better than a dual-core SoC for a tiny sensor node?
6. Why might a heterogeneous SoC be useful in a system that has both real-time control and a rich user interface?

---

## 3.9.10 Reflection Question

Write a short paragraph answering the following:

> Based on your ESP32 timing results and Chapter 3, explain how CPU architecture affects embedded-system responsiveness. In your answer, distinguish between ISA and microarchitecture, explain why RISC does not automatically mean highest raw performance, describe modified Harvard memory organization at a high level, and explain how task placement and yielding affected jitter or missed deadlines in this lab. Finally, state whether a single-core, dual-core, or heterogeneous design would be better for a battery-powered IoT node that must handle sensor sampling, wireless communication, and light application logic.

A strong answer should include:

- a correct distinction between **ISA** and **microarchitecture**;
- a recognition that **RISC is not automatically faster than CISC**;
- a brief explanation of **modified Harvard architecture**;
- a discussion of **contention, isolation, jitter, or missed deadlines**;
- direct use of your `max_jitter_us`, `missed_deadlines`, and task-core results;
- a justified choice between **single-core**, **dual-core**, or **heterogeneous** based on workload, power, determinism, and software complexity.

---

## 3.9.11 Lab Report Questions

In your lab report, answer the following:

1. **ISA vs. implementation:** Suppose two processors implement the same ISA, but one has a shallow in-order pipeline and the other has a deeper, higher-frequency design with larger caches. Which properties belong to the ISA, and which belong to the microarchitecture?

2. **RISC misconception:** A student says, “RISC is always faster than CISC.” Use Chapter 3 and your ESP32 timing results to explain why that statement is too simplistic.

3. **Timing evidence:** What evidence showed whether the timing-critical blink task was being delayed? Use `max_jitter_us`, `missed_deadlines`, `blink_count`, task-core placement, and `status` in your answer.

4. **Task placement:** Why did placing `BlinkTask` and `LoadTask` on the same core create a worse design? Why can moving them to different cores improve responsiveness?

5. **Scheduler behavior:** Why does adding `vTaskDelay(1)` after the heavy loop help? What does it allow the RTOS scheduler to do?

6. **Memory hierarchy:** Why can memory architecture still matter in a lab that mainly focuses on task placement? Include instruction fetch, data access, cache behavior, internal memory, or flash behavior in your explanation.

7. **Core-count decision:** You are designing a wearable sensor that samples data, encrypts small packets, and sends them over BLE a few times per second. Would you choose a single-core, dual-core, or heterogeneous SoC? Justify your answer using power, determinism, timing requirements, and software complexity.

8. **Contention and isolation:** Explain how a heavy background task can interfere with a timing-critical task even if the two tasks do not appear to do the same work.

9. **Architecture selection:** Compare an ESP32-class SoC, an Arm Cortex-A SoC, and an x86-based system for a smart-home hub. Which would you choose for a low-cost battery-powered node, and which would you choose for a mains-powered gateway? Why?

10. **Debugging multicore systems:** Why can multicore bugs be harder to debug than single-core bugs? Include at least two of the following terms: race condition, deadlock, priority inversion, cache/memory contention, shared buffer, timing jitter, or core affinity.

---

## 3.9.12 Grading

| Component | Points | How graded |
|-----------|--------|------------|
| Part 1 architecture classification | 15 | Correct board/core classification and correct ISA/microarchitecture language |
| Part 2 buggy timing test | 15 | Runs starting version, records complete telemetry, and identifies the intentional placement/yielding problems |
| Part 3 fixed timing test | 25 | Correctly moves the load task, adds `vTaskDelay(1)`, reruns the test, and records complete telemetry |
| Part 4 Chapter 3 analysis | 20 | Explains results using contention, scheduling, task placement, jitter, missed deadlines, and architecture tradeoffs |
| Reflection question | 10 | Clear paragraph using lab data and Chapter 3 vocabulary |
| Lab report questions | 15 | Depth of reasoning and quality of engineering tradeoff analysis |

Total: **100 points**

---

## 3.9.13 Summary

This lab turns Chapter 3 from a reading-only topic into a hands-on architecture exercise. By running and fixing a real ESP32 FreeRTOS timing test, you connect abstract ideas - **ISA vs. microarchitecture, RISC vs. CISC, modified Harvard memory organization, single-core vs. multicore design, task placement, contention, jitter, and missed deadlines** - to observable embedded-system behavior.

The most important lesson is not that multicore is always better. The most important lesson is that processor selection and task placement are engineering tradeoffs. A design is good when it meets timing requirements with acceptable power, cost, complexity, reliability, and software risk.

For this ESP32 lab, the key Chapter 3 takeaway is:

> More cores can improve responsiveness, but only when timing-critical work, background work, and scheduler behavior are coordinated carefully.
