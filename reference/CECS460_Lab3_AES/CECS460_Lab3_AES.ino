/*
  CECS 460 Chapter 3
  Shared Buffer Contention Lab - BROKEN STARTER (Per-Run Reset)

  Hardware:
    - ESP32
    - LED on GPIO 2
    - Pushbutton on GPIO 4
    - Button wired to GND, using INPUT_PULLUP

  Behavior:
    - One button press starts ONE fresh test run
    - That press creates a burst of events
    - Core 0 produces the burst
    - Core 1 consumes the burst
    - Leave the burst side alone, only work with fixing consumertask
    - Shared buffer/state handling is intentionally bad
    - Result is printed per run
    - Would you check the input or output on reading the shared data?

  Intended lesson:
    - Multicore can still perform badly if both cores fight over shared state
*/

#include <Arduino.h>

const int LED_PIN = 2;
const int BUTTON_PIN = 4;

// Stronger contention settings
const int BUFFER_SIZE = 8;          // small buffer = easier overflow
const int BURST_SIZE = 20;          // one press creates 20 events
const unsigned long DEBOUNCE_MS = 60;
const unsigned long RUN_WINDOW_MS = 400;   // one trial lasts 400 ms
const unsigned long LED_PULSE_MS = 40;

// Shared state
struct SharedState {
  volatile uint32_t eventBuffer[BUFFER_SIZE];
  volatile int writeIndex;
  volatile int readIndex;
  volatile int producedCount;
  volatile int consumedCount;
  volatile int missedCount;
  volatile int backlog;
};

SharedState shared = { {}, 0, 0, 0, 0, 0, 0 };

// Run state
volatile bool runActive = false;
volatile bool burstInjected = false;
volatile unsigned long runStartMs = 0;
volatile int runNumber = 0;

// LED pulse state
volatile bool ledPulseRequested = false;
volatile unsigned long ledPulseStart = 0;

// debounce
bool lastButtonState = HIGH;
unsigned long lastPressTime = 0;

// task handles
TaskHandle_t producerTaskHandle = NULL;
TaskHandle_t consumerTaskHandle = NULL;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
bool bufferFull() {
  return shared.backlog >= BUFFER_SIZE;
}

bool bufferEmpty() {
  return shared.backlog <= 0;
}

void resetRunState() {
  shared.writeIndex = 0;
  shared.readIndex = 0;
  shared.producedCount = 0;
  shared.consumedCount = 0;
  shared.missedCount = 0;
  shared.backlog = 0;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    shared.eventBuffer[i] = 0;
  }

  burstInjected = false;
  ledPulseRequested = false;
  digitalWrite(LED_PIN, LOW);
}

void printRunSummary() {
  Serial.println();
  Serial.println("=========== RUN SUMMARY ===========");
  Serial.print("run_number     = ");
  Serial.println(runNumber);
  Serial.print("producedCount  = ");
  Serial.println(shared.producedCount);
  Serial.print("consumedCount  = ");
  Serial.println(shared.consumedCount);
  Serial.print("missedCount    = ");
  Serial.println(shared.missedCount);
  Serial.print("backlog        = ");
  Serial.println(shared.backlog);
  Serial.print("producer_core  = ");
  Serial.println(0);
  Serial.print("consumer_core  = ");
  Serial.println(1);

  if (shared.consumedCount == shared.producedCount && shared.missedCount == 0) {
    Serial.println("status=PASS");
  } else {
    Serial.println("status=FAIL");
  }

  Serial.println("==================================");
}

// ------------------------------------------------------------
// Producer task (Core 0)
// Reads button event and injects burst into shared buffer
// ------------------------------------------------------------
void producerTask(void *pvParameters) {
  while (true) {
    if (runActive && !burstInjected) {
      burstInjected = true;

      // Create the burst for this run
      for (int i = 0; i < BURST_SIZE; i++) {
        if (!bufferFull()) {
          // Producer writes directly into shared state
          shared.eventBuffer[shared.writeIndex] = micros();
          shared.writeIndex = (shared.writeIndex + 1) % BUFFER_SIZE;
          shared.producedCount++;
          shared.backlog++;
        } else {
          shared.missedCount++;
        }

        // Small gap between produced events
        delay(1);
      }
    }

    vTaskDelay(1);
  }
}

// ------------------------------------------------------------
// Consumer task (Core 1)
// Intentionally bad: touches shared state too much and does
// heavy work every time an event is available
// FIX HERE 
// ------------------------------------------------------------
void consumerTask(void *pvParameters) {
  while (true) {
    if (runActive && !bufferEmpty()) {
      volatile uint32_t checksum = 0;

      // BAD PART 1:
      // Repeatedly scan the whole shared buffer
      for (int i = 0; i < BUFFER_SIZE; i++) {
        checksum += shared.eventBuffer[i];
      }

      // Consume one event
      uint32_t eventTime = shared.eventBuffer[shared.readIndex];
      (void)eventTime;

      shared.readIndex = (shared.readIndex + 1) % BUFFER_SIZE;
      shared.consumedCount++;
      shared.backlog--;

      // LED pulse for each consumed event
      ledPulseRequested = true;
      ledPulseStart = millis();

      // BAD PART 2:
      // Heavy work immediately after shared access
      // Makes the consumer slower and creates more visible backlog
      for (int i = 0; i < 300000; i++) {
        checksum ^= (i + shared.backlog);
      }

      // BAD PART 3:
      // Extra unnecessary shared accesses
      if (shared.backlog > 0) {
        checksum += shared.producedCount;
        checksum += shared.consumedCount;
      }
    }

    // small delay, but still enough to make contention visible
    vTaskDelay(1);
  }
}

// ------------------------------------------------------------
// Setup
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("=== CECS 460 Chapter 3 Shared Buffer Contention Lab ===");
  Serial.println("Press the button to start one fresh run.");
  Serial.println("Each press creates a burst of events.");
  Serial.println("This starter version is intentionally broken.");
  Serial.println();

  xTaskCreatePinnedToCore(
    producerTask,
    "ProducerTask",
    4096,
    NULL,
    2,
    &producerTaskHandle,
    0
  );

  xTaskCreatePinnedToCore(
    consumerTask,
    "ConsumerTask",
    4096,
    NULL,
    1,
    &consumerTaskHandle,
    1
  );
}

// ------------------------------------------------------------
// Main loop
// ------------------------------------------------------------
void loop() {
  // -------------------------
  // Button handling
  // -------------------------
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    unsigned long now = millis();

    if ((now - lastPressTime) > DEBOUNCE_MS && !runActive) {
      lastPressTime = now;

      runNumber++;
      resetRunState();

      runActive = true;
      runStartMs = millis();

      Serial.println();
      Serial.println("==================================");
      Serial.print("Starting run ");
      Serial.println(runNumber);
      Serial.print("Burst size = "); 
      Serial.println(BURST_SIZE);
      Serial.println("==================================");
    }
  }

  lastButtonState = currentButtonState;

  // -------------------------
  // LED pulse handling
  // -------------------------
  if (ledPulseRequested) {
    digitalWrite(LED_PIN, HIGH);
    if (millis() - ledPulseStart >= LED_PULSE_MS) {
      digitalWrite(LED_PIN, LOW);
      ledPulseRequested = false;
    }
  }

  // -------------------------
  // End current run after fixed window
  // -------------------------
  if (runActive && (millis() - runStartMs >= RUN_WINDOW_MS)) {
    runActive = false;
    printRunSummary();
    Serial.println("Press button again for a new run.");
  }

  delay(5);
}
