#ifndef LAB_CONFIG_H
#define LAB_CONFIG_H

#include <Arduino.h>

// ============================================================
// WiFi settings
// Students/instructor may edit these.
// ============================================================
const char* WIFI_SSID = "CECS460Chapter3";
const char* WIFI_PASSWORD = "460Chapter3";

// ============================================================
// Student setting
// Students may edit this.
// ============================================================
const char* STUDENT_ID = "ronve";

// ============================================================
// Classroom website/server settings
// Instructor usually edits these if the server IP changes.
// ============================================================
const char* SERVER_IP = "192.168.8.217";
const int SERVER_PORT = 5000;

const char* CLASS_ID = "cecs460";
const char* CHAPTER_ID = "ch3";

// ============================================================
// MQTT settings
// ============================================================
const char* MQTT_HOST = "192.168.8.217";
const int MQTT_PORT = 1883;

// Assignment topic prefix used by the classroom server.
#define COURSE "C460"

// Lesson topic used by the classroom server for JSON answers.
const char* LESSON_TOPIC = "c460_ch3";

// Raw fallback topic for q_lab1 telemetry.
const char* MQTT_TOPIC_RAW = "cecs460/ch3/q_lab1";

// ============================================================
// Hardware pins
// ============================================================
const int LED_PIN = 2;
const int BUTTON_PIN = 4;

// ============================================================
// Test settings
// ============================================================
const uint32_t LED_PERIOD_MS = 250;
const uint32_t TEST_DURATION_MS = 10000;
const uint32_t JITTER_FAIL_US = 20000;

#endif
