/**
 * @file main.cpp
 * @brief R-Sync ESP32 Local Server Firmware (Combined 4 Relays + 6 Servos + Timers + Scheduler)
 * @author Zenalghi
 * @version 3.0.0
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h> // Tambahan untuk pengatur Power Save WiFi
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
#define HTTP_GET 0b00000001
#define HTTP_POST 0b00000010
#define HTTP_DELETE 0b00000100
#define HTTP_PUT 0b00001000
#define HTTP_PATCH 0b00010000
#define HTTP_HEAD 0b00200000
#define HTTP_OPTIONS 0b01000000
#define HTTP_ANY 0b01111111

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
const uint8_t SERVO_PINS[NUM_SERVOS] = {14, 27, 26, 25, 33, 32};

#define BUTTON_PIN 0
#define OLED_SDA 21
#define OLED_SCL 22

// ---------------------------------------------------------------- Hardware Active Flags
bool relayActive[NUM_RELAYS] = {true, true, true, true};
bool switchActive[NUM_SWITCHES] = {true, true, true};

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
int displayPage = 0; // 0: Status, 1: Sched, 2: Timers

// ---------------------------------------------------------------- Servos & Calibration
Servo servos[NUM_SERVOS];
uint8_t restAngle = 90;
uint8_t pressAngle = 0;
uint16_t pressDurationMs = 400;

int switchStates[NUM_SWITCHES] = {0, 0, 0};

struct ServoAction
{
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
#define MAX_SCHEDULES 10

struct ScheduleEntry
{
  uint8_t hour;
  uint8_t minute;
  bool action;
  bool enabled;
  bool targetRelays[NUM_RELAYS];
  bool targetSwitches[NUM_SWITCHES];
};

ScheduleEntry schedules[MAX_SCHEDULES];
int lastEvaluatedMinute = -1;
bool ntpSynced = false;

// ---------------------------------------------------------------- Timer Engine
#define MAX_TIMERS 10

struct TimerItem
{
  int id;
  uint32_t totalDurationSec;
  uint32_t remainingSec;
  bool paused;
  bool active;
  bool finished; // Completed timers are kept & persisted as history
  bool invertOnStartEnd;
  bool targetAction;
  bool targetRelays[NUM_RELAYS];
  bool targetSwitches[NUM_SWITCHES];
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

void updateOLED();
void setRelay(int channel, bool state);
bool getRelay(int channel);
void triggerSwitch(int switchIdx, bool turnOn);
void runServoSelfTest();

volatile bool servoTestRequested = false;

// ---------------------------------------------------------------- Polarity Load/Save
void applyPolarity(bool isActiveLow)
{
  activeLow = isActiveLow;
  if (activeLow)
  {
    relayOnLevel = LOW;
    relayOffLevel = HIGH;
  }
  else
  {
    relayOnLevel = HIGH;
    relayOffLevel = LOW;
  }
}

void loadPolarity()
{
  // Gunakan false agar namespace dibuat otomatis jika belum ada di NVS
  preferences.begin("cfg", false);
  bool stored = preferences.getBool("activeLow", true);
  restAngle = preferences.getUChar("restAngle", 90);
  pressAngle = preferences.getUChar("pressAngle", 0);
  pressDurationMs = preferences.getUShort("pressDur", 400);
  preferences.end();
  applyPolarity(stored);
}

void savePolarity()
{
  preferences.begin("cfg", false);
  preferences.putBool("activeLow", activeLow);
  preferences.end();
}

void saveServoConfig()
{
  preferences.begin("cfg", false);
  preferences.putUChar("restAngle", restAngle);
  preferences.putUChar("pressAngle", pressAngle);
  preferences.putUShort("pressDur", pressDurationMs);
  preferences.end();
}

// ---------------------------------------------------------------- Hardware Active Config Load/Save
void loadHardwareConfig()
{
  preferences.begin("hwcfg", false);
  for (int i = 0; i < NUM_RELAYS; i++)
  {
    String key = "r" + String(i);
    relayActive[i] = preferences.getBool(key.c_str(), true);
  }
  for (int i = 0; i < NUM_SWITCHES; i++)
  {
    String key = "s" + String(i);
    switchActive[i] = preferences.getBool(key.c_str(), true);
  }
  preferences.end();
}

void saveHardwareConfig()
{
  preferences.begin("hwcfg", false);
  for (int i = 0; i < NUM_RELAYS; i++)
  {
    String key = "r" + String(i);
    preferences.putBool(key.c_str(), relayActive[i]);
  }
  for (int i = 0; i < NUM_SWITCHES; i++)
  {
    String key = "s" + String(i);
    preferences.putBool(key.c_str(), switchActive[i]);
  }
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
void setRelay(int channel, bool state)
{
  if (channel < 1 || channel > NUM_RELAYS)
    return;
  if (!relayActive[channel - 1])
    return;
  digitalWrite(RELAY_PINS[channel - 1], state ? relayOnLevel : relayOffLevel);
}

bool getRelay(int channel)
{
  if (channel < 1 || channel > NUM_RELAYS)
    return false;
  return digitalRead(RELAY_PINS[channel - 1]) == relayOnLevel;
}

void setRelayPolarityAndForceOff(bool isActiveLow)
{
  applyPolarity(isActiveLow);
  for (int i = 1; i <= NUM_RELAYS; i++)
  {
    digitalWrite(RELAY_PINS[i - 1], relayOffLevel);
  }
  savePolarity();
}

void startServoMovement(uint8_t servoIdx, uint8_t targetAngle, uint16_t durationMs)
{
  if (servoIdx >= NUM_SERVOS)
    return;
  servos[servoIdx].attach(SERVO_PINS[servoIdx], 500, 2400);
  servos[servoIdx].write(targetAngle);

  for (int i = 0; i < MAX_SERVO_ACTIONS; i++)
  {
    if (!servoActions[i].active || servoActions[i].servoIdx == servoIdx)
    {
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

void updateServos()
{
  uint32_t now = millis();
  for (int i = 0; i < MAX_SERVO_ACTIONS; i++)
  {
    if (servoActions[i].active)
    {
      if (now - servoActions[i].startTimeMs >= servoActions[i].durationMs)
      {
        uint8_t idx = servoActions[i].servoIdx;
        if (!servoActions[i].returningToRest)
        {
          servos[idx].write(restAngle);
          servoActions[i].startTimeMs = now;
          servoActions[i].durationMs = pressDurationMs;
          servoActions[i].returningToRest = true;
        }
        else
        {
          servos[idx].detach();
          servoActions[i].active = false;
        }
      }
    }
  }
}

void triggerSwitch(int switchIdx, bool turnOn)
{
  if (switchIdx < 0 || switchIdx >= NUM_SWITCHES)
    return;
  if (!switchActive[switchIdx])
    return;
  uint8_t servoIdx = (switchIdx * 2) + (turnOn ? 0 : 1);
  startServoMovement(servoIdx, pressAngle, pressDurationMs);
  switchStates[switchIdx] = turnOn ? 1 : 0;
}

void applyRestAngleImmediately()
{
  for (int s = 0; s < NUM_SERVOS; s++)
  {
    servos[s].attach(SERVO_PINS[s], 500, 2400);
    servos[s].write(restAngle);
  }
  delay(300);
  for (int s = 0; s < NUM_SERVOS; s++)
  {
    servos[s].detach();
  }
}

struct ServoTestState
{
  bool running = false;
  int cycle = 0;
  int sw = 0;
  int phase = 0;
  uint32_t phaseStartMs = 0;
  uint8_t safeAngle = 90;
};
ServoTestState selfTest;

void updateServoSelfTest()
{
  if (!selfTest.running)
    return;

  uint32_t now = millis();
  uint8_t onS = selfTest.sw * 2;
  uint8_t offS = selfTest.sw * 2 + 1;

  if (selfTest.phase == 0)
  {
    servos[onS].attach(SERVO_PINS[onS], 500, 2400);
    servos[offS].attach(SERVO_PINS[offS], 500, 2400);
    servos[onS].write(selfTest.safeAngle);
    servos[offS].write(selfTest.safeAngle);
    selfTest.phase = 1;
    selfTest.phaseStartMs = now;
  }
  else if (selfTest.phase == 1 && now - selfTest.phaseStartMs >= 250)
  {
    servos[onS].write(restAngle);
    servos[offS].write(restAngle);
    selfTest.phase = 2;
    selfTest.phaseStartMs = now;
  }
  else if (selfTest.phase == 2 && now - selfTest.phaseStartMs >= 250)
  {
    servos[onS].detach();
    servos[offS].detach();
    selfTest.sw++;
    if (selfTest.sw >= NUM_SWITCHES)
    {
      selfTest.sw = 0;
      selfTest.cycle++;
    }
    if (selfTest.cycle >= 3)
    {
      selfTest.running = false;
      Serial.println("[SERVO] SAFE self test sequence complete.");
    }
    else
    {
      selfTest.phase = 0;
    }
  }
}

void runServoSelfTest()
{
  if (selfTest.running)
    return;
  Serial.println("[SERVO] Scheduling SAFE self test sequence (3 cycles)...");
  uint8_t safeTestAngle;
  if (pressAngle < restAngle)
  {
    int target = (int)restAngle + 35;
    safeTestAngle = (target > 170) ? 170 : (uint8_t)target;
  }
  else
  {
    int target = (int)restAngle - 35;
    safeTestAngle = (target < 10) ? 10 : (uint8_t)target;
  }
  selfTest.safeAngle = safeTestAngle;
  selfTest.cycle = 0;
  selfTest.sw = 0;
  selfTest.phase = 0;
  selfTest.phaseStartMs = millis();
  selfTest.running = true;
}

// ---------------------------------------------------------------- Scheduler Load/Save
void loadSchedules()
{
  preferences.begin("sched2", false);
  String json = preferences.getString("entries", "[]");
  preferences.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err)
    return;

  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < MAX_SCHEDULES && i < (int)arr.size(); i++)
  {
    JsonObject obj = arr[i].as<JsonObject>();
    schedules[i].hour = obj["h"] | 0;
    schedules[i].minute = obj["m"] | 0;
    schedules[i].action = obj["a"] | false;
    schedules[i].enabled = obj["e"] | false;

    JsonArray rArr = obj["r"].as<JsonArray>();
    for (int r = 0; r < NUM_RELAYS; r++)
    {
      schedules[i].targetRelays[r] = (r < (int)rArr.size()) ? rArr[r].as<bool>() : false;
    }
    JsonArray sArr = obj["s"].as<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++)
    {
      schedules[i].targetSwitches[s] = (s < (int)sArr.size()) ? sArr[s].as<bool>() : false;
    }
  }
  Serial.println("[SCHED] Schedules loaded from Preferences.");
}

void saveSchedules()
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_SCHEDULES; i++)
  {
    JsonObject obj = arr.add<JsonObject>();
    obj["h"] = schedules[i].hour;
    obj["m"] = schedules[i].minute;
    obj["a"] = schedules[i].action;
    obj["e"] = schedules[i].enabled;
    JsonArray rArr = obj["r"].to<JsonArray>();
    for (int r = 0; r < NUM_RELAYS; r++)
      rArr.add(schedules[i].targetRelays[r]);
    JsonArray sArr = obj["s"].to<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++)
      sArr.add(schedules[i].targetSwitches[s]);
  }
  String json;
  serializeJson(doc, json);
  preferences.begin("sched2", false);
  preferences.putString("entries", json);
  preferences.end();
  Serial.println("[SCHED] Schedules saved to Preferences.");
}

int getMinutesFromMidnight(int h, int m) { return h * 60 + m; }

void applyChannelAction(bool targetRelays[], bool targetSwitches[], bool action)
{
  for (int r = 0; r < NUM_RELAYS; r++)
  {
    if (targetRelays[r])
      setRelay(r + 1, action);
  }
  for (int s = 0; s < NUM_SWITCHES; s++)
  {
    if (targetSwitches[s])
      triggerSwitch(s, action);
  }
}

void checkSchedules(int h, int m)
{
  int currentMin = getMinutesFromMidnight(h, m);
  if (currentMin == lastEvaluatedMinute)
    return;

  for (int i = 0; i < MAX_SCHEDULES; i++)
  {
    if (schedules[i].enabled &&
        getMinutesFromMidnight(schedules[i].hour, schedules[i].minute) == currentMin)
    {
      applyChannelAction(schedules[i].targetRelays, schedules[i].targetSwitches, schedules[i].action);
    }
  }
  lastEvaluatedMinute = currentMin;
}

// ---------------------------------------------------------------- Timer Engine Logic
void applyTimerTargets(TimerItem &t, bool action)
{
  for (int r = 0; r < NUM_RELAYS; r++)
  {
    if (t.targetRelays[r])
      setRelay(r + 1, action);
  }
  for (int s = 0; s < NUM_SWITCHES; s++)
  {
    if (t.targetSwitches[s])
      triggerSwitch(s, action);
  }
}

void saveTimers()
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < MAX_TIMERS; i++)
  {
    if (timers[i].active || timers[i].finished)
    {
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = timers[i].id;
      obj["total"] = timers[i].totalDurationSec;
      obj["rem"] = timers[i].remainingSec;
      obj["pause"] = timers[i].paused;
      obj["fin"] = timers[i].finished;
      obj["inv"] = timers[i].invertOnStartEnd;
      obj["act"] = timers[i].targetAction;
      JsonArray rArr = obj["r"].to<JsonArray>();
      for (int r = 0; r < NUM_RELAYS; r++)
        rArr.add(timers[i].targetRelays[r]);
      JsonArray sArr = obj["s"].to<JsonArray>();
      for (int s = 0; s < NUM_SWITCHES; s++)
        sArr.add(timers[i].targetSwitches[s]);
    }
  }
  String json;
  serializeJson(doc, json);
  preferences.begin("timers", false);
  preferences.putString("active", json);
  preferences.putInt("nextId", nextTimerId);
  preferences.end();
}

void loadTimers()
{
  preferences.begin("timers", false);
  String json = preferences.getString("active", "[]");
  nextTimerId = preferences.getInt("nextId", 1);
  preferences.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err)
    return;

  JsonArray arr = doc.as<JsonArray>();
  int slot = 0;
  for (JsonObject obj : arr)
  {
    if (slot >= MAX_TIMERS)
      break;
    timers[slot].id = obj["id"] | slot + 1;
    timers[slot].totalDurationSec = obj["total"] | 0;
    timers[slot].remainingSec = obj["rem"] | 0;
    timers[slot].paused = obj["pause"] | false;
    timers[slot].finished = obj["fin"] | false;
    timers[slot].invertOnStartEnd = obj["inv"] | false;
    timers[slot].targetAction = obj["act"] | false;
    timers[slot].active = (timers[slot].remainingSec > 0) && !timers[slot].finished;

    JsonArray rArr = obj["r"].as<JsonArray>();
    for (int r = 0; r < NUM_RELAYS; r++)
    {
      timers[slot].targetRelays[r] = (r < (int)rArr.size()) ? rArr[r].as<bool>() : false;
    }
    JsonArray sArr = obj["s"].as<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++)
    {
      timers[slot].targetSwitches[s] = (s < (int)sArr.size()) ? sArr[s].as<bool>() : false;
    }
    slot++;
  }
  Serial.printf("[TIMERS] Loaded %d active timers from Preferences.\n", slot);
}

void tickTimers()
{
  bool changed = false;
  for (int i = 0; i < MAX_TIMERS; i++)
  {
    if (timers[i].active && !timers[i].paused)
    {
      if (timers[i].remainingSec > 0)
      {
        timers[i].remainingSec--;
        if (timers[i].remainingSec == 0)
        {
          applyTimerTargets(timers[i], timers[i].targetAction);
          // Keep the timer as finished history instead of deleting it
          timers[i].active = false;
          timers[i].finished = true;
          changed = true;
        }
      }
    }
  }
  if (changed)
    saveTimers();
}

// ---------------------------------------------------------------- OLED Display Manager
void drawOledHeader(const char *title)
{
  if (!oledConnected)
    return;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int len = strlen(title);
  int x = (128 - (len * 6)) / 2;
  if (x < 0)
    x = 0;
  display.setCursor(x, 0);
  display.print(title);
  display.drawFastHLine(0, 9, 128, SSD1306_WHITE);
}

void drawOledFooter()
{
  if (!oledConnected)
    return;
  display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("R-Sync");

  // Non-blocking time retrieval
  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);

  if (ntpSynced && timeinfo && timeinfo->tm_year > (1970 - 1900))
  {
    char buf[9];
    sprintf(buf, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    display.setCursor(80, 56);
    display.print(buf);
  }
}

void updateOLED()
{
  if (!oledConnected)
    return;
  display.clearDisplay();

  if (displayPage == 0)
  {
    drawOledHeader("- DEVICE STATUS -");
    display.setCursor(0, 12);
    display.print("WiFi: ");
    display.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Disconnected");

    const char *rLabels[4] = {"R1", "R2", "R3", "R4"};
    for (int i = 0; i < NUM_RELAYS; i++)
    {
      if (!relayActive[i])
        continue;
      display.setCursor(i * 32, 24);
      bool on = getRelay(i + 1);
      display.printf("%s:%s", rLabels[i], on ? "ON" : "OF");
    }

    const char *swLabels[3] = {"A", "B", "C"};
    for (int i = 0; i < NUM_SWITCHES; i++)
    {
      if (!switchActive[i])
        continue;
      display.setCursor(i * 43, 36);
      bool on = (switchStates[i] == 1);
      display.printf("Sw%s:%s", swLabels[i], on ? "ON" : "OF");
    }
  }
  else if (displayPage == 1)
  {
    drawOledHeader("- SCHEDULES -");
    int activeCount = 0;
    int y = 12;
    for (int i = 0; i < MAX_SCHEDULES; i++)
    {
      if (schedules[i].enabled && y < 50)
      {
        display.setCursor(0, y);
        display.printf("%02d:%02d %s", schedules[i].hour, schedules[i].minute,
                       schedules[i].action ? "ON" : "OFF");
        y += 10;
        activeCount++;
      }
    }
    if (activeCount == 0)
    {
      display.setCursor(20, 26);
      display.print("No Active Schedules");
    }
  }
  else if (displayPage == 2)
  {
    // PAGE 2: ACTIVE & FINISHED TIMERS
    drawOledHeader("- TIMERS LIST -");
    int activeTimersCount = 0;
    int y = 14;
    for (int i = 0; i < MAX_TIMERS; i++)
    {
      if (timers[i].id > 0 && (timers[i].active || timers[i].finished || timers[i].totalDurationSec > 0))
      {
        activeTimersCount++;
        if (y < 50)
        {
          uint32_t sec = timers[i].remainingSec;
          uint32_t h = sec / 3600;
          uint32_t m = (sec % 3600) / 60;
          uint32_t s = sec % 60;
          display.setCursor(0, y);
          const char *tag = timers[i].finished ? "[Done]" : (timers[i].paused ? "[P]" : "[R]");
          display.printf("T#%d: %02u:%02u:%02u %s", timers[i].id, h, m, s, tag);
          y += 11;
        }
      }
    }
    if (activeTimersCount == 0)
    {
      display.setCursor(20, 26);
      display.print("No Timers");
    }
  }

  drawOledFooter();
  display.display();
}

void drawOledCountdown(int secondsRemaining)
{
  if (!oledConnected)
    return;
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

void handleButtonPress()
{
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW)
  {
    buttonPressTime = millis();
    isHolding = false;
  }
  else if (lastButtonState == LOW && currentButtonState == LOW)
  {
    unsigned long duration = millis() - buttonPressTime;
    if (duration >= 1000 && duration < 11000)
    {
      isHolding = true;
      int elapsedSeconds = (duration - 1000) / 1000;
      int remaining = 10 - elapsedSeconds;
      if (remaining < 0)
        remaining = 0;
      static int lastDisplayedSec = -1;
      if (lastDisplayedSec != remaining)
      {
        lastDisplayedSec = remaining;
        drawOledCountdown(remaining);
      }
    }
    else if (duration >= 11000)
    {
      setRelayPolarityAndForceOff(!activeLow);
      if (oledConnected)
      {
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
  else if (lastButtonState == LOW && currentButtonState == HIGH)
  {
    unsigned long duration = millis() - buttonPressTime;
    if (!isHolding && duration < 1000)
    {
      displayPage = (displayPage + 1) % 3;
      updateOLED();
    }
    else if (isHolding)
    {
      updateOLED();
    }
    isHolding = false;
  }
  lastButtonState = currentButtonState;
}

// ---------------------------------------------------------------- REST API Endpoints
void setupAPI()
{
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    if (request->method() == HTTP_OPTIONS) {
      request->send(200);
    } else {
      request->send(404, "application/json", "{\"status\":\"Error\",\"message\":\"Not Found\"}");
    } });

  server.on("/api/capabilities", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["device_name"]    = "R-Sync ESP32 Server";
    doc["version"]        = "3.0.0";
    doc["oled_connected"] = oledConnected;

    int activeRelayCount   = 0;
    int activeSwitchCount  = 0;
    JsonArray rActive = doc["active_relays"].to<JsonArray>();
    JsonArray sActive = doc["active_switches"].to<JsonArray>();
    for (int i = 0; i < NUM_RELAYS; i++) {
      rActive.add(relayActive[i]);
      if (relayActive[i]) activeRelayCount++;
    }
    for (int i = 0; i < NUM_SWITCHES; i++) {
      sActive.add(switchActive[i]);
      if (switchActive[i]) activeSwitchCount++;
    }
    doc["relays_count"]             = activeRelayCount;
    doc["switches_count"]           = activeSwitchCount;
    doc["servos_count"]             = NUM_SERVOS;
    doc["timer_feature"]            = true;
    doc["max_timers"]               = MAX_TIMERS;
    doc["scheduler_feature"]        = true;
    doc["max_schedules"]            = MAX_SCHEDULES;
    doc["servo_config_feature"]     = true;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["ip"]   = WiFi.localIP().toString();
    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";

    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    if (timeinfo && timeinfo->tm_year > (1970 - 1900)) {
      char buf[50];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
      doc["time"] = String(buf);
    } else {
      doc["time"] = "Not Synced";
    }

    doc["activeLow"]      = activeLow;
    doc["displayPage"]    = displayPage;
    doc["oledConnected"]  = oledConnected;
    doc["restAngle"]      = restAngle;
    doc["pressAngle"]     = pressAngle;
    doc["pressDurationMs"] = pressDurationMs;

    JsonArray rArr = doc["relays"].to<JsonArray>();
    for (int r = 1; r <= NUM_RELAYS; r++) {
      rArr.add(getRelay(r) ? "ON" : "OFF");
    }
    JsonArray rActiveArr = doc["relayActive"].to<JsonArray>();
    for (int r = 0; r < NUM_RELAYS; r++) rActiveArr.add(relayActive[r]);

    JsonArray sArr = doc["switches"].to<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++) {
      sArr.add(switchStates[s] == 1 ? "ON" : "OFF");
    }
    JsonArray sActiveArr = doc["switchActive"].to<JsonArray>();
    for (int s = 0; s < NUM_SWITCHES; s++) sActiveArr.add(switchActive[s]);

    JsonArray tArr = doc["timers"].to<JsonArray>();
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (timers[i].active || timers[i].finished) {
        JsonObject obj = tArr.add<JsonObject>();
        obj["id"]              = timers[i].id;
        obj["totalDurationSec"] = timers[i].totalDurationSec;
        obj["remainingSec"]    = timers[i].remainingSec;
        obj["paused"]          = timers[i].paused;
        obj["finished"]        = timers[i].finished;
        obj["invertOnStartEnd"] = timers[i].invertOnStartEnd;
        obj["targetAction"]    = timers[i].targetAction ? "ON" : "OFF";
        JsonArray rT = obj["targetRelays"].to<JsonArray>();
        for (int r = 0; r < NUM_RELAYS; r++) rT.add(timers[i].targetRelays[r]);
        JsonArray sT = obj["targetSwitches"].to<JsonArray>();
        for (int s = 0; s < NUM_SWITCHES; s++) sT.add(timers[i].targetSwitches[s]);
      }
    }

    JsonArray schArr = doc["schedules"].to<JsonArray>();
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      JsonObject obj = schArr.add<JsonObject>();
      obj["h"] = schedules[i].hour;
      obj["m"] = schedules[i].minute;
      obj["a"] = schedules[i].action ? "ON" : "OFF";
      obj["e"] = schedules[i].enabled;
      JsonArray rS = obj["r"].to<JsonArray>();
      for (int r = 0; r < NUM_RELAYS; r++) rS.add(schedules[i].targetRelays[r]);
      JsonArray sS = obj["s"].to<JsonArray>();
      for (int sw = 0; sw < NUM_SWITCHES; sw++) sS.add(schedules[i].targetSwitches[sw]);
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

  // 3. POST & GET /api/relay/polarity and /api/polarity (Registered BEFORE /api/relay to prevent prefix routing collision!)
  auto polarityHandler = [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
  {
    if (request->method() == HTTP_OPTIONS)
      return;
    if (len == 0)
      return;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (!error && (doc["activeLow"].is<bool>() ||
                   doc["activeLow"].is<int>() ||
                   doc["activeLow"].is<const char *>()))
    {
      bool al = true;
      if (doc["activeLow"].is<bool>())
      {
        al = doc["activeLow"].as<bool>();
      }
      else if (doc["activeLow"].is<int>())
      {
        al = (doc["activeLow"].as<int>() != 0);
      }
      else if (doc["activeLow"].is<const char *>())
      {
        String s = doc["activeLow"].as<const char *>();
        al = (s.equalsIgnoreCase("true") || s == "1");
      }
      setRelayPolarityAndForceOff(al);
      request->send(200, "application/json", "{\"status\":\"OK\",\"activeLow\":" + String(al ? "true" : "false") + "}");
      return;
    }
    request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Polarity Payload\"}");
  };

  auto polarityReqHandler = [](AsyncWebServerRequest *request)
  {
    if (request->method() == HTTP_OPTIONS)
    {
      request->send(200);
      return;
    }
    if (request->hasParam("activeLow"))
    {
      String p = request->getParam("activeLow")->value();
      bool al = (p.equalsIgnoreCase("true") || p == "1");
      setRelayPolarityAndForceOff(al);
      request->send(200, "application/json", "{\"status\":\"OK\",\"activeLow\":" + String(al ? "true" : "false") + "}");
      return;
    }
    if (request->method() == HTTP_GET)
    {
      request->send(200, "application/json", "{\"status\":\"OK\",\"activeLow\":" + String(activeLow ? "true" : "false") + "}");
      return;
    }
  };

  server.on("/api/relay/polarity", HTTP_ANY, polarityReqHandler, NULL, polarityHandler);
  server.on("/api/polarity", HTTP_ANY, polarityReqHandler, NULL, polarityHandler);

  // 4. POST /api/relay (Explicit exact match check to prevent subroute interception)
  server.on("/api/relay", HTTP_ANY, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      if (request->url() != "/api/relay") return; // Strictly ignore sub-paths
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
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Relay\"}"); });

  // 5. POST /api/switch
  server.on("/api/switch", HTTP_ANY, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      if (request->url() != "/api/switch") return;
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
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Switch\"}"); });

  // 6. POST /api/servo/test
  server.on("/api/servo/test", HTTP_ANY, [](AsyncWebServerRequest *request)
            {
    if (request->method() == HTTP_OPTIONS) { request->send(200); return; }
    runServoSelfTest();
    request->send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Self test scheduled\"}"); });

  // 7. POST /api/servo/config
  server.on("/api/servo/config", HTTP_ANY, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        if (doc["restAngle"].is<uint8_t>())    restAngle      = doc["restAngle"].as<uint8_t>();
        if (doc["pressAngle"].is<uint8_t>())   pressAngle     = doc["pressAngle"].as<uint8_t>();
        if (doc["pressDurationMs"].is<uint16_t>()) pressDurationMs = doc["pressDurationMs"].as<uint16_t>();
        saveServoConfig();
        applyRestAngleImmediately();
        request->send(200, "application/json", "{\"status\":\"OK\"}");
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Servo Config\"}"); });

  server.on("/api/schedules", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["h"] = schedules[i].hour;
      obj["m"] = schedules[i].minute;
      obj["a"] = schedules[i].action ? "ON" : "OFF";
      obj["e"] = schedules[i].enabled;
      JsonArray rS = obj["r"].to<JsonArray>();
      for (int r = 0; r < NUM_RELAYS; r++) rS.add(schedules[i].targetRelays[r]);
      JsonArray sS = obj["s"].to<JsonArray>();
      for (int s = 0; s < NUM_SWITCHES; s++) sS.add(schedules[i].targetSwitches[s]);
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

  server.on("/api/schedules", HTTP_POST, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        JsonArray arr = doc.as<JsonArray>();
        for (int i = 0; i < MAX_SCHEDULES; i++) {
          schedules[i] = ScheduleEntry();
          schedules[i].hour = 0; schedules[i].minute = 0;
          schedules[i].action = false; schedules[i].enabled = false;
          for (int r = 0; r < NUM_RELAYS; r++) schedules[i].targetRelays[r] = false;
          for (int s = 0; s < NUM_SWITCHES; s++) schedules[i].targetSwitches[s] = false;
        }
        int i = 0;
        for (JsonObject obj : arr) {
          if (i >= MAX_SCHEDULES) break;
          schedules[i].hour    = obj["h"] | 0;
          schedules[i].minute  = obj["m"] | 0;
          schedules[i].action  = (obj["a"] | String("OFF")) == "ON";
          schedules[i].enabled = obj["e"] | false;
          JsonArray rArr = obj["r"].as<JsonArray>();
          for (int r = 0; r < NUM_RELAYS; r++) {
            schedules[i].targetRelays[r] = (r < (int)rArr.size()) ? rArr[r].as<bool>() : false;
          }
          JsonArray sArr = obj["s"].as<JsonArray>();
          for (int s = 0; s < NUM_SWITCHES; s++) {
            schedules[i].targetSwitches[s] = (s < (int)sArr.size()) ? sArr[s].as<bool>() : false;
          }
          i++;
        }
        saveSchedules();
        request->send(200, "application/json", "{\"status\":\"OK\"}");
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Schedules\"}"); });

  server.on("/api/timer/add", HTTP_ANY, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int freeSlot = -1;
        for (int i = 0; i < MAX_TIMERS; i++) {
          if (!timers[i].active && !timers[i].finished) { freeSlot = i; break; }
        }
        if (freeSlot != -1) {
          timers[freeSlot].id               = nextTimerId++;
          timers[freeSlot].totalDurationSec = doc["durationSec"] | 0;
          timers[freeSlot].remainingSec     = timers[freeSlot].totalDurationSec;
          timers[freeSlot].paused           = false;
          timers[freeSlot].active           = true;
          timers[freeSlot].finished         = false;
          timers[freeSlot].invertOnStartEnd = doc["invertOnStartEnd"] | false;
          timers[freeSlot].targetAction     = (doc["targetAction"] == "ON");

          JsonArray rArr = doc["targetRelays"].as<JsonArray>();
          for (int r = 0; r < NUM_RELAYS; r++) {
            timers[freeSlot].targetRelays[r] = (r < (int)rArr.size()) ? rArr[r].as<bool>() : false;
          }
          JsonArray sArr = doc["targetSwitches"].as<JsonArray>();
          for (int s = 0; s < NUM_SWITCHES; s++) {
            timers[freeSlot].targetSwitches[s] = (s < (int)sArr.size()) ? sArr[s].as<bool>() : false;
          }

          if (timers[freeSlot].invertOnStartEnd) {
            applyTimerTargets(timers[freeSlot], !timers[freeSlot].targetAction);
          }

          saveTimers();
          request->send(200, "application/json",
                        "{\"status\":\"OK\",\"id\":" + String(timers[freeSlot].id) + "}");
          return;
        } else {
          request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Timer slots full\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Payload Add Timer\"}"); });

  server.on("/api/timer/control", HTTP_ANY, [](AsyncWebServerRequest *req)
            {
    if (req->method() == HTTP_OPTIONS) req->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      if (request->method() == HTTP_OPTIONS) return;
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        int id     = doc["id"] | 0;
        String cmd = doc["command"] | "";
        for (int i = 0; i < MAX_TIMERS; i++) {
          if (timers[i].id == id) {
            if (cmd == "pause") {
              timers[i].paused = true;
            } else if (cmd == "resume") {
              timers[i].paused = false;
            } else if (cmd == "cancel" || cmd == "stop") {
              timers[i].active = false;
              timers[i].paused = false;
              timers[i].finished = true;
              timers[i].remainingSec = 0;
            } else if (cmd == "start" || cmd == "restart") {
              timers[i].remainingSec = timers[i].totalDurationSec;
              timers[i].active = true;
              timers[i].paused = false;
              timers[i].finished = false;
              if (timers[i].invertOnStartEnd) {
                applyTimerTargets(timers[i], !timers[i].targetAction);
              }
            } else if (cmd == "remove") {
              timers[i] = TimerItem();
            }
            saveTimers();
            request->send(200, "application/json", "{\"status\":\"OK\"}");
            return;
          }
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Timer not found\"}"); });

  server.on("/api/display", HTTP_ANY, [](AsyncWebServerRequest *req) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error && doc["page"].is<int>()) {
        displayPage = doc["page"].as<int>() % 3;
      } else {
        displayPage = (displayPage + 1) % 3;
      }
      updateOLED();
      request->send(200, "application/json",
                    "{\"status\":\"OK\",\"displayPage\":" + String(displayPage) + "}"); });

  server.on("/api/hardware/config", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    JsonArray rArr = doc["relays"].to<JsonArray>();
    for (int i = 0; i < NUM_RELAYS; i++) rArr.add(relayActive[i]);
    JsonArray sArr = doc["switches"].to<JsonArray>();
    for (int i = 0; i < NUM_SWITCHES; i++) sArr.add(switchActive[i]);
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

  server.on("/api/hardware/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      if (!error) {
        if (doc["relays"].is<JsonArray>()) {
          JsonArray rArr = doc["relays"].as<JsonArray>();
          for (int i = 0; i < NUM_RELAYS && i < (int)rArr.size(); i++) {
            relayActive[i] = rArr[i].as<bool>();
            if (!relayActive[i]) {
              digitalWrite(RELAY_PINS[i], relayOffLevel);
            }
          }
        }
        if (doc["switches"].is<JsonArray>()) {
          JsonArray sArr = doc["switches"].as<JsonArray>();
          for (int i = 0; i < NUM_SWITCHES && i < (int)sArr.size(); i++) {
            switchActive[i] = sArr[i].as<bool>();
          }
        }
        saveHardwareConfig();
        request->send(200, "application/json", "{\"status\":\"OK\"}");
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\",\"message\":\"Bad Hardware Config Payload\"}"); });

  server.on("/api/wifi/reset", HTTP_ANY, [](AsyncWebServerRequest *request)
            {
    request->send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Resetting WiFi...\"}");
    delay(600);
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart(); });
}

// ---------------------------------------------------------------- SETUP
void setup()
{
  Serial.begin(115200);

  for (int r = 0; r < NUM_RELAYS; r++)
  {
    pinMode(RELAY_PINS[r], OUTPUT);
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadPolarity();
  loadHardwareConfig();
  for (int r = 1; r <= NUM_RELAYS; r++)
  {
    setRelay(r, false);
  }

  // OLED Init
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0)
  {
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    {
      oledConnected = true;
      display.clearDisplay();
      drawOledHeader("- SYSTEM START -");
      display.setCursor(0, 20);
      display.println("Booting R-Sync...");
      display.println("Please wait...");
      drawOledFooter();
      display.display();
    }
    else
    {
      oledConnected = false;
      Serial.println(F("[I2C] SSD1306 allocation failed"));
    }
  }
  else
  {
    oledConnected = false;
    Serial.println(F("[I2C] OLED SSD1306 not detected on 0x3C. Skipping display operations."));
  }

  loadSchedules();
  loadTimers();

  bool forcePortal = false;
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    forcePortal = true;
    if (oledConnected)
    {
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

  wm.setAPCallback([](WiFiManager *myWiFiManager)
                   {
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- CONFIG PORTAL -");
      display.setCursor(0, 14);
      display.println("WiFi Not Found!");
      display.println("SSID: R-Sync");
      display.println("IP: 192.168.4.1");
      drawOledFooter();
      display.display();
    } });

  bool wifiConfigured = false;
  wm.setSaveConfigCallback([&wifiConfigured]()
                           { wifiConfigured = true; });

  if (oledConnected && !forcePortal)
  {
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
  if (forcePortal)
  {
    wm.resetSettings();
    res = wm.startConfigPortal("R-Sync");
  }
  else
  {
    res = wm.autoConnect("R-Sync");
  }

  if (!res)
  {
    Serial.println("Failed to connect or portal timeout");
    ESP.restart();
  }

  if (wifiConfigured || forcePortal)
  {
    if (oledConnected)
    {
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

  // FIX: Matikan Power Save Mode untuk ping ultra-stabil dan zero packet loss
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (oledConnected)
  {
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
  ArduinoOTA.onStart([]()
                     {
    ota_updating = true;
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- SYSTEM UPDATE -");
      display.setCursor(0, 20);
      display.println("Receiving update...");
      display.println("Please wait...");
      drawOledFooter();
      display.display();
    } });
  ArduinoOTA.onEnd([]()
                   { ota_updating = false; });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
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
    } });
  ArduinoOTA.onError([](ota_error_t error)
                     { ota_updating = false; });
  ArduinoOTA.begin();

  setupAPI();
  server.begin();

  Serial.println("[R-Sync] v3.0.0 Ready.");
  Serial.printf("[R-Sync] OLED: %s\n", oledConnected ? "Connected" : "Not found");
}

// ---------------------------------------------------------------- LOOP
unsigned long lastTimerTick = 0;

void loop()
{
  ArduinoOTA.handle();

  if (ota_updating)
  {
    delay(10);
    return;
  }

  handleButtonPress();
  updateServos();
  updateServoSelfTest();

  unsigned long currentMillis = millis();

  // Tick countdown timers setiap 1000ms
  if (currentMillis - lastTimerTick >= 1000)
  {
    lastTimerTick = currentMillis;
    tickTimers();
  }

  // Refresh OLED dan check NTP schedules secara non-blocking setiap 1000ms
  if (currentMillis - lastOledUpdate >= 1000)
  {
    lastOledUpdate = currentMillis;

    // FIX: Menggunakan time_t & localtime() secara non-blocking pengganti getLocalTime()
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);

    if (timeinfo && timeinfo->tm_year > (1970 - 1900))
    {
      if (!ntpSynced)
      {
        ntpSynced = true;
      }
      else
      {
        checkSchedules(timeinfo->tm_hour, timeinfo->tm_min);
      }
    }

    if (!isHolding)
    {
      updateOLED();
    }
  }

  // FIX: Mengasih nafas ke FreeRTOS scheduler agar task async_tcp & WiFi tidak kena watchdog reset
  vTaskDelay(1 / portTICK_PERIOD_MS);
}