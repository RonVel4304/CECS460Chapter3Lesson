#include "ClassroomClient.h"
#include "LabConfig.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

static String g_mac = "";
static String g_deviceId = "";
static int g_slot = -1;
static String g_token = "";
static String g_studentUrl = "";
static bool g_announced = false;

static String serialInput = "";

static String getMac();
static void connectWiFi();
static void connectMQTT();
static void mqttCallback(char* topic, byte* payload, unsigned int len);
static void handleSerialInput();
static void printHelp();

void classroomBegin() {
  WiFi.mode(WIFI_STA);

  g_mac = getMac();
  g_deviceId = "esp32_" + g_mac.substring(6);

  Serial.println();
  Serial.println("============== CLASSROOM CLIENT ==============");
  Serial.print("Device MAC: ");
  Serial.println(g_mac);
  Serial.print("Device ID: ");
  Serial.println(g_deviceId);
  Serial.println("==============================================");

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(768);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(10);

  connectWiFi();
  connectMQTT();
  classroomPrintAccessInfo();
}

void classroomLoop() {
  handleSerialInput();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();

  if (mqttClient.connected() && !g_announced) {
    classroomAnnounce();
  }
}

void classroomPublishAnswer(const char* stepId, const String& answer) {
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  Serial.println();
  Serial.println("============== CLASSROOM PUBLISH ==============");
  Serial.print("Step: ");
  Serial.println(stepId);
  Serial.print("Answer: ");
  Serial.println(answer);

  if (g_slot > 0 && g_token.length() > 0) {
    StaticJsonDocument<512> doc;
    doc["slot"] = g_slot;
    doc["token"] = g_token;
    doc["step"] = stepId;
    doc["answer"] = answer;

    String out;
    serializeJson(doc, out);

    String answerTopic = String(LESSON_TOPIC) + "/" + String(g_slot) + "/answer";

    Serial.print("JSON Topic: ");
    Serial.println(answerTopic);
    Serial.print("JSON Payload: ");
    Serial.println(out);

    bool okJson = mqttClient.publish(answerTopic.c_str(), out.c_str());

    if (okJson) {
      Serial.println("JSON answer publish succeeded.");
    } else {
      Serial.println("JSON answer publish failed.");
    }
  } else {
    Serial.println("No slot/token yet, skipping JSON answer publish.");
    Serial.println("Type 'status' to check assignment, or 'announce' to ask again.");
  }

  Serial.print("Raw Topic: ");
  Serial.println(MQTT_TOPIC_RAW);
  Serial.print("Raw Payload: ");
  Serial.println(answer);

  bool okRaw = mqttClient.publish(MQTT_TOPIC_RAW, answer.c_str());

  if (okRaw) {
    Serial.println("Raw publish succeeded.");
  } else {
    Serial.println("Raw publish failed.");
  }

  Serial.println("================================================");
}

void classroomAnnounce() {
  if (!mqttClient.connected()) {
    return;
  }

  StaticJsonDocument<512> doc;
  doc["mac"] = g_mac;
  doc["device_id"] = g_deviceId;
  doc["student_id"] = STUDENT_ID;
  doc["firmware"] = "cecs460_ch3_timing_lab";

  if (g_slot > 0) {
    doc["saved_slot"] = g_slot;
  }

  if (g_token.length() > 0) {
    doc["saved_token"] = g_token;
  }

  String out;
  serializeJson(doc, out);

  String topic = String(COURSE) + "/device/announce";
  bool ok = mqttClient.publish(topic.c_str(), out.c_str());

  Serial.println();
  Serial.println("============== DEVICE ANNOUNCE ==============");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(out);

  if (ok) {
    Serial.println("Announce sent. Waiting for slot/token assignment...");
    g_announced = true;
  } else {
    Serial.println("Announce failed.");
  }

  Serial.println("=============================================");
}

void classroomPrintAccessInfo() {
  String baseUrl = "http://";
  baseUrl += SERVER_IP;
  baseUrl += ":";
  baseUrl += String(SERVER_PORT);

  String fallbackUrl = baseUrl;
  fallbackUrl += "/";
  fallbackUrl += CLASS_ID;
  fallbackUrl += "/lesson/";
  fallbackUrl += CHAPTER_ID;

  if (g_slot > 0 && g_token.length() > 0) {
    fallbackUrl += "?slot=";
    fallbackUrl += String(g_slot);
    fallbackUrl += "&token=";
    fallbackUrl += g_token;
  } else {
    fallbackUrl += "?slot=WAITING_FOR_ASSIGNMENT&token=WAITING_FOR_ASSIGNMENT";
  }

  String loginUrl = baseUrl;
  loginUrl += "/";
  loginUrl += CLASS_ID;
  loginUrl += "/login";

  String instructorUrl = baseUrl;
  instructorUrl += "/";
  instructorUrl += CLASS_ID;
  instructorUrl += "/instructor";

  String projectorUrl = baseUrl;
  projectorUrl += "/";
  projectorUrl += CLASS_ID;
  projectorUrl += "/projector";

  Serial.println();
  Serial.println("============== STUDENT ACCESS INFO ==============");
  Serial.print("Server:      ");
  Serial.println(baseUrl);
  Serial.print("Login:       ");
  Serial.println(loginUrl);
  Serial.print("Instructor:  ");
  Serial.println(instructorUrl);
  Serial.print("Projector:   ");
  Serial.println(projectorUrl);
  Serial.println();

  if (g_slot > 0 && g_token.length() > 0) {
    Serial.print("Assigned Slot:  ");
    Serial.println(g_slot);
    Serial.print("Assigned Token: ");
    Serial.println(g_token);
    Serial.println();
    Serial.println("USE THIS STUDENT URL:");

    if (g_studentUrl.length() > 0) {
      Serial.println(g_studentUrl);
    } else {
      Serial.println(fallbackUrl);
    }
  } else {
    Serial.println("No slot/token assigned yet.");
    Serial.println("Waiting for server assignment...");
    Serial.println();
    Serial.println("The final URL will look like:");
    Serial.println("http://192.168.8.217:5000/cecs460/lesson/ch3?slot=7&token=abc123");
  }

  Serial.println();
  Serial.print("Class:       ");
  Serial.println(CLASS_ID);
  Serial.print("Chapter:     ");
  Serial.println(CHAPTER_ID);
  Serial.print("Student ID:  ");
  Serial.println(STUDENT_ID);
  Serial.print("Device ID:   ");
  Serial.println(g_deviceId);
  Serial.print("MAC:         ");
  Serial.println(g_mac);
  Serial.print("MQTT Host:   ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  Serial.print("Raw Topic:   ");
  Serial.println(MQTT_TOPIC_RAW);
  Serial.print("Lesson Topic:");
  Serial.println(LESSON_TOPIC);
  Serial.println("=================================================");
}

void classroomPrintStatus() {
  Serial.println();
  Serial.println("============== ESP32 STATUS ==============");
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  }

  Serial.print("MQTT: ");
  Serial.println(mqttClient.connected() ? "Connected" : "Disconnected");
  Serial.print("MAC: ");
  Serial.println(g_mac);
  Serial.print("Device ID: ");
  Serial.println(g_deviceId);
  Serial.print("Student ID: ");
  Serial.println(STUDENT_ID);
  Serial.print("Slot: ");
  Serial.println(g_slot > 0 ? String(g_slot) : "unassigned");
  Serial.print("Token: ");
  Serial.println(g_token.length() > 0 ? g_token : "none");
  Serial.print("Student URL: ");
  Serial.println(g_studentUrl.length() > 0 ? g_studentUrl : "not assigned yet");
  Serial.println("==========================================");

  classroomPrintAccessInfo();
}

bool classroomHasAssignment() {
  return g_slot > 0 && g_token.length() > 0;
}

int classroomSlot() {
  return g_slot;
}

String classroomToken() {
  return g_token;
}

String classroomStudentUrl() {
  return g_studentUrl;
}

String classroomDeviceId() {
  return g_deviceId;
}

String classroomMac() {
  return g_mac;
}

static String getMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return mac;
}

static void connectWiFi() {
  Serial.println();
  Serial.println("============== WIFI SETUP ==============");
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Server IP should be: ");
  Serial.println(SERVER_IP);
  Serial.println("========================================");
}

static void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println();
    Serial.println("============== MQTT SETUP ==============");
    Serial.print("Connecting to MQTT broker at ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    String clientId = g_deviceId + "_" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT connected!");

      String assignTopic = String(COURSE) + "/device/assign/" + g_mac;
      mqttClient.subscribe(assignTopic.c_str());

      String controlStepTopic = String(LESSON_TOPIC) + "/control/step";
      String controlBroadcastTopic = String(LESSON_TOPIC) + "/control/broadcast";

      mqttClient.subscribe(controlStepTopic.c_str());
      mqttClient.subscribe(controlBroadcastTopic.c_str());

      Serial.print("Subscribed to assignment topic: ");
      Serial.println(assignTopic);
      Serial.print("Subscribed to control topic: ");
      Serial.println(controlStepTopic);
      Serial.print("Subscribed to broadcast topic: ");
      Serial.println(controlBroadcastTopic);
      Serial.println("========================================");

      g_announced = false;
      classroomAnnounce();
    } else {
      Serial.print("MQTT connection failed. State code: ");
      Serial.println(mqttClient.state());
      Serial.println("Retrying in 2 seconds...");
      Serial.println("========================================");
      delay(2000);
    }
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String p = "";

  for (unsigned int i = 0; i < len; i++) {
    p += (char)payload[i];
  }

  Serial.println();
  Serial.println("============== MQTT MESSAGE ==============");
  Serial.print("Topic: ");
  Serial.println(t);
  Serial.print("Payload: ");
  Serial.println(p);
  Serial.println("==========================================");

  String assignTopic = String(COURSE) + "/device/assign/" + g_mac;

  if (t == assignTopic) {
    StaticJsonDocument<768> doc;
    DeserializationError err = deserializeJson(doc, p);

    if (err) {
      Serial.print("Could not parse assignment JSON: ");
      Serial.println(err.c_str());
      return;
    }

    g_slot = doc["slot"] | -1;
    g_token = doc["token"] | "";
    g_studentUrl = doc["student_url"] | "";

    Serial.println();
    Serial.println("==================================================");
    Serial.println("                 ACCESS ASSIGNED                 ");
    Serial.println("==================================================");
    Serial.print("Slot  : ");
    Serial.println(g_slot);
    Serial.print("Token : ");
    Serial.println(g_token);
    Serial.print("URL   : ");
    Serial.println(g_studentUrl);
    Serial.println("==================================================");

    classroomPrintAccessInfo();
  }

  String stepTopic = String(LESSON_TOPIC) + "/control/step";
  if (t == stepTopic) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, p) == DeserializationError::Ok) {
      int step = doc["step"] | -1;
      Serial.print("[Lesson] Active step: ");
      Serial.println(step);
    }
  }

  String broadcastTopic = String(LESSON_TOPIC) + "/control/broadcast";
  if (t == broadcastTopic) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, p) == DeserializationError::Ok) {
      String msg = doc["message"] | "";
      Serial.print("[Broadcast] ");
      Serial.println(msg);
    }
  }
}

static void handleSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      serialInput.trim();

      if (serialInput.length() > 0) {
        if (serialInput == "status") {
          classroomPrintStatus();
        } else if (serialInput == "announce") {
          g_announced = false;
          classroomAnnounce();
        } else if (serialInput == "url") {
          classroomPrintAccessInfo();
        } else if (serialInput == "help") {
          printHelp();
        } else {
          Serial.print("Unknown command: ");
          Serial.println(serialInput);
          Serial.println("Try: status");
        }
      }

      serialInput = "";
    } else {
      serialInput += c;
    }
  }
}

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  status   - print WiFi/MQTT/slot/token/student URL");
  Serial.println("  url      - reprint student URL");
  Serial.println("  announce - ask server for slot/token again");
  Serial.println("  help     - show this command list");
}
