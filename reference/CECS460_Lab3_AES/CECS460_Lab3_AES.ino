/*
  ============================================================
  CECS 460 Chapter 3 ESP32 Timing Lab
  Student-Facing Version
  ============================================================

  Students should focus on the TODO area and the two tasks below.

  Hidden in helper files:
  - WiFi connection
  - MQTT connection
  - slot/token assignment
  - student URL printing
  - answer publishing

  Required libraries:
  - PubSubClient
  - ArduinoJson

  Hardware:
    LED:
      GPIO 2 -> 220 ohm resistor -> LED anode (+)
      LED cathode (-) -> GND

    Button:
      One side of pushbutton -> GPIO 4
      Other side -> GND

    BUTTON_PIN uses INPUT_PULLUP:
      Not pressed = HIGH
      Pressed     = LOW
*/

#include <Arduino.h>
#include "LabConfig.h"
#include "ClassroomClient.h"

// ============================================================
// STUDENT TODO AREA
// ============================================================

/*
  TODO 1:
  The heavy background load task is intentionally placed on the
  same core as the timing-critical blink task.

  Fix:
    Move LOAD_TASK_CORE to the other core.

  Hint:
    ESP32 has core 0 and core 1.
*/
const int BLINK_TASK_CORE = 0;
const int LOAD_TASK_CORE = 0;   // BUG: change this to 1

/*
  TODO 2:
  Inside loadTask(), the heavy loop can hog CPU time.

  Fix:
    Add vTaskDelay(1) after the heavy for-loop.

  Look for the TODO comment inside loadTask().
*/

// ============================================================
// Lab state
// ============================================================
volatile bool testRunning = false;
volatile bool testFinished = false;

volatile uint32_t blinkCount = 0;
volatile uint32_t maxJitterUs = 0;
volatile uint32_t missedDeadlines = 0;
volatile uint32_t buttonPressCount = 0;

volatile bool ledState = false;

volatile int blinkTaskCore = -1;
volatile int loadTaskCore = -1;

bool lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelayMs = 50;

unsigned long testStartMs = 0;

TaskHandle_t blinkTaskHandle = NULL;
TaskHandle_t loadTaskHandle = NULL;

// ============================================================
// Forward declarations
// ============================================================
void printHeader();
void printInstructions();
void checkButton();
void resetTestStats();
void printResults();
void publishTimingResult();
String buildTimingAnswer();

void blinkTask(void *pvParameters);
void loadTask(void *pvParameters);

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  printHeader();
  printInstructions();

  classroomBegin();

  xTaskCreatePinnedToCore(
    blinkTask,
    "BlinkTask",
    4096,
    NULL,
    2,
    &blinkTaskHandle,
    BLINK_TASK_CORE
  );

  xTaskCreatePinnedToCore(
    loadTask,
    "LoadTask",
    4096,
    NULL,
    1,
    &loadTaskHandle,
    LOAD_TASK_CORE
  );

  Serial.println();
  Serial.println("System ready.");
  Serial.println("Press the button to start the timing test.");
  Serial.println("Type 'status' in Serial Monitor to reprint the student URL.");
}

// ============================================================
// Loop
// ============================================================
void loop() {
  classroomLoop();

  checkButton();

  if (testRunning && !testFinished) {
    if (millis() - testStartMs >= TEST_DURATION_MS) {
      testFinished = true;
      testRunning = false;

      printResults();
      publishTimingResult();

      Serial.println();
      Serial.println("Press the button again to rerun the test.");
      Serial.println("Type 'status' to reprint your student URL.");
    }
  }

  delay(10);
}

// ============================================================
// Button handling
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

void resetTestStats() {
  blinkCount = 0;
  maxJitterUs = 0;
  missedDeadlines = 0;
  ledState = false;
  digitalWrite(LED_PIN, LOW);
}

// ============================================================
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
// Heavy background task
// ============================================================
void loadTask(void *pvParameters) {
  loadTaskCore = xPortGetCoreID();

  volatile uint32_t dummy = 0;

  while (true) {
    if (testRunning && !testFinished) {
      for (uint32_t i = 0; i < 500000; i++) {
        dummy += (i ^ 0x55AA55AA);
      }

      // TODO 2:
      // Add a small delay/yield here so the heavy task does not hog CPU time.
      // Example fix:
      // vTaskDelay(1);

    } else {
      vTaskDelay(10);
    }
  }
}

// ============================================================
// Result publishing
// ============================================================
void publishTimingResult() {
  String answer = buildTimingAnswer();
  classroomPublishAnswer("q_lab1", answer);
}

String buildTimingAnswer() {
  String statusText;

  if (missedDeadlines == 0 && maxJitterUs < JITTER_FAIL_US) {
    statusText = "PASS";
  } else {
    statusText = "FAIL";
  }

  String answer = "[timing:";
  answer += "student_id=";
  answer += STUDENT_ID;

  if (classroomHasAssignment()) {
    answer += " slot=";
    answer += String(classroomSlot());
    answer += " token=";
    answer += classroomToken();
  }

  answer += " produced=";
  answer += String(blinkCount);
  answer += " consumed=";
  answer += String(blinkCount);
  answer += " missed=";
  answer += String(missedDeadlines);
  answer += " backlog=0";
  answer += " producer_core=";
  answer += String(blinkTaskCore);
  answer += " consumer_core=";
  answer += String(loadTaskCore);
  answer += " max_jitter_us=";
  answer += String(maxJitterUs);
  answer += " status=";
  answer += statusText;
  answer += "]";

  return answer;
}

// ============================================================
// Serial output
// ============================================================
void printHeader() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println("CECS 460 Chapter 3 ESP32 Timing Lab");
  Serial.println("Student-Facing Version");
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

  Serial.println("Student tasks:");
  Serial.println("  TODO 1: Move the heavy load task to the other core.");
  Serial.println("  TODO 2: Add a small yield/delay inside the heavy load task.");
  Serial.println();

  Serial.println("Serial Monitor commands:");
  Serial.println("  status   - reprint WiFi/MQTT/slot/token/student URL");
  Serial.println("  url      - reprint student URL");
  Serial.println("  announce - request slot/token again");
  Serial.println("  help     - show commands");
  Serial.println();
}

void printResults() {
  String statusText;

  if (missedDeadlines == 0 && maxJitterUs < JITTER_FAIL_US) {
    statusText = "PASS";
  } else {
    statusText = "FAIL";
  }

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
  Serial.print("student_id=");
  Serial.println(STUDENT_ID);
  Serial.print("status=");
  Serial.println(statusText);
  Serial.println("==========================================");
}
