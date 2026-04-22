/*
  ============================================================
  CECS 460 Chapter 3 Starter Lab
  Single-Core vs Dual-Core Timing Bug-Fix Lab
  ============================================================

  PURPOSE
  -------
  This starter sketch demonstrates a timing problem caused by
  poor task placement on a dual-core ESP32.

  A timing-critical LED task and a heavy background load task
  are currently placed in a bad way, which can increase timing
  jitter and missed deadlines.

  STUDENT GOAL
  ------------
  Fix the bug so that the LED timing becomes more stable.

  The intended fix is related to:
  - task core placement
  - task scheduling / yielding

  REQUIRED PARTS
  --------------
  1x ESP32 DevKit board
  1x Breadboard
  1x LED
  1x 220 ohm resistor
  1x Pushbutton
  1x 10k ohm resistor (optional if not using INPUT_PULLUP)
  Jumper wires
  USB cable

  PIN USAGE
  ---------
  LED_PIN       = GPIO 2
  BUTTON_PIN    = GPIO 4

  BREADBOARD CONNECTIONS
  ----------------------
  LED circuit:
    - ESP32 GPIO 2 -> 220 ohm resistor -> LED anode (+)
    - LED cathode (-) -> GND

  Button circuit (recommended with internal pull-up):
    - One side of pushbutton -> GPIO 4
    - Other side of pushbutton -> GND
    - In code, BUTTON_PIN uses INPUT_PULLUP

  HOW THE BUTTON WORKS
  --------------------
  Because INPUT_PULLUP is used:
    - button NOT pressed = HIGH
    - button pressed     = LOW

  LAB BEHAVIOR
  ------------
  - The LED should toggle at a fixed interval.
  - A background task creates heavy CPU load.
  - A button press starts the test run.
  - Timing statistics are printed to Serial Monitor.

  CURRENT BUG
  -----------
  The heavy background task is pinned to the same core as the
  timing-critical LED task, and it does not yield enough.

  That means:
  - more jitter
  - possible missed deadlines
  - poor timing stability

  WHAT STUDENTS SHOULD FIX
  ------------------------
  1. Core assignment for the heavy load task
  2. Scheduling behavior inside the load task

  SERIAL MONITOR
  --------------
  Open Serial Monitor at 115200 baud.

  ============================================================
*/

#include <Arduino.h>

// ------------------------------------------------------------
// Hardware pins
// ------------------------------------------------------------
const int LED_PIN = 2;
const int BUTTON_PIN = 4;

// ------------------------------------------------------------
// Test settings
// ------------------------------------------------------------
const uint32_t LED_PERIOD_MS = 250;        // Intended LED toggle period
const uint32_t TEST_DURATION_MS = 10000;   // Total test duration after button press
const uint32_t JITTER_FAIL_US = 20000;     // Jitter threshold for missed deadline

// ------------------------------------------------------------
// Shared test state
// ------------------------------------------------------------
volatile bool testRunning = false;
volatile bool testFinished = false;

volatile uint32_t blinkCount = 0;
volatile uint32_t maxJitterUs = 0;
volatile uint32_t missedDeadlines = 0;
volatile uint32_t buttonPressCount = 0;

volatile bool ledState = false;

// Track which core each task is using
volatile int blinkTaskCore = -1;
volatile int loadTaskCore = -1;

// ------------------------------------------------------------
// Button debounce state
// ------------------------------------------------------------
bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelayMs = 50;

// ------------------------------------------------------------
// Timing state
// ------------------------------------------------------------
unsigned long testStartMs = 0;

// ------------------------------------------------------------
// Task handles
// ------------------------------------------------------------
TaskHandle_t blinkTaskHandle = NULL;
TaskHandle_t loadTaskHandle = NULL;

// ------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------
void printHeader();
void printInstructions();
void printResults();
void resetTestStats();
void checkButton();

void blinkTask(void *pvParameters);
void loadTask(void *pvParameters);

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);

  printHeader();
  printInstructions();

  // ----------------------------------------------------------
  // BUGGY STARTER CONFIGURATION
  // ----------------------------------------------------------
  // blinkTask should be protected as much as possible.
  // loadTask is intentionally placed badly here.
  //
  // Students are expected to fix the task placement.
  // ----------------------------------------------------------

  xTaskCreatePinnedToCore(
    blinkTask,          // Task function
    "BlinkTask",        // Name
    4096,               // Stack size
    NULL,               // Parameters
    2,                  // Priority
    &blinkTaskHandle,   // Handle
    0                   // Core 0
  );

  xTaskCreatePinnedToCore(
    loadTask,           // Task function
    "LoadTask",         // Name
    4096,               // Stack size
    NULL,               // Parameters
    1,                  // Priority
    &loadTaskHandle,    // Handle
    0                   // BUG: also on Core 0
  );

  Serial.println("System ready.");
  Serial.println("Press the button to start the timing test.");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  checkButton();

  if (testRunning && !testFinished) {
    if (millis() - testStartMs >= TEST_DURATION_MS) {
      testFinished = true;
      testRunning = false;
      printResults();

      Serial.println();
      Serial.println("Press the button again to rerun the test.");
    }
  }

  delay(10);
}

// ============================================================
// BUTTON HANDLING
// ============================================================
void checkButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelayMs) {
    static bool buttonLatched = false;

    if (reading == LOW && !buttonLatched) {
      buttonLatched = true;
      buttonPressCount++;

      if (!testRunning) {
        resetTestStats();
        testRunning = true;
        testFinished = false;
        testStartMs = millis();

        Serial.println();
        Serial.println("======================================");
        Serial.println("Test started.");
        Serial.println("======================================");
      }
    }

    if (reading == HIGH) {
      buttonLatched = false;
    }
  }

  lastButtonReading = reading;
}

// ============================================================
// RESET TEST STATS
// ============================================================
void resetTestStats() {
  blinkCount = 0;
  maxJitterUs = 0;
  missedDeadlines = 0;
  ledState = false;
  digitalWrite(LED_PIN, LOW);
}

// ============================================================
// BLINK TASK
// Timing-critical task
// ============================================================
void blinkTask(void *pvParameters) {
  blinkTaskCore = xPortGetCoreID();

  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t periodTicks = pdMS_TO_TICKS(LED_PERIOD_MS);

  uint32_t lastMicros = micros();

  while (true) {
    if (testRunning && !testFinished) {
      uint32_t nowMicros = micros();
      uint32_t actualDeltaUs = nowMicros - lastMicros;
      uint32_t targetDeltaUs = LED_PERIOD_MS * 1000UL;

      uint32_t jitterUs;
      if (actualDeltaUs > targetDeltaUs) {
        jitterUs = actualDeltaUs - targetDeltaUs;
      } else {
        jitterUs = targetDeltaUs - actualDeltaUs;
      }

      if (jitterUs > maxJitterUs) {
        maxJitterUs = jitterUs;
      }

      if (jitterUs > JITTER_FAIL_US) {
        missedDeadlines++;
      }

      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);

      blinkCount++;
      lastMicros = nowMicros;
    } else {
      lastMicros = micros();
    }

    vTaskDelayUntil(&lastWakeTime, periodTicks);
  }
}

// ============================================================
// LOAD TASK
// Heavy background workload
// ============================================================
void loadTask(void *pvParameters) {
  loadTaskCore = xPortGetCoreID();

  volatile uint32_t dummy = 0;

  while (true) {
    if (testRunning && !testFinished) {
      // Heavy CPU work
      for (uint32_t i = 0; i < 500000; i++) {
        dummy += (i ^ 0x55AA55AA);
      }

      // ------------------------------------------------------
      // BUG / IMPROVEMENT AREA
      // ------------------------------------------------------
      // This task currently does not yield enough.
      // Students may need to add a delay or yield here.
      // Example possible fix:
      // vTaskDelay(1);
      // ------------------------------------------------------
    } else {
      vTaskDelay(10);
    }
  }
}

// ============================================================
// SERIAL OUTPUT HELPERS
// ============================================================
void printHeader() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("CECS 460 Chapter 3 Starter Lab");
  Serial.println("Single-Core vs Dual-Core Timing Bug-Fix Lab");
  Serial.println("============================================================");
}

void printInstructions() {
  Serial.println();
  Serial.println("Required hardware:");
  Serial.println("  - ESP32 DevKit");
  Serial.println("  - 1 LED");
  Serial.println("  - 1 x 220 ohm resistor");
  Serial.println("  - 1 pushbutton");
  Serial.println("  - Breadboard and jumper wires");
  Serial.println();

  Serial.println("Pin usage:");
  Serial.println("  LED_PIN    = GPIO 2");
  Serial.println("  BUTTON_PIN = GPIO 4");
  Serial.println();

  Serial.println("Connections:");
  Serial.println("  LED: GPIO 2 -> resistor -> LED anode, LED cathode -> GND");
  Serial.println("  Button: GPIO 4 -> pushbutton -> GND");
  Serial.println("  BUTTON_PIN uses INPUT_PULLUP");
  Serial.println();

  Serial.println("Lab objective:");
  Serial.println("  Fix the buggy task placement / scheduling so the LED task");
  Serial.println("  becomes more stable during heavy background load.");
  Serial.println();
}

void printResults() {
  Serial.println();
  Serial.println("============== TEST RESULTS ==============");
  Serial.print("blink_count=");
  Serial.println(blinkCount);

  Serial.print("max_jitter_us=");
  Serial.println(maxJitterUs);

  Serial.print("missed_deadlines=");
  Serial.println(missedDeadlines);

  Serial.print("button_press_count=");
  Serial.println(buttonPressCount);

  Serial.print("blink_task_core=");
  Serial.println(blinkTaskCore);

  Serial.print("load_task_core=");
  Serial.println(loadTaskCore);

  // Simple device-side status summary
  // Server-side grading can later key off these strings if needed.
  if (missedDeadlines == 0 && maxJitterUs < JITTER_FAIL_US) {
    Serial.println("status=PASS");
  } else {
    Serial.println("status=FAIL");
  }

  Serial.println("==========================================");
}
