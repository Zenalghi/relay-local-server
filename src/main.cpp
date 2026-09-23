/**
 * @file main.cpp
 * @brief R-Sync ESP32 Local Server Firmware (Combined 4 Relays + 6 Servos + Timers + Scheduler)
 * @author Zenalghi
 *
 * Repositories:
 * - Firmware ESP32: https://github.com/Zenalghi/relay-local-server
 * - Flutter Client: https://github.com/Zenalghi/r_sync_app
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

// WiFiManager includes WebServer.h which uses http_parser sequential enums (0,1,2,3...).
// ESPAsyncWebServer requires powers of 2 (bitmasks) for method filtering.
#undef HTTP_GET
#undef HTTP_POST
#undef HTTP_DELETE
#undef HTTP_PUT
#undef HTTP_PATCH
#undef HTTP_HEAD
#undef HTTP_OPTIONS
#undef HTTP_ANY
#define HTTP_GET     0b00000001
#define HTTP_POST    0b00000010
#define HTTP_DELETE  0b00000100
#define HTTP_PUT     0b00001000
#define HTTP_PATCH   0b00010000
#define HTTP_HEAD    0b00200000
#define HTTP_OPTIONS 0b01000000
#define HTTP_ANY     0b01111111

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <time.h>
#include <ArduinoOTA.h>

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "change-me"
#endif

// ---------------------------------------------------------------- Pins
#define NUM_RELAYS 4
const uint8_t RELAY_PINS[NUM_RELAYS] = {4, 16, 17, 5}; // Relay 1, 2, 3, 4

#define NUM_SWITCHES 3
#define NUM_SERVOS 6
// Switch A: Servo 0 (ON) Pin 14, Servo 1 (OFF) Pin 27
// Switch B: Servo 2 (ON) Pin 26, Servo 3 (OFF) Pin 25
// Switch C: Servo 4 (ON) Pin 33, Servo 5 (OFF) Pin 32
const uint8_t SERVO_PINS[NUM_SERVOS] = {14, 27, 26, 25, 33, 32};

#define BUTTON_PIN 0
#define OLED_SDA 21
#define OLED_SCL 22

// ---------------------------------------------------------------- Relay Polarity
bool activeLow = true;
uint8_t relayOnLevel = LOW;
uint8_t relayOffLevel = HIGH;

// ---------------------------------------------------------------- OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledConnected = false;
volatile bool ota_updating = false;
int displayPage = 0; // 0: Status, 1: Relay Sched, 2: Switch Sched, 3: Timers

// ---------------------------------------------------------------- Servos & Calibration
Servo servos[NUM_SERVOS];
uint8_t restAngle = 90;
uint8_t pressAngle = 0;
uint16_t pressDurationMs = 400;

// Switch state memory (0: OFF, 1: ON, -1: Unknown)
int switchStates[NUM_SWITCHES] = {0, 0, 0};

// Non-blocking servo pulse structure
struct ServoAction {
  uint8_t servoIdx;
  uint8_t targetAngle;
  uint32_t startTimeMs;
  uint32_t durationMs;
  bool active;
  bool returningToRest;
};

#define MAX_SERVO_ACTIONS 6
ServoAction servoActions[MAX_SERVO_ACTIONS];

// ---------------------------------------------------------------- Scheduler
struct Job {
  uint8_t hour;
  uint8_t minute;
  bool action; // true = ON, false = OFF
  bool enabled;
};

#define MAX_JOBS 4
// 4 Relays + 3 Switches = 7 channels
// Index 0..3 -> Relay 1..4
// Index 4..6 -> Switch A..C (Index 4: Switch A, 5: Switch B, 6: Switch C)
Job channelJobs[7][MAX_JOBS];

int lastEvaluatedMinute = -1;
bool ntpSynced = false;

// ---------------------------------------------------------------- Timer Engine
#define MAX_TIMERS 10

struct TimerItem {
  int id;
  uint32_t totalDurationSec;
  uint32_t remainingSec;
  bool paused;
  bool active;
  bool invertOnStartEnd; // If true: set opposite state on start, target state on finish
  bool targetAction;     // true = ON, false = OFF
  bool targetRelays[NUM_RELAYS];   // true if this relay is selected
  bool targetSwitches[NUM_SWITCHES]; // true if this switch is selected
};

TimerItem timers[MAX_TIMERS];
int nextTimerId = 1;

// ---------------------------------------------------------------- Objects & Globals
AsyncWebServer server(80);
Preferences preferences;

bool lastButtonState = HIGH;
unsigned long buttonPressTime = 0;
bool isHolding = false;
unsigned long lastOledUpdate = 0;

// Function declarations
void updateOLED();
void setRelay(int channel, bool state);
bool getRelay(int channel);
void triggerSwitch(int switchIdx, bool turnOn);
void runServoSelfTest();

// ---------------------------------------------------------------- Polarity Load/Save
void applyPolarity(bool isActiveLow) {
  activeLow = isActiveLow;
  if (activeLow) {
    relayOnLevel = LOW;
    relayOffLevel = HIGH;
  } else {
    relayOnLevel = HIGH;
    relayOffLevel = LOW;
  }
}

void loadPolarity() {
  preferences.begin("cfg", true);
  bool stored = preferences.getBool("activeLow", true);
  restAngle = preferences.getUChar("restAngle", 90);
  pressAngle = preferences.getUChar("pressAngle", 0);
  pressDurationMs = preferences.getUShort("pressDur", 400);
  preferences.end();
  applyPolarity(stored);
}

void savePolarity() {
  preferences.begin("cfg", false);
  preferences.putBool("activeLow", activeLow);
  preferences.end();
}

void saveServoConfig() {
  preferences.begin("cfg", false);
  preferences.putUChar("restAngle", restAngle);
  preferences.putUChar("pressAngle", pressAngle);
  preferences.putUShort("pressDur", pressDurationMs);
  preferences.end();
}

// ---------------------------------------------------------------- SVG Logo
const char *custom_svg_logo = R"rawliteral(
<div style="text-align:center;">
<svg xmlns="http://www.w3.org/2000/svg" width="100px" height="100px" viewBox="0 0 200.28 166.5">
 <g id="Layer_1">
  <path fill="#178697" d="M21.24 40.87c0.43,0.95 -0.52,0.95 4.13,0.94l14.69 -0.01c6.39,-0.01 12.78,-0.03 19.17,-0.06l57.93 -0.05c7.68,-0.03 11.68,-1.19 15.83,3.35 4.3,4.7 13.15,10.29 7.47,18.29 -0.92,1.29 -3.82,4.43 -5.13,4.95 -3.54,2.95 -6.37,6.91 -10.16,10.33l-20.61 21.05c-1.6,1.86 -3.83,3.66 -5.38,5.22 -0.91,0.92 -1.52,1.97 -2.46,2.83 -1.41,1.28 -1.32,0.89 -2.44,2.5 0.37,0.69 -0.47,0.76 2.87,0.69 9.82,-0.2 46.18,0.39 49.18,-0.37l11.11 -11.05c2.08,-2.11 3.48,-3.39 5.72,-5.68 6.06,-6.19 17.91,-17.46 22.65,-23.07 6.78,-8.05 4.75,-19.96 -0.54,-25.34l-5.8 -5.67c-0.64,-0.79 -0.46,-0.82 -1.26,-1.56l-28.47 -28.22c-0.67,-0.61 -0.81,-0.75 -1.46,-1.42 -3.07,-3.13 -5.29,-5.43 -10.19,-6.99 -6.52,-2.09 -14.09,-1.41 -21.33,-1.44 -15.21,-0.05 -30.48,-0.1 -45.69,0.01 -10.6,0.08 -7.21,-1.12 -15.39,6.98 -1.87,1.85 -3.99,3.63 -5.99,5.45 -2.16,1.95 -3.48,4.02 -5.57,5.86l-22.87 22.48z"/>
  <path fill="#2D3641" d="M-0 108.01c0.43,2.21 -0.17,0.34 1.05,1.67 0.36,0.39 0.97,1.05 1.35,1.47 3.59,4.05 10.08,8.85 13.6,12.91 1.54,1.78 3.78,3.98 5.48,5.54 1.01,0.93 1.77,1.65 2.77,2.54l13.42 13.1c2.35,1.96 3.48,3.19 5.52,5.48 2.4,2.69 10.54,9.94 13.33,13.18 3.29,3.82 4.79,2.26 14.14,2.34 7.09,0.06 38.17,0.35 42.88,-0.21l-10.9 -11.11c-3.74,-3.01 -6.19,-6.8 -9.25,-9.09 -0.79,-0.59 -1.11,-0.98 -1.88,-1.83 -4.14,-4.59 -6.81,-6.54 -10.92,-11.1l-7.42 -7.27c-1.75,-2.3 -4.72,-4.65 -7.35,-7.32l-7.31 -7.39c-1.31,-1.54 -2.83,-2.3 -3.8,-3.99 0.86,-1.28 1.2,-1.21 2.35,-2.17l34.14 -34.4c0.87,-1 1.23,-1.87 2.48,-2.19 -0.4,-0.99 0.54,-1.09 -3.02,-1.03l-22.86 0.04c-8.93,0.05 -17.86,-0.07 -26.78,-0.02 -1.82,3.61 -3.98,3.35 -7.43,7.6l-10.11 10.27c-1.31,1.25 -1.63,1.08 -2.82,2.5 -1.52,1.82 -3.21,3.73 -5.07,5.13 -2.57,1.93 -3,2.85 -5.02,5.18l-10.56 10.17z"/>
  <path fill="#EC651C" d="M200.28 165.6l-52.78 -52.94c-0.8,-0.23 -0.79,-0.29 -2.26,-0.36l-42.35 -0.17c-1.07,-0 -2.25,-0.05 -3.29,-0.03 -0.51,0.01 -1.04,0 -1.54,0.14l-1.34 0.82c0.09,0.54 48,50.08 50.74,52.13 2.13,1.59 10.26,0.96 13.77,0.95 5.18,-0 36.99,0.54 39.05,-0.54z"/>
 </g>
</svg>
</div>
)rawliteral";

// ---------------------------------------------------------------- Relays & Servos Control
void setRelay(int channel, bool state) {
  if (channel < 1 || channel > NUM_RELAYS) return;
  digitalWrite(RELAY_PINS[channel - 1], state ? relayOnLevel : relayOffLevel);
}

bool getRelay(int channel) {
  if (channel < 1 || channel > NUM_RELAYS) return false;
  return digitalRead(RELAY_PINS[channel - 1]) == relayOnLevel;
}

void setRelayPolarityAndForceOff(bool isActiveLow) {
  applyPolarity(isActiveLow);
  for (int i = 1; i <= NUM_RELAYS; i++) {
    setRelay(i, false);
  }
  savePolarity();
}

void startServoMovement(uint8_t servoIdx, uint8_t targetAngle, uint16_t durationMs) {
  if (servoIdx >= NUM_SERVOS) return;
  servos[servoIdx].attach(SERVO_PINS[servoIdx], 500, 2400);
  servos[servoIdx].write(targetAngle);

  for (int i = 0; i < MAX_SERVO_ACTIONS; i++) {
    if (!servoActions[i].active || servoActions[i].servoIdx == servoIdx) {
      servoActions[i].servoIdx = servoIdx;
      servoActions[i].targetAngle = targetAngle;
      servoActions[i].startTimeMs = millis();
      servoActions[i].durationMs = durationMs;
      servoActions[i].active = true;
      servoActions[i].returningToRest = false;
      break;
    }
  }
}

void updateServos() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_SERVO_ACTIONS; i++) {
    if (servoActions[i].active) {
      if (now - servoActions[i].startTimeMs >= servoActions[i].durationMs) {
        uint8_t idx = servoActions[i].servoIdx;
        if (!servoActions[i].returningToRest) {
          // Move back to rest angle
          servos[idx].write(restAngle);
          servoActions[i].startTimeMs = now;
          servoActions[i].durationMs = pressDurationMs;
          servoActions[i].returningToRest = true;
        } else {
          // Finished returning to rest -> Detach
          servos[idx].detach();
          servoActions[i].active = false;
        }
      }
    }
  }
}

void triggerSwitch(int switchIdx, bool turnOn) {
  if (switchIdx < 0 || switchIdx >= NUM_SWITCHES) return;
  // Servo index for Switch A: ON=0, OFF=1. Switch B: ON=2, OFF=3. Switch C: ON=4, OFF=5.
  uint8_t servoIdx = (switchIdx * 2) + (turnOn ? 0 : 1);
  startServoMovement(servoIdx, pressAngle, pressDurationMs);
  switchStates[switchIdx] = turnOn ? 1 : 0;
}

void applyRestAngleImmediately() {
  for (int s = 0; s < NUM_SERVOS; s++) {
    servos[s].attach(SERVO_PINS[s], 500, 2400);
    servos[s].write(restAngle);
  }
  delay(300);
  for (int s = 0; s < NUM_SERVOS; s++) {
    servos[s].detach();
  }
}

void runServoSelfTest() {
  Serial.println("[SERVO] Running SAFE self test sequence (3 cycles)...");
  
  // Calculate safe test angle in the OPPOSITE direction of pressAngle
  // Example: if pressAngle=0 and restAngle=90, safe direction is towards 125-135 (away from switch)
  uint8_t safeTestAngle;
  if (pressAngle < restAngle) {
    int target = (int)restAngle + 35;
    safeTestAngle = (target > 170) ? 170 : (uint8_t)target;
  } else {
    int target = (int)restAngle - 35;
    safeTestAngle = (target < 10) ? 10 : (uint8_t)target;
  }

  // Sweep servos in safe direction switch by switch (sequential to avoid jamming or switch presses)
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int sw = 0; sw < NUM_SWITCHES; sw++) {
      uint8_t onServo = sw * 2;
      uint8_t offServo = sw * 2 + 1;

      servos[onServo].attach(SERVO_PINS[onServo], 500, 2400);
      servos[offServo].attach(SERVO_PINS[offServo], 500, 2400);

      servos[onServo].write(safeTestAngle);
      servos[offServo].write(safeTestAngle);
      delay(250);

      servos[onServo].write(restAngle);
      servos[offServo].write(restAngle);
      delay(250);

      servos[onServo].detach();
      servos[offServo].detach();
      delay(100);
    }
  }
  Serial.println("[SERVO] SAFE self test sequence complete.");
}

// ---------------------------------------------------------------- Scheduler Logic
void loadJobs() {
  preferences.begin("sched", true);
  for (int ch = 0; ch < 7; ch++) {
    String key = "ch" + String(ch);
    String json = preferences.getString(key.c_str(), "[]");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (!error) {
      JsonArray arr = doc.as<JsonArray>();
      for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
        channelJobs[ch][i].hour = arr[i]["h"] | 0;
        channelJobs[ch][i].minute = arr[i]["m"] | 0;
        channelJobs[ch][i].action = arr[i]["a"] | false;
        channelJobs[ch][i].enabled = arr[i]["e"] | false;
      }
    }
  }
  preferences.end();
}

void saveJobs() {
  preferences.begin("sched", false);
  for (int ch = 0; ch < 7; ch++) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_JOBS; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["h"] = channelJobs[ch][i].hour;
      obj["m"] = channelJobs[ch][i].minute;
      obj["a"] = channelJobs[ch][i].action;
      obj["e"] = channelJobs[ch][i].enabled;
    }
    String json;
    serializeJson(doc, json);
    String key = "ch" + String(ch);
    preferences.putString(key.c_str(), json);
  }
  preferences.end();
}

int getMinutesFromMidnight(int h, int m) {
  return h * 60 + m;
}

void applyChannelAction(int ch, bool action) {
  if (ch >= 0 && ch < 4) {
    // Relays 1..4
    setRelay(ch + 1, action);
  } else if (ch >= 4 && ch < 7) {
    // Switches A..C
    triggerSwitch(ch - 4, action);
  }
}

void checkSchedules(int h, int m) {
  int currentMin = getMinutesFromMidnight(h, m);
  if (currentMin == lastEvaluatedMinute) return;

  for (int ch = 0; ch < 7; ch++) {
    for (int i = 0; i < MAX_JOBS; i++) {
      if (channelJobs[ch][i].enabled && getMinutesFromMidnight(channelJobs[ch][i].hour, channelJobs[ch][i].minute) == currentMin) {
        applyChannelAction(ch, channelJobs[ch][i].action);
      }
    }
  }
  lastEvaluatedMinute = currentMin;
}

// ---------------------------------------------------------------- Timer Engine Logic
void applyTimerTargets(TimerItem &t, bool action) {
  for (int r = 0; r < NUM_RELAYS; r++) {
    if (t.targetRelays[r]) {
      setRelay(r + 1, action);
    }
  }
  for (int s = 0; s < NUM_SWITCHES; s++) {
    if (t.targetSwitches[s]) {
      triggerSwitch(s, action);
    }
  }
}

void tickTimers() {
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (timers[i].active && !timers[i].paused) {
      if (timers[i].remainingSec > 0) {
        timers[i].remainingSec--;
        if (timers[i].remainingSec == 0) {
          // Timer finished! Execute target action
          applyTimerTargets(timers[i], timers[i].targetAction);
          timers[i].active = false;
        }
      }
    }
  }
}

// ---------------------------------------------------------------- OLED Display Manager
void drawOledHeader(const char *title) {
  if (!oledConnected) return;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int len = strlen(title);
  int x = (128 - (len * 6)) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, 0);
  display.print(title);
  display.drawFastHLine(0, 9, 128, SSD1306_WHITE);
}

void drawOledFooter() {
  if (!oledConnected) return;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(46, 56);
  display.print("R-Sync");
}

void updateOLED() {
  if (!oledConnected) return;
  display.clearDisplay();

  if (displayPage == 0) {
    // PAGE 0: DEVICE STATUS
    drawOledHeader("- DEVICE STATUS -");

    display.setCursor(0, 12);
    display.print("IP: ");
    display.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Disconnected");

    display.setCursor(0, 22);
    display.print("Time: ");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      char timeStr[20];
      sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      display.println(timeStr);
    } else {
      display.println("Syncing...");
    }

    display.setCursor(0, 34);
    display.printf("R1:%s R2:%s R3:%s R4:%s\n",
                   getRelay(1) ? "ON" : "OFF", getRelay(2) ? "ON" : "OFF",
                   getRelay(3) ? "ON" : "OFF", getRelay(4) ? "ON" : "OFF");

    display.setCursor(0, 44);
    display.printf("SwA:%s SwB:%s SwC:%s\n",
                   switchStates[0] == 1 ? "ON" : "OFF",
                   switchStates[1] == 1 ? "ON" : "OFF",
                   switchStates[2] == 1 ? "ON" : "OFF");
  }
  else if (displayPage == 1) {
    // PAGE 1: RELAY SCHEDULES
    drawOledHeader("- RELAY SCHED -");
    int y = 12;
    for (int r = 0; r < 4; r++) {
      display.setCursor(0, y);
      display.printf("R%d:", r + 1);
      int activeCount = 0;
      for (int j = 0; j < MAX_JOBS; j++) {
        if (channelJobs[r][j].enabled) {
          display.printf(" %02d:%02d%c", channelJobs[r][j].hour, channelJobs[r][j].minute, channelJobs[r][j].action ? '+' : '-');
          activeCount++;
          if (activeCount >= 2) break; // fit on row
        }
      }
      if (activeCount == 0) display.print(" No Sched");
      y += 10;
    }
  }
  else if (displayPage == 2) {
    // PAGE 2: SWITCH SCHEDULES
    drawOledHeader("- SWITCH SCHED -");
    const char swNames[3] = {'A', 'B', 'C'};
    int y = 14;
    for (int s = 0; s < 3; s++) {
      int ch = 4 + s;
      display.setCursor(0, y);
      display.printf("Sw%c:", swNames[s]);
      int activeCount = 0;
      for (int j = 0; j < MAX_JOBS; j++) {
        if (channelJobs[ch][j].enabled) {
          display.printf(" %02d:%02d%c", channelJobs[ch][j].hour, channelJobs[ch][j].minute, channelJobs[ch][j].action ? '+' : '-');
          activeCount++;
          if (activeCount >= 2) break;
        }
      }
      if (activeCount == 0) display.print(" No Sched");
      y += 12;
    }
  }
  else if (displayPage == 3) {
    // PAGE 3: ACTIVE TIMERS
    drawOledHeader("- TIMERS LIST -");
    int activeTimersCount = 0;
    int y = 14;
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (timers[i].active) {
        activeTimersCount++;
        if (y < 50) {
          uint32_t sec = timers[i].remainingSec;
          uint32_t h = sec / 3600;
          uint32_t m = (sec % 3600) / 60;
          uint32_t s = sec % 60;
          display.setCursor(0, y);
          display.printf("T#%d: %02u:%02u:%02u %s", timers[i].id, h, m, s, timers[i].paused ? "[P]" : "[R]");
          y += 11;
        }
      }
    }
    if (activeTimersCount == 0) {
      display.setCursor(20, 26);
      display.print("No Active Timers");
    }
  }

  drawOledFooter();
  display.display();
}

void drawOledCountdown(int secondsRemaining) {
  if (!oledConnected) return;
  display.clearDisplay();
  drawOledHeader("- TOGGLE POLARITY -");

  display.setTextSize(1);
  display.setCursor(0, 14);
  display.println("Keep holding button");
  display.println("to switch polarity!");

  display.setCursor(32, 36);
  display.setTextSize(2);
  display.printf("in %ds", secondsRemaining);

  drawOledFooter();
  display.display();
}

void handleButtonPress() {
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    buttonPressTime = millis();
    isHolding = false;
  }
  else if (lastButtonState == LOW && currentButtonState == LOW) {
    unsigned long duration = millis() - buttonPressTime;
    if (duration >= 1000 && duration < 11000) {
      isHolding = true;
      int elapsedSeconds = (duration - 1000) / 1000;
      int remaining = 10 - elapsedSeconds;
      if (remaining < 0) remaining = 0;

      static int lastDisplayedSec = -1;
      if (lastDisplayedSec != remaining) {
        lastDisplayedSec = remaining;
        drawOledCountdown(remaining);
      }
    }
    else if (duration >= 11000) {
      setRelayPolarityAndForceOff(!activeLow);
      if (oledConnected) {
        display.clearDisplay();
        drawOledHeader("- TOGGLE POLARITY -");
        display.setTextSize(1);
        display.setCursor(0, 18);
        display.println("Polarity Changed!");
        display.printf("Mode: %s\n", activeLow ? "ACTIVE LOW" : "ACTIVE HIGH");
        display.println("Relays reset to OFF");
        drawOledFooter();
        display.display();
      }
      delay(2000);
      buttonPressTime = millis();
      isHolding = false;
      updateOLED();
    }
  }
  else if (lastButtonState == LOW && currentButtonState == HIGH) {
    unsigned long duration = millis() - buttonPressTime;
    if (!isHolding && duration < 1000) {
      displayPage = (displayPage + 1) % 4; // 4 Pages
      updateOLED();
    }
    else if (isHolding) {
      updateOLED();
    }
    isHolding = false;
  }
  lastButtonState = currentButtonState;
}

// ---------------------------------------------------------------- REST API Endpoints
void setupAPI() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      request->send(200);
    } else {
      request->send(404, "application/json", "{\"status\":\"Error\",\"message\":\"Not Found\"}");
    }
  });

  // 1. GET Capabilities (Smart Discovery)
  server.on("/api/capabilities", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["device_name"] = "R-Sync ESP32 Server";
    doc["version"] = "2.0.0";
    doc["relays_count"] = NUM_RELAYS;
    doc["switches_count"] = NUM_SWITCHES;
    doc["servos_count"] = NUM_SERVOS;
    doc["timer_feature"] = true;
    doc["max_timers"] = MAX_TIMERS;
    doc["scheduler_feature"] = true;
    doc["max_schedules_per_channel"] = MAX_JOBS;
    doc["servo_config_feature"] = true;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 2. GET System Status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["ip"] = WiFi.localIP().toString();
    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeStringBuff[50];
      strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
      doc["time"] = String(timeStringBuff);
    } else {
      doc["time"] = "Not Synced";
    }

    doc["activeLow"] = activeLow;
    doc["displayPage"] = displayPage;
    doc["restAngle"] = restAngle;
    doc["pressAngle"] = pressAngle;
    doc["pressDurationMs"] = pressDurationMs;

    // Relays
    JsonArray rArr = doc["relays"].to<JsonArray>();
    for (int r = 1; r <= NUM_RELAYS; r++) {
      rArr.add(getRelay(r) ? "ON" : "OFF");
    }

    // Switches
    JsonArray sArr = doc["switches"].to<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++) {
      sArr.add(switchStates[s] == 1 ? "ON" : "OFF");
    }

    // Active Timers
    JsonArray tArr = doc["timers"].to<JsonArray>();
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (timers[i].active) {
        JsonObject obj = tArr.add<JsonObject>();
        obj["id"] = timers[i].id;
        obj["totalDurationSec"] = timers[i].totalDurationSec;
        obj["remainingSec"] = timers[i].remainingSec;
        obj["paused"] = timers[i].paused;
        obj["invertOnStartEnd"] = timers[i].invertOnStartEnd;
        obj["targetAction"] = timers[i].targetAction ? "ON" : "OFF";
        
        JsonArray rTargets = obj["targetRelays"].to<JsonArray>();
        for (int r = 0; r < NUM_RELAYS; r++) rTargets.add(timers[i].targetRelays[r]);
        JsonArray sTargets = obj["targetSwitches"].to<JsonArray>();
        for (int s = 0; s < NUM_SWITCHES; s++) sTargets.add(timers[i].targetSwitches[s]);
      }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 3. POST /api/relay (Control Relay 1..4)
  server.on("/api/relay", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int channel = doc["channel"] | 0;
        String state = doc["state"] | "";
        if (channel >= 1 && channel <= NUM_RELAYS) {
          setRelay(channel, state == "ON");
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Relay\"}");
    });

  // 4. POST /api/switch (Control Wall Switch A..C / Index 0..2)
  server.on("/api/switch", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int switchIdx = doc["switch"] | -1;
        String state = doc["state"] | "";
        if (switchIdx >= 0 && switchIdx < NUM_SWITCHES) {
          triggerSwitch(switchIdx, state == "ON");
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Switch\"}");
    });

  // 5. POST /api/servo/test (Trigger 3x Self-Test movement)
  server.on("/api/servo/test", HTTP_ANY, [](AsyncWebServerRequest *request) {
    runServoSelfTest();
    request->send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Self test completed\"}");
  });

  // 6. POST /api/servo/config (Calibrate Rest & Press Angles)
  server.on("/api/servo/config", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        if (doc["restAngle"].is<uint8_t>()) restAngle = doc["restAngle"].as<uint8_t>();
        if (doc["pressAngle"].is<uint8_t>()) pressAngle = doc["pressAngle"].as<uint8_t>();
        if (doc["pressDurationMs"].is<uint16_t>()) pressDurationMs = doc["pressDurationMs"].as<uint16_t>();
        saveServoConfig();
        applyRestAngleImmediately();
        request->send(200, "application/json", "{\"status\":\"OK\"}");
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Servo Config\"}");
    });

  // 7. POST /api/relay/polarity
  server.on("/api/relay/polarity", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error && doc["activeLow"].is<bool>()) {
        setRelayPolarityAndForceOff(doc["activeLow"].as<bool>());
        request->send(200, "application/json", "{\"status\":\"OK\"}");
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Polarity Payload\"}");
    });

  // 8. GET & POST /api/schedule
  server.on("/api/schedules", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray chArray = doc.to<JsonArray>();
    for (int ch = 0; ch < 7; ch++) {
      JsonObject chObj = chArray.add<JsonObject>();
      chObj["channel"] = ch;
      JsonArray jArr = chObj["jobs"].to<JsonArray>();
      for (int i = 0; i < MAX_JOBS; i++) {
        JsonObject job = jArr.add<JsonObject>();
        job["h"] = channelJobs[ch][i].hour;
        job["m"] = channelJobs[ch][i].minute;
        job["a"] = channelJobs[ch][i].action ? "ON" : "OFF";
        job["e"] = channelJobs[ch][i].enabled;
      }
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/schedule", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int ch = doc["channel"] | -1;
        JsonArray arr = doc["jobs"].as<JsonArray>();
        if (ch >= 0 && ch < 7) {
          for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
            channelJobs[ch][i].hour = arr[i]["h"] | 0;
            channelJobs[ch][i].minute = arr[i]["m"] | 0;
            channelJobs[ch][i].action = (arr[i]["a"] == "ON");
            channelJobs[ch][i].enabled = arr[i]["e"] | false;
          }
          saveJobs();
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Schedule\"}");
    });

  // 9. Timer APIs: POST /api/timer/add, POST /api/timer/control
  server.on("/api/timer/add", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        // Find free timer slot
        int freeSlot = -1;
        for (int i = 0; i < MAX_TIMERS; i++) {
          if (!timers[i].active) {
            freeSlot = i;
            break;
          }
        }
        if (freeSlot != -1) {
          timers[freeSlot].id = nextTimerId++;
          timers[freeSlot].totalDurationSec = doc["durationSec"] | 0;
          timers[freeSlot].remainingSec = timers[freeSlot].totalDurationSec;
          timers[freeSlot].paused = false;
          timers[freeSlot].active = true;
          timers[freeSlot].invertOnStartEnd = doc["invertOnStartEnd"] | false;
          timers[freeSlot].targetAction = (doc["targetAction"] == "ON");

          JsonArray rArr = doc["targetRelays"].as<JsonArray>();
          for (int r = 0; r < NUM_RELAYS; r++) {
            timers[freeSlot].targetRelays[r] = (r < rArr.size()) ? rArr[r].as<bool>() : false;
          }
          JsonArray sArr = doc["targetSwitches"].as<JsonArray>();
          for (int s = 0; s < NUM_SWITCHES; s++) {
            timers[freeSlot].targetSwitches[s] = (s < sArr.size()) ? sArr[s].as<bool>() : false;
          }

          // If invertOnStartEnd is checked, execute opposite state on timer START!
          if (timers[freeSlot].invertOnStartEnd) {
            applyTimerTargets(timers[freeSlot], !timers[freeSlot].targetAction);
          }

          request->send(200, "application/json", "{\"status\":\"OK\",\"id\":" + String(timers[freeSlot].id) + "}");
          return;
        } else {
          request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Timer slots full\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Add Timer\"}");
    });

  server.on("/api/timer/control", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int id = doc["id"] | 0;
        String cmd = doc["command"] | ""; // "pause", "resume", "cancel"
        for (int i = 0; i < MAX_TIMERS; i++) {
          if (timers[i].active && timers[i].id == id) {
            if (cmd == "pause") timers[i].paused = true;
            else if (cmd == "resume") timers[i].paused = false;
            else if (cmd == "cancel") timers[i].active = false;
            request->send(200, "application/json", "{\"status\":\"OK\"}");
            return;
          }
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Timer not found\"}");
    });

  // 10. POST /api/display
  server.on("/api/display", HTTP_ANY, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error && doc["page"].is<int>()) {
        displayPage = doc["page"].as<int>() % 4;
      } else {
        displayPage = (displayPage + 1) % 4;
      }
      updateOLED();
      request->send(200, "application/json", "{\"status\":\"OK\",\"displayPage\":" + String(displayPage) + "}");
    });

  // 11. Reset WiFi
  server.on("/api/wifi/reset", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Resetting WiFi...\"}");
    delay(600);
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
  });
}

// ---------------------------------------------------------------- SETUP
void setup() {
  Serial.begin(115200);

  for (int r = 0; r < NUM_RELAYS; r++) {
    pinMode(RELAY_PINS[r], OUTPUT);
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadPolarity();
  for (int r = 1; r <= NUM_RELAYS; r++) {
    setRelay(r, false);
  }

  // OLED Init
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 OLED allocation failed"));
    oledConnected = false;
  } else {
    oledConnected = true;
    display.clearDisplay();
    drawOledHeader("- SYSTEM START -");
    display.setCursor(0, 20);
    display.println("Booting R-Sync...");
    display.println("Please wait...");
    drawOledFooter();
    display.display();
  }

  loadJobs();

  // Servo Self Test at Boot
  runServoSelfTest();

  // Instant WiFi Reset if BOOT button held down during startup
  bool forcePortal = false;
  if (digitalRead(BUTTON_PIN) == LOW) {
    forcePortal = true;
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- FACTORY RESET -");
      display.setCursor(0, 14);
      display.println("BOOT button pressed!");
      display.println("Resetting Wi-Fi...");
      display.println("Opening AP: R-Sync");
      drawOledFooter();
      display.display();
    }
    delay(1200);
  }

  WiFiManager wm;
  wm.setCustomHeadElement(custom_svg_logo);
  wm.setConnectTimeout(60);
  wm.setConfigPortalTimeout(120);

  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- CONFIG PORTAL -");
      display.setCursor(0, 14);
      display.println("WiFi Not Found!");
      display.println("SSID: R-Sync");
      display.println("IP: 192.168.4.1");
      drawOledFooter();
      display.display();
    }
  });

  bool wifiConfigured = false;
  wm.setSaveConfigCallback([&wifiConfigured]() { wifiConfigured = true; });

  if (oledConnected && !forcePortal) {
    display.clearDisplay();
    drawOledHeader("- WIFI CONNECT -");
    display.setCursor(0, 14);
    display.println("Connecting to WiFi...");
    display.println("Max wait: 60s");
    display.println("Hold BOOT: Reset");
    drawOledFooter();
    display.display();
  }

  bool res = false;
  if (forcePortal) {
    wm.resetSettings();
    res = wm.startConfigPortal("R-Sync");
  } else {
    res = wm.autoConnect("R-Sync");
  }

  if (!res) {
    Serial.println("Failed to connect or portal timeout");
    ESP.restart();
  }

  if (wifiConfigured || forcePortal) {
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- CONFIG SAVED -");
      display.setCursor(0, 18);
      display.println("WiFi Saved!");
      display.println("Restarting ESP32...");
      drawOledFooter();
      display.display();
    }
    delay(1500);
    ESP.restart();
  }

  Serial.println("WiFi connected");
  WiFi.setAutoReconnect(true);

  if (oledConnected) {
    display.clearDisplay();
    drawOledHeader("- WIFI CONNECTED -");
    display.setCursor(0, 18);
    display.println("Connected to Network!");
    display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    drawOledFooter();
    display.display();
  }

  configTime(7 * 3600, 0, "pool.ntp.org");

  // OTA Setup
  ArduinoOTA.setHostname("r-sync");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    ota_updating = true;
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- SYSTEM UPDATE -");
      display.setCursor(0, 20);
      display.println("Receiving update...");
      display.println("Please wait...");
      drawOledFooter();
      display.display();
    }
  });
  ArduinoOTA.onEnd([]() {
    ota_updating = false;
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (oledConnected) {
      int percentage = (progress / (total / 100));
      display.clearDisplay();
      drawOledHeader("- SYSTEM UPDATE -");
      display.drawRect(0, 18, 128, 10, SSD1306_WHITE);
      display.fillRect(0, 18, (128 * percentage) / 100, 10, SSD1306_WHITE);
      display.setCursor(24, 34);
      display.printf("Progress: %u%%", percentage);
      drawOledFooter();
      display.display();
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ota_updating = false;
  });
  ArduinoOTA.begin();

  setupAPI();
  server.begin();
}

// ---------------------------------------------------------------- LOOP
unsigned long lastTimerTick = 0;

void loop() {
  ArduinoOTA.handle();

  if (ota_updating) {
    delay(10);
    return;
  }

  handleButtonPress();
  updateServos();

  unsigned long currentMillis = millis();

  // Tick countdown timers every 1000ms
  if (currentMillis - lastTimerTick >= 1000) {
    lastTimerTick = currentMillis;
    tickTimers();
  }

  // Refresh OLED and check NTP schedules every 1000ms
  if (currentMillis - lastOledUpdate >= 1000) {
    lastOledUpdate = currentMillis;

    struct tm timeinfo;
    bool gotTime = getLocalTime(&timeinfo, 10);
    if (gotTime) {
      if (!ntpSynced) {
        ntpSynced = true;
      } else {
        checkSchedules(timeinfo.tm_hour, timeinfo.tm_min);
      }
    }

    if (!isHolding) {
      updateOLED();
    }
  }
}