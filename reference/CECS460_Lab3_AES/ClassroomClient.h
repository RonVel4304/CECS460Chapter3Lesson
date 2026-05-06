#ifndef CLASSROOM_CLIENT_H
#define CLASSROOM_CLIENT_H

#include <Arduino.h>

// Start WiFi, MQTT, device announcement, and URL assignment.
void classroomBegin();

// Keep WiFi/MQTT alive and handle Serial Monitor commands.
// Call this once every loop().
void classroomLoop();

// Publish a lab answer string for a specific question/step.
// Example stepId: "q_lab1"
void classroomPublishAnswer(const char* stepId, const String& answer);

// Reprint WiFi/MQTT/slot/token/student URL status.
void classroomPrintStatus();

// Reprint just the student access information.
void classroomPrintAccessInfo();

// Ask the server to assign/reassign slot/token.
void classroomAnnounce();

// Accessors for optional use in the lab sketch.
bool classroomHasAssignment();
int classroomSlot();
String classroomToken();
String classroomStudentUrl();
String classroomDeviceId();
String classroomMac();

#endif
