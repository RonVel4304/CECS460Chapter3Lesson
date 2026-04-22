# Lab 3 — CPU Architectures and Core Comparisons on the ESP32

## Overview

This lab aligns with **Chapter 3 — CPU Architectures and Core Comparisons** by using the ESP32 as a concrete example of how processor architecture choices affect embedded system behavior. Rather than focusing on hardware acceleration, this lab asks you to observe and reason about the **CPU core architecture, memory organization, and multicore behavior** of a real SoC you can program directly.

The ESP32 family is especially useful for this chapter because it sits at the intersection of many of the ideas introduced in the reading:

- It uses a **32-bit RISC-style embedded core family** rather than a desktop-class x86 processor.
- It exposes a **modified Harvard organization** with separate instruction and data paths near the core.
- It is available in **single-core and dual-core variants**, making it a good case study for concurrency tradeoffs.
- It includes memory regions such as **Flash, SRAM, cache, and IRAM**, which let you see how memory placement affects execution.

In this lab, you will inspect your ESP32, run a small timing experiment, compare execution from different memory regions, and answer design questions that connect the chapter concepts to an actual SoC.

---

## 3.9.1 Objectives

By the end of this lab you will be able to:

1. Distinguish between **ISA** and **microarchitecture** using the ESP32 as an example.
2. Explain why the ESP32 is associated with a **RISC-style embedded core**, and why that does **not** automatically mean “faster than x86” in every sense.
3. Describe how the ESP32’s **modified Harvard architecture** differs from a simple von Neumann model.
4. Observe how **memory placement** (for example, Flash vs. IRAM) can affect execution time.
5. Identify when a **single-core** design is sufficient and when a **dual-core** SoC provides a real advantage.
6. Connect processor-family choices (Xtensa, RISC-V, ARM Cortex-M, x86) to application requirements such as power, determinism, compatibility, and software ecosystem.

---

## 3.9.2 Hardware and Software Requirements

| Item | Requirement |
|------|-------------|
| Board | ESP32 development board |
| Framework | Arduino-ESP32 (Arduino IDE or PlatformIO) |
| Libraries | None beyond the standard ESP32 Arduino core |
| Measurement API | `micros()` or `esp_timer_get_time()` |
| Serial Monitor | 115200 baud |

No external hardware is required.

---

## 3.9.3 Background Connection to Chapter 3

This lab is built around four Chapter 3 ideas:

### ISA vs. Microarchitecture

The **instruction set architecture (ISA)** is the programmer-visible interface: instructions, registers, exceptions, and memory model. The **microarchitecture** is the hardware implementation: pipeline depth, cache behavior, execution order, and frequency. Chapter 3 emphasizes that software targets the ISA, while performance and power depend heavily on the microarchitecture.

In this course, the ESP32 serves as an example of an SoC built around embedded **32-bit RISC-style cores** rather than desktop-class x86 processors. That makes it well suited to low-power control, networking, and real-time oriented tasks, even though it does not compete with high-end application processors in raw peak throughput.

### RISC vs. CISC

Chapter 3 explains that **RISC vs. CISC is not a simple speed ranking**. Embedded RISC cores are typically attractive because of their implementation simplicity, power efficiency, and timing predictability. Meanwhile, x86 processors can still outperform small embedded cores in wall-clock time because of much higher clock frequency, aggressive speculation, and wide out-of-order execution.

### Harvard vs. von Neumann

The ESP32 is a good example of a **modified Harvard architecture**. Near the CPU core, instruction and data paths are separated for bandwidth. But to the programmer, the system still looks much more like a unified address space than a strict textbook Harvard machine.

### Single-Core vs. Multi-Core

Some ESP32 devices use a **dual-core design**, which lets one core service communication or background tasks while another runs application code. Chapter 3 emphasizes that extra cores help when tasks are relatively independent, but multicore also adds synchronization cost, debugging difficulty, and possible nondeterminism.

---

## 3.9.4 What You Will Do

You will complete three short activities:

1. **Identify the processor architecture** of your board and relate it to the Chapter 3 taxonomy.
2. **Measure a simple benchmark** to observe execution time on your ESP32.
3. **Compare memory placement or task placement effects** and explain the result using Chapter 3 concepts.

The goal is not to produce the absolute fastest code. The goal is to use real observations to support architectural reasoning.

---

## 3.9.5 Part 1 — Identify the Core and Classify It

Upload and run a short sketch that prints information about the board, CPU frequency, and available cores. For example, your sketch may report:

- Chip model
- CPU frequency
- Number of cores
- SDK version
- Whether the device is using Xtensa or RISC-V (depending on the ESP32 family)

Record your observations below:

| Observation | Your value |
|-------------|------------|
| Chip/board model | ________ |
| CPU frequency | ________ |
| Number of cores | ________ |
| Core family (Xtensa or RISC-V) | ________ |

Then answer:

1. Is your board best described as a **microcontroller-class SoC** or an **application processor**?
2. Is its ISA closer to the **embedded RISC** examples discussed in Chapter 3, or to desktop/server **x86** systems?
3. What kind of products would this core family be appropriate for?

---

## 3.9.6 Part 2 — Timing a Simple Workload

Create a simple loop-based benchmark such as one of the following:

- Integer accumulation
- Array sum
- Small FIR-like multiply-add loop
- Bit manipulation loop

Measure the execution time for a fixed number of iterations using `micros()` or `esp_timer_get_time()`.

A generic structure is:

```cpp
uint64_t t_start = esp_timer_get_time();
for (int i = 0; i < N; i++) {
    // test workload
}
uint64_t t_end = esp_timer_get_time();
```

Repeat the measurement several times and record the average.

| Measurement | Your value |
|-------------|------------|
| Workload used | ________ |
| Iteration count | ________ |
| Average execution time | ________ |

### Interpretation questions

1. Does this timing result tell you the **ISA** is fast, or the **microarchitecture** and clock rate of this specific implementation are sufficient for the workload?
2. Why would Chapter 3 say that performance depends on more than the RISC/CISC label alone?
3. Would the same loop necessarily run faster on x86? Explain why the answer is not simply “yes because x86 is CISC” or “no because RISC is better.”

---

## 3.9.7 Part 3 — Memory Architecture Observation

Chapter 3 explains that the ESP32 uses a **modified Harvard memory organization** with separate instruction and data paths near the core, plus caches and distinct memory regions such as Flash and IRAM.

For this part, compare two cases:

- **Case A:** Run your benchmark function in its normal location.
- **Case B:** Place the same function in **IRAM** using the appropriate attribute (for example, `IRAM_ATTR`) if your board/framework supports it.

Measure both versions.

| Case | Average time |
|------|--------------|
| Default code placement | ________ |
| IRAM placement | ________ |

### Questions

1. Was there a measurable difference?
2. If so, how does Chapter 3 explain it in terms of **instruction fetch path**, **cache behavior**, or **wait states**?
3. Why does this support the claim that modern embedded systems often use a **modified Harvard** structure rather than a pure von Neumann model?
4. Why is it incorrect to say Harvard always means “two completely separate physical memories visible to the programmer”?

---

## 3.9.8 Part 4 — Single-Core vs. Multi-Core Reasoning

If your ESP32 board has two cores, create two FreeRTOS tasks:

- One task repeatedly performs a simple compute workload.
- One task periodically prints status text or simulates a communication/background job.

If your board has one core, answer the questions conceptually instead.

Observe how the system behaves when:

- both activities share one core, versus
- the activities are pinned to different cores (if supported).

### Questions

1. What type of workload benefits most from using multiple cores: **independent tasks** or **tightly coupled tasks that share data constantly**?
2. According to Chapter 3, what costs appear when moving from single-core to multicore?
3. Why might a dual-core ESP32 be useful for networking plus application logic, but unnecessary for a tiny sensor node?
4. Why can a single-core design still be the better engineering choice when predictability and simplicity matter most?

---

## 3.9.9 Part 5 — Processor Family Comparison

Using the processor families discussed in Chapter 3, complete the table below.

| Processor family | Best fit application | Why |
|------------------|----------------------|-----|
| MSP430 | ________ | ________ |
| ARM Cortex-M | ________ | ________ |
| ESP32 Xtensa / RISC-V | ________ | ________ |
| ARM Cortex-A | ________ | ________ |
| x86 / x86-64 | ________ | ________ |

Then answer:

1. Why would a battery-powered environmental sensor usually not use x86?
2. Why might an industrial gateway still choose x86 anyway?
3. Why is an ESP32-class SoC a good fit for connected embedded devices but not for a rich desktop operating system workload?

---

## 3.9.10 Reflection Question (Submitted via Serial or LMS)

Write a short paragraph answering the following:

> Based on your observations and Chapter 3, explain why the ESP32 is a good example of an embedded RISC-style SoC. In your answer, distinguish between ISA and microarchitecture, describe its modified Harvard memory organization at a high level, and state whether a single-core or multicore version would be better for a battery-powered IoT node that must handle sensor sampling, wireless communication, and light application logic.

A strong answer should include:

- A correct distinction between **ISA** and **microarchitecture**
- A recognition that **RISC does not automatically mean highest raw performance**
- A brief explanation of **modified Harvard architecture**
- A justified choice between **single-core** and **multicore** based on workload tradeoffs

---

## 3.9.11 Lab Report Questions

In your lab report, answer the following:

1. **ISA vs. implementation:** Suppose two processors implement the same ISA, but one has a shallow in-order pipeline and the other has a deeper, higher-frequency design. Which properties belong to the ISA, and which belong to the microarchitecture?

2. **RISC misconception:** A student says, “RISC is always faster than CISC.” Use Chapter 3 and your ESP32 observations to explain why that statement is too simplistic.

3. **Memory hierarchy:** Why can placing a function in IRAM improve execution consistency or speed on some ESP32 workloads?

4. **Core-count decision:** You are designing a wearable sensor that samples data, encrypts small packets, and sends them over BLE a few times per second. Would you choose a single-core or dual-core SoC? Justify your answer using power, determinism, and software complexity.

5. **Architecture selection:** Compare an ESP32-class SoC, an ARM Cortex-A SoC, and an x86-based system for a smart-home hub. Which would you choose for a low-cost battery-powered node, and which would you choose for a mains-powered gateway? Why?

---

## 3.9.12 Grading

| Component | Points | How graded |
|-----------|--------|------------|
| Part 1 device identification | 15 | Completion + correctness |
| Part 2 timing measurement | 20 | Working benchmark + recorded results |
| Part 3 memory architecture analysis | 20 | Measurement + explanation |
| Reflection question | 20 | Technical accuracy and use of Chapter 3 concepts |
| Lab report questions | 25 | Depth of reasoning |

Total: **100 points**

---

## 3.9.13 Summary

This lab turns Chapter 3 from a reading-only topic into a hands-on architecture exercise. By observing a real ESP32, you connect abstract ideas—**ISA vs. microarchitecture, RISC vs. CISC, Harvard vs. von Neumann, and single-core vs. multicore design**—to the behavior of an actual embedded SoC.

The key lesson is the same one emphasized throughout Chapter 3: processor selection is always about **tradeoffs**. The “best” core depends on whether you care most about raw speed, low power, determinism, compatibility, software ecosystem, or integration cost.
