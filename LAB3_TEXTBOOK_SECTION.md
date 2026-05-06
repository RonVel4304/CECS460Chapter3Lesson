# Lab 3 - CPU Architectures, Task Placement, and Timing Stability on the ESP32

## Overview

This lab aligns with **Chapter 3 - CPU Architectures and Core Comparisons** by using the ESP32 as a concrete example of how processor architecture choices affect embedded-system behavior. The revised chapter emphasizes a design question that appears throughout this lab:

> What does this architectural choice make easier, and what does it make harder?

The ESP32 family is useful because it exposes several Chapter 3 ideas in a device you can program directly:

- It uses **microcontroller-class 32-bit embedded cores**, including Xtensa cores on classic ESP32 boards and RISC-V cores on newer ESP32 variants.
- It shows why **ISA and microarchitecture are different**: the ISA defines what software targets, while the implementation and SoC memory system determine timing behavior.
- It uses a **modified Harvard-style memory organization**, where separate instruction and data paths exist near the core even though C code usually sees a mostly unified programming model.
- Classic ESP32 boards provide **dual-core FreeRTOS task placement**, making them a strong case study for concurrency, contention, jitter, missed deadlines, and workload isolation.
- Newer ESP32 variants may be single-core, dual-core, or heterogeneous, giving you a chance to reason about why core count alone does not define system quality.

In this lab, you will identify your ESP32 core family, measure a simple timing workload, and run or analyze a task-placement experiment. The goal is not to produce the fastest benchmark. The goal is to connect observed behavior to architecture tradeoffs.

---

## 3.9.1 Objectives

By the end of this lab you will be able to:

1. Distinguish between **ISA** and **microarchitecture** using the ESP32 as an example.
2. Explain why the ESP32 is associated with **embedded RISC-style cores** without claiming that RISC automatically means highest raw performance.
3. Describe the ESP32's **modified Harvard-style memory organization** at a high level.
4. Measure simple execution timing and explain why timing depends on clock rate, compiler output, memory behavior, and workload.
5. Explain how **task placement** can create or reduce timing jitter in a multicore embedded system.
6. Identify how shared buffers, scheduling, memory contention, and long-running background work can cause backlog or missed deadlines.
7. Decide when a **single-core**, **symmetric multicore**, or **heterogeneous** SoC is the better engineering choice.
8. Connect processor-family choices, including MSP430, Arm Cortex-M, ESP32 Xtensa/RISC-V, Arm Cortex-A/R, and x86, to power, determinism, compatibility, and software ecosystem constraints.

---

## 3.9.2 Hardware and Software Requirements

| Item | Requirement |
|------|-------------|
| Board | ESP32 development board |
| Framework | Arduino-ESP32 using Arduino IDE or PlatformIO |
| Libraries | Standard ESP32 Arduino core and FreeRTOS APIs included with the core |
| Measurement API | `micros()` or `esp_timer_get_time()` |
| Serial Monitor | 115200 baud |
| Optional feature | Dual-core task pinning using `xTaskCreatePinnedToCore()` |

No external hardware is required.

> **Board note:** Classic ESP32 boards usually have two Xtensa LX6 cores. ESP32-C3 boards are single-core RISC-V. ESP32-C6-class devices should not be treated as ordinary symmetric dual-core ESP32 boards; they include different RISC-V cores intended for different roles. If your board does not support symmetric dual-core task pinning, complete the multicore section conceptually using your Part 1 observations.

---

## 3.9.3 Background Connection to Chapter 3

This lab is built around five Chapter 3 ideas.

### ISA vs. Microarchitecture

The **instruction set architecture (ISA)** is the contract between software and hardware. It defines the instructions, registers, memory behavior, interrupt model, and toolchain expectations that software can rely on.

The **microarchitecture** is the hardware strategy used to implement that ISA. It includes pipeline structure, cache behavior, clock frequency, interrupt latency, memory buses, and power-management behavior.

Two processors can implement the same ISA but behave very differently. For example, two RISC-V processors may both run RV32 code, but one may be a tiny in-order microcontroller core while another may have larger caches, branch prediction, and higher frequency. The ISA tells you what the program can say; the microarchitecture and SoC integration affect how quickly, efficiently, and predictably the program runs.

### RISC vs. CISC

Chapter 3 warns against the misconception that **RISC is always faster than CISC**. Embedded RISC-style cores are often attractive because they can be compact, power-efficient, predictable, and easy to integrate with peripherals. Desktop-class x86 processors can still be much faster in raw wall-clock performance because of high clock speeds, large caches, out-of-order execution, branch prediction, and wide execution pipelines.

A better engineering question is:

> Which processor meets the timing requirement with acceptable power, cost, software risk, and design complexity?

### Modified Harvard Memory Organization

A simple **von Neumann** model uses one shared path for instructions and data. A pure **Harvard** model separates instruction and data memory. Real SoCs often use a **modified Harvard** approach: separate instruction and data paths near the core, caches or fast internal memory regions, and a programming model that still feels mostly unified to C code.

On ESP32-class devices, memory placement can matter. Code running through flash cache may behave differently from code or interrupt handlers placed in internal memory. This is why architecture affects real timing, even when the C program looks ordinary.

### Single-Core, Multicore, and Workload Isolation

A single-core system is often simpler, lower power, easier to debug, and easier to reason about. A multicore system can improve responsiveness when tasks are mostly independent, but it also introduces synchronization, shared-data bugs, memory contention, and jitter.

The revised Chapter 3 emphasis is not "more cores are always better." The emphasis is:

> Multicore can improve responsiveness when task placement and shared data are handled carefully.

### Producer-Consumer Timing

Many embedded systems use a **producer-consumer** pattern. One task produces samples, packets, events, or readings. Another task consumes and processes them. A shared queue or buffer connects the two.

This pattern is useful, but it creates design questions:

- What happens if the producer runs faster than the consumer?
- What happens when the buffer fills?
- Does a heavy background task delay a timing-critical task?
- Do tasks share data safely?
- Does moving tasks to different cores reduce interference or add synchronization cost?

Your lab data should help answer these questions.

---

## 3.9.4 What You Will Do

You will complete four activities:

1. **Identify the processor architecture** of your board and relate it to the Chapter 3 taxonomy.
2. **Measure a simple timing workload** and explain why the result is not just an ISA property.
3. **Run or analyze a task-placement experiment** involving timing-critical and background work.
4. **Compare processor families** using engineering tradeoffs instead of slogans.

The central lab takeaway is:

> Architecture affects responsiveness, not just peak speed. Poor task placement, shared buffers, and memory contention can make a system miss deadlines even when the CPU seems fast enough.

---

## 3.9.5 Part 1 - Identify the Core and Classify It

Upload and run a short sketch that prints information about the board, CPU frequency, and available cores.

Example starting point:

```cpp
#include <Arduino.h>
#include "esp_system.h"
#include "esp_chip_info.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.println("ESP32 architecture identification");
  Serial.printf("Chip model code: %d\n", chip_info.model);
  Serial.printf("CPU frequency: %lu MHz\n", getCpuFrequencyMhz());
  Serial.printf("Reported cores: %d\n", chip_info.cores);
  Serial.printf("SDK version: %s\n", esp_get_idf_version());

#if CONFIG_IDF_TARGET_ESP32
  Serial.println("Likely core family: Xtensa LX6, classic ESP32");
#elif CONFIG_IDF_TARGET_ESP32S3
  Serial.println("Likely core family: Xtensa LX7-class, ESP32-S3");
#elif CONFIG_IDF_TARGET_ESP32C3
  Serial.println("Likely core family: RISC-V, ESP32-C3");
#elif CONFIG_IDF_TARGET_ESP32C6
  Serial.println("Likely core family: RISC-V, ESP32-C6");
#else
  Serial.println("Core family: check board documentation");
#endif
}

void loop() {
}
```

Record your observations:

| Observation | Your value |
|-------------|------------|
| Chip/board model | ________ |
| CPU frequency | ________ |
| Number of reported cores | ________ |
| Core family, such as Xtensa or RISC-V | ________ |
| Development framework/version | ________ |

Answer these questions:

1. Is your board best described as a **microcontroller-class SoC** or an **application processor**?
2. Which parts of your answer describe the **ISA or core family**, and which describe the **specific implementation or SoC**?
3. Is this board closer to the embedded RISC-style examples from Chapter 3, or to desktop/server x86 systems?
4. What kinds of products would this core family be appropriate for?
5. What kinds of products would this core family be a poor fit for?

---

## 3.9.6 Part 2 - Timing a Simple Workload

Create a simple loop-based benchmark such as one of the following:

- Integer accumulation
- Array sum
- Small multiply-add loop
- Bit manipulation loop
- Lightweight checksum

Measure execution time for a fixed number of iterations using `esp_timer_get_time()` or `micros()`.

Example:

```cpp
#include <Arduino.h>
#include "esp_timer.h"

volatile uint32_t sink = 0;

uint32_t workload(uint32_t n) {
  uint32_t x = 0;
  for (uint32_t i = 0; i < n; i++) {
    x = (x * 1664525UL) + 1013904223UL;
    x ^= (x >> 13);
  }
  return x;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  const uint32_t N = 200000;
  const int trials = 10;
  uint64_t total_us = 0;

  for (int t = 0; t < trials; t++) {
    uint64_t start = esp_timer_get_time();
    sink = workload(N);
    uint64_t end = esp_timer_get_time();

    uint64_t elapsed = end - start;
    total_us += elapsed;
    Serial.printf("trial=%d time_us=%llu sink=%lu\n", t, elapsed, sink);
    delay(100);
  }

  Serial.printf("average_us=%llu\n", total_us / trials);
}

void loop() {
}
```

Record your results:

| Measurement | Your value |
|-------------|------------|
| Workload used | ________ |
| Iteration count | ________ |
| Number of trials | ________ |
| Minimum time | ________ |
| Maximum time | ________ |
| Average time | ________ |
| Approximate jitter, max minus min | ________ |

### Interpretation questions

1. Does this timing result prove that the **ISA** is fast, or does it reflect the **microarchitecture, clock rate, compiler output, memory behavior, and workload** of this specific system?
2. Why does Chapter 3 say that RISC versus CISC is not a simple speed ranking?
3. Would this same loop necessarily run faster on x86? Explain why the answer is not simply "yes because x86 is CISC" or "no because RISC is better."
4. If your repeated trials varied, what could cause that variation?
5. Why is average execution time not always enough for a real-time embedded system?

---

## 3.9.7 Part 3 - Task Placement and Timing Stability

This is the main revised Chapter 3 lab activity.

You will create or run a small producer-consumer style experiment:

- A **producer task** represents timing-critical work, such as sampling a sensor or receiving packets.
- A **consumer task** represents processing, logging, encryption, formatting, printing, or other background work.
- A shared queue or buffer connects them.
- The system reports telemetry such as produced items, consumed items, missed items, backlog, task core placement, and pass/fail status.

The purpose is to observe how task placement affects timing stability.

### Experiment idea

Run two configurations:

- **Configuration A - shared core:** producer and consumer run on the same core, or both are allowed to compete without careful placement.
- **Configuration B - separated work:** timing-critical work is isolated from heavier background work, often by pinning tasks to different cores on a dual-core ESP32.

If your board is single-core, run only the shared-core version and answer the separated-core questions conceptually.

### Example dual-core task structure

This sketch is intentionally simple. Your instructor may provide a more complete version with stricter pass/fail rules.

```cpp
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

struct Sample {
  uint32_t sequence;
  uint64_t t_us;
};

QueueHandle_t q;

volatile uint32_t produced = 0;
volatile uint32_t consumed = 0;
volatile uint32_t missed = 0;

const uint32_t PRODUCER_PERIOD_MS = 10;
const uint32_t HEAVY_WORK = 60000;

void heavyCompute() {
  volatile uint32_t x = 0;
  for (uint32_t i = 0; i < HEAVY_WORK; i++) {
    x = (x * 1664525UL) + 1013904223UL;
  }
}

void producerTask(void *param) {
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    Sample s;
    s.sequence = produced;
    s.t_us = esp_timer_get_time();

    if (xQueueSend(q, &s, 0) == pdTRUE) {
      produced++;
    } else {
      missed++;
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PRODUCER_PERIOD_MS));
  }
}

void consumerTask(void *param) {
  Sample s;

  while (true) {
    if (xQueueReceive(q, &s, pdMS_TO_TICKS(20)) == pdTRUE) {
      heavyCompute();
      consumed++;
    }
  }
}

void monitorTask(void *param) {
  while (true) {
    UBaseType_t backlog = uxQueueMessagesWaiting(q);

    Serial.printf(
      "[timing:produced=%lu consumed=%lu missed=%lu backlog=%u producer_core=%d consumer_core=%d status=%s]\n",
      produced,
      consumed,
      missed,
      backlog,
      xPortGetCoreID(),
      -1,
      missed == 0 ? "PASS" : "CHECK"
    );

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  q = xQueueCreate(16, sizeof(Sample));

  // Configuration A: try pinning both producer and consumer to the same core.
  // Configuration B: try pinning producer and consumer to different cores on dual-core boards.
  // Change these values as directed by your instructor.
  const int PRODUCER_CORE = 1;
  const int CONSUMER_CORE = 1;

  xTaskCreatePinnedToCore(producerTask, "producer", 4096, NULL, 3, NULL, PRODUCER_CORE);
  xTaskCreatePinnedToCore(consumerTask, "consumer", 4096, NULL, 2, NULL, CONSUMER_CORE);
  xTaskCreatePinnedToCore(monitorTask, "monitor", 4096, NULL, 1, NULL, 0);
}

void loop() {
}
```

> **Important:** The printed `status=PASS` or `status=CHECK` is not the whole story. The important part is explaining what changed and why.

### Record your results

| Configuration | Producer core | Consumer core | Produced | Consumed | Missed | Backlog trend | Notes |
|---------------|---------------|---------------|----------|----------|--------|---------------|-------|
| A: shared core or default placement | ________ | ________ | ________ | ________ | ________ | ________ | ________ |
| B: separated or improved placement | ________ | ________ | ________ | ________ | ________ | ________ | ________ |

### Analysis questions

1. Which configuration had more missed events, backlog, or timing instability?
2. What was competing with what: CPU time, queue space, memory bandwidth, serial printing, locks, or something else?
3. Did moving work to a different core improve responsiveness? Why or why not?
4. Why does Chapter 3 say that multicore is useful mainly when tasks are relatively independent?
5. What costs or risks appear when moving from single-core to multicore?
6. Why can poor task placement create jitter even when the average workload seems small?
7. Why is "core 1 good, core 0 bad" the wrong lesson?
8. What would you change to make the design more robust: queue size, task priorities, less printing, shorter critical sections, different task placement, or reduced background workload?

---

## 3.9.8 Part 4 - Optional Memory Placement Observation

Chapter 3 also discusses memory organization: caches, internal SRAM, flash, IRAM, and modified Harvard-style instruction/data paths. This optional section connects that idea to timing.

Compare two cases if your board and framework support it:

- **Case A:** Run a benchmark function in its default code location.
- **Case B:** Place the same function in IRAM using `IRAM_ATTR`.

Example pattern:

```cpp
uint32_t defaultWorkload(uint32_t n) {
  uint32_t x = 0;
  for (uint32_t i = 0; i < n; i++) {
    x = (x * 1664525UL) + 1013904223UL;
  }
  return x;
}

uint32_t IRAM_ATTR iramWorkload(uint32_t n) {
  uint32_t x = 0;
  for (uint32_t i = 0; i < n; i++) {
    x = (x * 1664525UL) + 1013904223UL;
  }
  return x;
}
```

Record your results:

| Case | Average time | Jitter or notes |
|------|--------------|-----------------|
| Default code placement | ________ | ________ |
| IRAM placement | ________ | ________ |

### Questions

1. Was there a measurable difference?
2. If so, how could instruction fetch path, cache behavior, flash access, or wait states explain the result?
3. Why does this support the claim that modern embedded systems often use a **modified Harvard** structure rather than a pure von Neumann model?
4. Why is it incorrect to say Harvard always means "two completely separate physical memories visible to the programmer"?
5. Why might interrupt handlers or timing-critical routines be placed in internal memory on some ESP32 systems?

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

> Based on your observations and Chapter 3, explain how the ESP32 demonstrates the relationship between CPU architecture and timing behavior. In your answer, distinguish between ISA and microarchitecture, explain why RISC does not automatically mean highest raw performance, describe modified Harvard memory organization at a high level, and explain how task placement can reduce or increase jitter, backlog, or missed deadlines. Finally, state whether a single-core, dual-core, or heterogeneous design would be better for a battery-powered IoT node that must handle sensor sampling, wireless communication, and light application logic.

A strong answer should include:

- A correct distinction between **ISA** and **microarchitecture**
- A recognition that **RISC is not automatically faster than CISC**
- A brief explanation of **modified Harvard architecture**
- A discussion of **contention, isolation, jitter, backlog, or missed deadlines**
- A justified choice between **single-core**, **dual-core**, or **heterogeneous** based on workload, power, determinism, and software complexity

---

## 3.9.11 Lab Report Questions

In your lab report, answer the following:

1. **ISA vs. implementation:** Suppose two processors implement the same ISA, but one has a shallow in-order pipeline and the other has a deeper, higher-frequency design with larger caches. Which properties belong to the ISA, and which belong to the microarchitecture?

2. **RISC misconception:** A student says, "RISC is always faster than CISC." Use Chapter 3 and your ESP32 observations to explain why that statement is too simplistic.

3. **Memory hierarchy:** Why can placing a function or interrupt handler in internal memory improve execution consistency or speed on some ESP32 workloads?

4. **Core-count decision:** You are designing a wearable sensor that samples data, encrypts small packets, and sends them over BLE a few times per second. Would you choose a single-core, dual-core, or heterogeneous SoC? Justify your answer using power, determinism, timing requirements, and software complexity.

5. **Task placement:** In the producer-consumer experiment, what evidence showed whether the timing-critical task was being delayed? Use produced count, consumed count, missed count, backlog, jitter, or serial output timing in your answer.

6. **Contention and isolation:** Explain how a heavy background task can interfere with a timing-critical task even if the two tasks do not appear to do the same work.

7. **Architecture selection:** Compare an ESP32-class SoC, an Arm Cortex-A SoC, and an x86-based system for a smart-home hub. Which would you choose for a low-cost battery-powered node, and which would you choose for a mains-powered gateway? Why?

8. **Debugging multicore systems:** Why can multicore bugs be harder to debug than single-core bugs? Include at least two of the following terms: race condition, deadlock, priority inversion, cache/memory contention, shared buffer, timing jitter, or core affinity.

---

## 3.9.12 Grading

| Component | Points | How graded |
|-----------|--------|------------|
| Part 1 device identification | 15 | Completion, correctness, and correct ISA/microarchitecture language |
| Part 2 timing measurement | 15 | Working benchmark, repeated trials, and recorded timing/jitter results |
| Part 3 task-placement experiment | 25 | Correct experiment setup or conceptual analysis, telemetry collection, and explanation of timing behavior |
| Optional memory placement observation | 10 | Measurement or well-supported explanation of modified Harvard memory effects |
| Reflection question | 20 | Technical accuracy and use of Chapter 3 concepts |
| Lab report questions | 25 | Depth of reasoning and quality of architecture tradeoff analysis |

Total without optional memory section: **100 points**

Total with optional memory section: **110 points possible, capped at 100 unless your instructor states otherwise**

Suggested 100-point grading if the optional memory section is not assigned:

| Component | Points |
|-----------|--------|
| Part 1 device identification | 15 |
| Part 2 timing measurement | 15 |
| Part 3 task-placement experiment | 30 |
| Reflection question | 20 |
| Lab report questions | 20 |

---

## 3.9.13 Summary

This lab turns Chapter 3 from a reading-only topic into a hands-on architecture exercise. By observing a real ESP32, you connect abstract ideas - **ISA vs. microarchitecture, RISC vs. CISC, modified Harvard memory organization, single-core vs. multicore design, and task placement** - to the behavior of an actual embedded SoC.

The key lesson is the same one emphasized throughout the revised Chapter 3: processor selection is about **tradeoffs**. The best core is not the one with the most impressive label. It is the one that satisfies the product's timing, power, cost, software, and reliability requirements.

For the ESP32 lab, the most important architecture lesson is this:

> More cores can improve responsiveness, but only when timing-critical work, background work, shared buffers, and memory access are coordinated carefully.
