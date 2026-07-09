// SPDX-License-Identifier: MIT
//
// Motion Security Monitor -- MCU sketch (STM32U585 / Zephyr side)
//
// Responsibilities (kept here, not in Python, because this needs tight,
// jitter-free timing):
//   - Read the PIR sensor and tell Python when motion starts
//     (Python then "wakes up" the camera/AI pipeline for a while).
//   - Ping the ultrasonic sensor on a fixed schedule and stream the
//     distance to Python for the dashboard.
//   - Drive the buzzer directly:
//       * distance < 50 cm  -> slow beep (proximity alert), decided locally
//       * Python confirms "person" AND distance < 50 cm -> fast beep,
//         escalated via a Bridge call from the Linux side.
//
// Wiring (change the pin numbers below to match your build):
//   PIR sensor   OUT -> D2
//   Ultrasonic   TRIG -> D4
//   Ultrasonic   ECHO -> D5
//   Buzzer       (+)  -> D3   (needs to be a PWM-capable pin for tone())
//
// NOTE: Arduino_RouterBridge ships with App Lab, no library install needed.

#include "Arduino_RouterBridge.h"

const int PIR_PIN = 2;
const int TRIG_PIN = 4;
const int ECHO_PIN = 5;
const int BUZZER_PIN = 3;

const float ALERT_DISTANCE_CM = 50.0;
const unsigned long DISTANCE_INTERVAL_MS = 150;  // ultrasonic ping rate
const unsigned long MOTION_RETRIGGER_MS = 3000;   // debounce repeat motion events

// 0 = idle, 1 = proximity only (slow beep), 2 = person confirmed nearby (fast beep)
volatile int alertLevel = 0;

bool lastPirState = LOW;
unsigned long lastMotionNotify = 0;
unsigned long lastDistanceCheck = 0;
unsigned long lastBeepToggle = 0;
bool beepOn = false;

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 30 ms timeout ~= 5 m max range; pulseIn returns 0 on timeout
  long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return -1.0;          // no echo / out of range
  return duration * 0.0343f / 2.0f;        // speed of sound (cm/us), round trip
}

// Registered with the Bridge below. Python calls this (Bridge.call) once
// its vision pipeline confirms a person AND the last known distance is
// under the threshold, escalating the buzzer to the urgent pattern.
void set_alert_level(int level) {
  if (level > alertLevel) {
    alertLevel = level;
  }
}

void setup() {
  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(115200);

  Bridge.begin();
  Bridge.provide("set_alert_level", set_alert_level);
}

void updateBuzzer() {
  if (alertLevel == 0) {
    noTone(BUZZER_PIN);
    beepOn = false;
    return;
  }

  unsigned long beepPeriodMs = (alertLevel == 2) ? 150 : 400; // faster = more urgent
  if (millis() - lastBeepToggle >= beepPeriodMs) {
    lastBeepToggle = millis();
    beepOn = !beepOn;
    if (beepOn) {
      tone(BUZZER_PIN, alertLevel == 2 ? 2500 : 1800);
    } else {
      noTone(BUZZER_PIN);
    }
  }
}

void loop() {
  // --- PIR motion detection: notify Python on the rising edge only ---
  bool pirState = digitalRead(PIR_PIN);
  if (pirState == HIGH && lastPirState == LOW &&
      millis() - lastMotionNotify > MOTION_RETRIGGER_MS) {
    Bridge.notify("motion_detected");
    lastMotionNotify = millis();
  }
  lastPirState = pirState;

  // --- Ultrasonic distance, streamed to Python + drives the base alert ---
  if (millis() - lastDistanceCheck >= DISTANCE_INTERVAL_MS) {
    lastDistanceCheck = millis();
    float distance = readDistanceCm();
    Bridge.notify("distance_update", distance);

    if (distance > 0 && distance < ALERT_DISTANCE_CM) {
      if (alertLevel == 0) alertLevel = 1;   // proximity-only beep
    } else {
      alertLevel = 0;                        // out of range: clear any alert
    }
  }

  updateBuzzer();
}
