/*
 * CECS460_Ch3_CoreLab.ino
 * =======================
 * Chapter 3 — CPU Architectures and Core Comparisons
 * ESP32 classroom firmware with:
 *  - Wi-Fi + MQTT + slot/token assignment
 *  - Shared-buffer contention lab
 *  - Auto-submit q_lab1 result
 *  - Serial reflection submit for q_lab2
 *
 * Hardware:
 *   LED_PIN    = GPIO 2
 *   BUTTON_PIN = GPIO 4
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --------------------------------------------------
// Version / config
// --------------------------------------------------
#define FW_VERSION        "1.0.0"
#define FW_DATE           "2026-04"

#define DEFAULT_SSID      "CECS"
#define DEFAULT_PASS      "CECS-Classroom"
#define DEFAULT_MQTT_HOST "192.168.8.217"
#define DEFAULT_MQTT_PORT 1883

#define HEARTBEAT_MS      10000UL
#define LED_PIN           2
#define BUTTON_PIN        4

// MUST match server/class config
#define COURSE  "C460"
#define LESSON  "c460_ch03_corelab"

#define NVS_NS  "cecs460_ch3"

// --------------------------------------------------
// Lab config
// --------------------------------------------------
const int BUFFER_SIZE = 8;
const int BURST_SIZE = 20;
const unsigned long DEBOUNCE_MS   = 60;
const unsigned long RUN_WINDOW_MS = 400;
const unsigned long LED_PULSE_MS  = 40;

// --------------------------------------------------
// Shared lab state
// --------------------------------------------------
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

volatile bool runActive       = false;
volatile bool burstInjected   = false;
volatile unsigned long runStartMs = 0;
volatile int runNumber        = 0;

volatile bool ledPulseRequested = false;
volatile unsigned long ledPulseStart = 0;

// debounce
bool lastButtonState = HIGH;
unsigned long lastPressTime = 0;

// tasks
TaskHandle_t producerTaskHandle = NULL;
TaskHandle_t consumerTaskHandle = NULL;

// --------------------------------------------------
// Networking globals
// --------------------------------------------------
Preferences  prefs;
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

String  g_ssid, g_pass, g_mqttHost;
int     g_mqttPort;
String  g_deviceId, g_mac, g_studentId;
int     g_slot    = -1;
String  g_token   = "";
bool    g_verbose = false;
bool    g_announced = false;

unsigned long g_wifiRetry = 0;
unsigned long g_mqttRetry = 0;
unsigned long g_lastHB    = 0;
int           g_mqttBackoff = 2000;
int           g_dotCount    = 0;
bool          g_prevSameLine= false;

bool g_lab1Submitted = false;
bool g_lab2Submitted = false;

// --------------------------------------------------
// Serial helpers
// --------------------------------------------------
void serialLine(const String& s) {
  if (g_prevSameLine) {
    Serial.println();
    g_dotCount = 0;
    g_prevSameLine = false;
  }
  Serial.println(s);
}

void serialDot() {
  Serial.print(".");
  g_dotCount++;
  g_prevSameLine = true;
  if (g_dotCount >= 60) {
    Serial.println();
    g_dotCount = 0;
    g_prevSameLine = false;
  }
}

void verboseLog(const String& s) {
  if (g_verbose) serialLine("[DBG] " + s);
}

// --------------------------------------------------
// Preferences
// --------------------------------------------------
void loadPrefs() {
  prefs.begin(NVS_NS, true);
  g_ssid      = prefs.getString("ssid",     DEFAULT_SSID);
  g_pass      = prefs.getString("pass",     DEFAULT_PASS);
  g_mqttHost  = prefs.getString("mqttHost", DEFAULT_MQTT_HOST);
  g_mqttPort  = prefs.getInt   ("mqttPort", DEFAULT_MQTT_PORT);
  g_studentId = prefs.getString("student",  "");
  g_slot      = prefs.getInt   ("slot",     -1);
  g_token     = prefs.getString("token",    "");
  g_verbose   = prefs.getBool  ("verbose",  false);
  g_deviceId  = prefs.getString("deviceId", "");
  prefs.end();
}

void savePref(const String& k, const String& v) {
  prefs.begin(NVS_NS, false);
  prefs.putString(k.c_str(), v.c_str());
  prefs.end();
}

void savePrefInt(const String& k, int v) {
  prefs.begin(NVS_NS, false);
  prefs.putInt(k.c_str(), v);
  prefs.end();
}

void savePrefBool(const String& k, bool v) {
  prefs.begin(NVS_NS, false);
  prefs.putBool(k.c_str(), v);
  prefs.end();
}

void clearAssignment() {
  prefs.begin(NVS_NS, false);
  prefs.remove("slot");
  prefs.remove("token");
  prefs.end();

  g_slot = -1;
  g_token = "";
  g_announced = false;
  g_lab1Submitted = false;
  g_lab2Submitted = false;

  serialLine("[NVS] Assignment cleared — will re-announce");
}

// --------------------------------------------------
// MAC
// --------------------------------------------------
String getMac() {
  WiFi.mode(WIFI_STA);
  delay(50);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return mac;
}

// --------------------------------------------------
// Wi-Fi
// --------------------------------------------------
void checkWifi() {
  static bool prev = false;
  bool conn = (WiFi.status() == WL_CONNECTED);

  if (conn && !prev) {
    serialLine("[WiFi] Connected! IP:" + WiFi.localIP().toString() +
               " RSSI:" + String(WiFi.RSSI()) + "dBm");
    g_mqttBackoff = 2000;
    g_announced = false;
  } else if (!conn && prev) {
    serialLine("[WiFi] Disconnected");
  }
  prev = conn;

  if (!conn) {
    unsigned long now = millis();
    if (now - g_wifiRetry >= 8000) {
      g_wifiRetry = now;
      serialLine("[WiFi] Connecting to " + g_ssid + "...");
      WiFi.mode(WIFI_STA);
      WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    }
  }
}

// --------------------------------------------------
// MQTT callback
// --------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String t(topic), p;
  for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
  verboseLog("MQTT RX " + t + " : " + p);

  if (t == String(COURSE) + "/device/assign/" + g_mac) {
    JsonDocument doc;
    if (deserializeJson(doc, p) == DeserializationError::Ok) {
      g_slot  = doc["slot"].as<int>();
      g_token = doc["token"].as<String>();
      String url = doc["student_url"].as<String>();

      savePrefInt("slot", g_slot);
      savePref("token", g_token);

      serialLine("");
      serialLine("╔══════════════════════════════════════════════════════╗");
      serialLine("║              SLOT ASSIGNED                           ║");
      serialLine("╠══════════════════════════════════════════════════════╣");
      serialLine("║ Slot   : " + String(g_slot));
      serialLine("║ Token  : " + g_token);
      serialLine("║ URL    : " + url);
      serialLine("╚══════════════════════════════════════════════════════╝");
    }
  }

  if (t == String(LESSON) + "/control/step") {
    JsonDocument doc;
    if (deserializeJson(doc, p) == DeserializationError::Ok) {
      serialLine("[Lesson] Active step → " + String(doc["step"].as<String>()));
    }
  }

  if (t == String(LESSON) + "/control/broadcast") {
    JsonDocument doc;
    if (deserializeJson(doc, p) == DeserializationError::Ok) {
      serialLine("[Broadcast] " + doc["message"].as<String>());
    }
  }
}

// --------------------------------------------------
// MQTT connect / announce
// --------------------------------------------------
void checkMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) {
    g_mqttBackoff = 2000;
    return;
  }

  unsigned long now = millis();
  if (now - g_mqttRetry < (unsigned long)g_mqttBackoff) return;
  g_mqttRetry = now;

  serialLine("[MQTT] Connecting to " + g_mqttHost + ":" + String(g_mqttPort) + "...");
  String cid = g_deviceId + "_" + String(random(0xffff), HEX);

  if (mqtt.connect(cid.c_str())) {
    serialLine("[MQTT] Connected");
    g_mqttBackoff = 2000;

    mqtt.subscribe((String(COURSE) + "/device/assign/" + g_mac).c_str());
    mqtt.subscribe((String(LESSON) + "/control/step").c_str());
    mqtt.subscribe((String(LESSON) + "/control/broadcast").c_str());

    g_announced = false;
  } else {
    serialLine("[MQTT] Failed rc=" + String(mqtt.state()) +
               ", retry in " + String(g_mqttBackoff / 1000) + "s");
    g_mqttBackoff = min(g_mqttBackoff * 2, 30000);
  }
  serialLine("[MQTT] Connecting to " + g_mqttHost + ":" + String(g_mqttPort) +
           " from local IP " + WiFi.localIP().toString() + "...");
}

void announce() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["mac"] = g_mac;
  doc["device_id"] = g_deviceId;
  doc["firmware"] = FW_VERSION;
  if (g_slot > 0) doc["saved_slot"] = g_slot;
  if (g_token.length()) doc["saved_token"] = g_token;
  if (g_studentId.length()) doc["student_id"] = g_studentId;

  String out;
  serializeJson(doc, out);

  mqtt.publish((String(COURSE) + "/device/announce").c_str(), out.c_str());
  serialLine("[MQTT] Announce sent (MAC=" + g_mac + ")");
  g_announced = true;
}

// --------------------------------------------------
// Publish helpers
// --------------------------------------------------
void publishStatus() {
  if (!mqtt.connected() || g_slot < 0) return;

  JsonDocument doc;
  doc["slot"]      = g_slot;
  doc["ip"]        = WiFi.localIP().toString();
  doc["rssi"]      = WiFi.RSSI();
  doc["uptime"]    = millis() / 1000;
  doc["firmware"]  = FW_VERSION;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["student_id"]= g_studentId;
  doc["produced"]  = shared.producedCount;
  doc["consumed"]  = shared.consumedCount;
  doc["missed"]    = shared.missedCount;
  doc["backlog"]   = shared.backlog;

  String out;
  serializeJson(doc, out);
  mqtt.publish((String(COURSE) + "/device/status/" + String(g_slot)).c_str(), out.c_str());

  if (g_verbose) serialLine("[HB] " + out);
  else serialDot();
}

void publishLabAnswer() {
  if (!mqtt.connected() || g_slot < 0) return;

  const char* status =
    (shared.consumedCount == shared.producedCount && shared.missedCount == 0)
      ? "PASS" : "FAIL";

  char buf[180];
  snprintf(buf, sizeof(buf),
           "[timing:produced=%d consumed=%d missed=%d backlog=%d producer_core=%d consumer_core=%d status=%s]",
           shared.producedCount,
           shared.consumedCount,
           shared.missedCount,
           shared.backlog,
           0,
           1,
           status);

  JsonDocument doc;
  doc["slot"]   = g_slot;
  doc["token"]  = g_token;
  doc["step"]   = "q_lab1";
  doc["answer"] = String(buf);

  String out;
  serializeJson(doc, out);
  mqtt.publish((String(LESSON) + "/" + String(g_slot) + "/answer").c_str(), out.c_str());

  serialLine("[MQTT] q_lab1 auto-submitted: " + String(buf));
  g_lab1Submitted = true;
}

void publishReflectionAnswer(const String& text) {
  if (!mqtt.connected() || g_slot < 0) {
    serialLine("[MQTT] Not connected or no slot — answer not sent");
    return;
  }

  JsonDocument doc;
  doc["slot"]   = g_slot;
  doc["token"]  = g_token;
  doc["step"]   = "q_lab2";
  doc["answer"] = text;

  String out;
  serializeJson(doc, out);
  mqtt.publish((String(LESSON) + "/" + String(g_slot) + "/answer").c_str(), out.c_str());

  serialLine("[MQTT] q_lab2 submitted (" + String(text.length()) + " chars)");
  g_lab2Submitted = true;
}

// --------------------------------------------------
// Lab helpers
// --------------------------------------------------
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

// --------------------------------------------------
// Producer task (Core 0)
// --------------------------------------------------
void producerTask(void *pvParameters) {
  while (true) {
    if (runActive && !burstInjected) {
      burstInjected = true;

      for (int i = 0; i < BURST_SIZE; i++) {
        if (!bufferFull()) {
          shared.eventBuffer[shared.writeIndex] = micros();
          shared.writeIndex = (shared.writeIndex + 1) % BUFFER_SIZE;
          shared.producedCount++;
          shared.backlog++;
        } else {
          shared.missedCount++;
        }
        delay(1);
      }
    }

    vTaskDelay(1);
  }
}

// --------------------------------------------------
// Consumer task (Core 1)
// STARTER = intentionally bad
// Student fixes this section
// --------------------------------------------------
void consumerTask(void *pvParameters) {
  while (true) {
    if (runActive && !bufferEmpty()) {
      volatile uint32_t checksum = 0;

      for (int i = 0; i < BUFFER_SIZE; i++) {
        checksum += shared.eventBuffer[i];
      }

      uint32_t eventTime = shared.eventBuffer[shared.readIndex];
      (void)eventTime;

      shared.readIndex = (shared.readIndex + 1) % BUFFER_SIZE;
      shared.consumedCount++;
      shared.backlog--;

      ledPulseRequested = true;
      ledPulseStart = millis();

      for (int i = 0; i < 300000; i++) {
        checksum ^= (i + shared.backlog);
      }

      if (shared.backlog > 0) {
        checksum += shared.producedCount;
        checksum += shared.consumedCount;
      }
    }

    vTaskDelay(1);
  }
}

// --------------------------------------------------
// Status / serial commands
// --------------------------------------------------
void printStatus() {
  serialLine("=== CECS 460 Chapter 3 Status ===");
  serialLine("FW        : " FW_VERSION " (" FW_DATE ")");
  serialLine("Device ID : " + g_deviceId);
  serialLine("MAC       : " + g_mac);
  serialLine("Slot      : " + (g_slot > 0 ? String(g_slot) : String("unassigned")));
  serialLine("Token     : " + (g_token.length() ? g_token : String("none")));
  serialLine("Student   : " + (g_studentId.length() ? g_studentId : String("none")));
  serialLine("Wi-Fi     : " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
  if (WiFi.status() == WL_CONNECTED) {
    serialLine("IP        : " + WiFi.localIP().toString());
    serialLine("RSSI      : " + String(WiFi.RSSI()) + " dBm");
  }
  serialLine("MQTT      : " + String(mqtt.connected() ? "Connected" : "Disconnected"));
  serialLine("Lab1 sent : " + String(g_lab1Submitted ? "YES" : "NO"));
  serialLine("Lab2 sent : " + String(g_lab2Submitted ? "YES" : "NO"));
  serialLine("Verbose   : " + String(g_verbose ? "ON" : "OFF"));
  serialLine("==============================");
}

void handleSerialCommand(const String& raw) {
  String cmd = raw;
  cmd.trim();
  if (!cmd.length()) return;

  // reflection answer
  if (cmd != "help" && cmd != "status" && cmd != "version" &&
      !cmd.startsWith("set ") && cmd != "clear assignment" &&
      !cmd.startsWith("verbose") && cmd != "submit lab1") {
    serialLine("[q_lab2] Submitting answer: \"" + cmd + "\"");
    publishReflectionAnswer(cmd);
    return;
  }

  serialLine("> " + cmd);

  if (cmd == "help") {
    serialLine("Commands: help, status, version, verbose on/off, submit lab1");
    serialLine("  set ssid/pass/student/device <val>, clear assignment");
    serialLine("  any other input = q_lab2 reflection answer");
  }
  else if (cmd == "status") { printStatus(); }
  else if (cmd == "version") { serialLine("FW: " FW_VERSION " " FW_DATE); }
  else if (cmd == "verbose on") {
    g_verbose = true;
    savePrefBool("verbose", true);
    serialLine("Verbose: ON");
  }
  else if (cmd == "verbose off") {
    g_verbose = false;
    savePrefBool("verbose", false);
    serialLine("Verbose: OFF");
  }
  else if (cmd == "clear assignment") {
    clearAssignment();
  }
  else if (cmd == "submit lab1") {
    publishLabAnswer();
  }
  else if (cmd.startsWith("set ssid ")) {
    g_ssid = cmd.substring(9);
    savePref("ssid", g_ssid);
    serialLine("SSID: " + g_ssid);
  }
  else if (cmd.startsWith("set pass ")) {
    g_pass = cmd.substring(9);
    savePref("pass", g_pass);
    serialLine("Pass updated");
  }
  else if (cmd.startsWith("set student ")) {
    g_studentId = cmd.substring(12);
    savePref("student", g_studentId);
    serialLine("Student: " + g_studentId);
  }
  else if (cmd.startsWith("set device ")) {
    g_deviceId = cmd.substring(11);
    savePref("deviceId", g_deviceId);
    serialLine("Device: " + g_deviceId);
  }
  else {
    serialLine("Unknown command (type 'help')");
  }
}

// --------------------------------------------------
// Setup
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  serialLine("");
  serialLine("╔══════════════════════════════════════════╗");
  serialLine("║  CECS 460 Ch 3 — Core Contention Lab    ║");
  serialLine("║  Firmware v" FW_VERSION "  " FW_DATE "              ║");
  serialLine("╚══════════════════════════════════════════╝");

  loadPrefs();

  // FORCE these for debugging right now
  g_ssid = "CECS";
  g_pass = "CECS-Classroom";
  g_mqttHost = "192.168.8.217";
  g_mqttPort = 1883;

  WiFi.mode(WIFI_STA);
  delay(50);
  g_mac = getMac();

  if (!g_deviceId.length()) {
    g_deviceId = "esp32_" + g_mac.substring(max(0, (int)g_mac.length() - 6));
    savePref("deviceId", g_deviceId);
  }

  serialLine("[Boot] MAC: " + g_mac + "  Device: " + g_deviceId);
  if (g_slot > 0) serialLine("[Boot] Saved slot: " + String(g_slot));

  IPAddress brokerIP(192, 168, 8, 217);
  mqtt.setServer(brokerIP, 1883);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(10);

  serialLine("[Boot] Ready — connecting to network.");
  serialLine("[Boot] Press button to start a run after connection.");
  serialLine("[Boot] Type 'help' for commands.");

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

// --------------------------------------------------
// Loop
// --------------------------------------------------
void loop() {
  // Serial input
  if (Serial.available()) {
    static String buf;
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (buf.length()) {
          handleSerialCommand(buf);
          buf = "";
        }
      } else {
        buf += c;
      }
    }
  }

  // network
  checkWifi();

  if (WiFi.status() == WL_CONNECTED) {
    checkMqtt();
    if (mqtt.connected()) {
      mqtt.loop();
      if (!g_announced) announce();
    }
  }

  // button handling
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    unsigned long now = millis();

    if ((now - lastPressTime) > DEBOUNCE_MS && !runActive) {
      lastPressTime = now;

      runNumber++;
      resetRunState();

      runActive = true;
      runStartMs = millis();
      g_lab1Submitted = false;

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

  // LED pulse
  if (ledPulseRequested) {
    digitalWrite(LED_PIN, HIGH);
    if (millis() - ledPulseStart >= LED_PULSE_MS) {
      digitalWrite(LED_PIN, LOW);
      ledPulseRequested = false;
    }
  }

  // end run + auto-submit q_lab1
  if (runActive && (millis() - runStartMs >= RUN_WINDOW_MS)) {
    runActive = false;
    printRunSummary();

    if (mqtt.connected() && g_slot > 0) {
      publishLabAnswer();
    } else {
      serialLine("[MQTT] Not connected or no slot — q_lab1 not sent");
    }

    serialLine("Type your q_lab2 reflection and press Enter.");
    serialLine("Press button again for a new run.");
  }

  // heartbeat
  unsigned long now = millis();
  if (now - g_lastHB >= HEARTBEAT_MS) {
    g_lastHB = now;
    publishStatus();
  }

  delay(5);
}
