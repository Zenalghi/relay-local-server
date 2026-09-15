/**
 * @file main.cpp
 * @brief R-Sync ESP32 Local Server Firmware
 * @author Zenalghi
 *
 * GitHub Repositories:
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
#define HTTP_HEAD    0b00100000
#define HTTP_OPTIONS 0b01000000
#define HTTP_ANY     0b01111111

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <ArduinoOTA.h>

#define RELAY1_PIN 26
#define RELAY2_PIN 25
#define BUTTON_PIN 0

// Default = active low (legacy module behavior)
bool activeLow = true;
uint8_t relayOnLevel = LOW;
uint8_t relayOffLevel = HIGH;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledConnected = false;
volatile bool ota_updating = false;

// Helper to draw centered Header with divider line
void drawOledHeader(const char *title){
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

// Helper to draw centered "R-Sync" at the bottom row (y = 56 of 64px) on all screens
void drawOledFooter(){
  if (!oledConnected)
    return;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(46, 56);
  display.print("R-Sync");
}

AsyncWebServer server(80);
Preferences preferences;

void applyPolarity(bool isActiveLow){
  activeLow = isActiveLow;
  if (activeLow) {
    relayOnLevel = LOW;
    relayOffLevel = HIGH;
  }
  else {
    relayOnLevel = HIGH;
    relayOffLevel = LOW;
  }
}

void loadPolarity(){
  preferences.begin("cfg", true);
  bool stored = preferences.getBool("activeLow", true);
  preferences.end();
  applyPolarity(stored);
}

void savePolarity(){
  preferences.begin("cfg", false);
  preferences.putBool("activeLow", activeLow);
  preferences.end();
}

// SVG Logo from R.svg
const char *custom_svg_logo = R"rawliteral(
<div style="text-align:center;">
<svg xmlns="http://www.w3.org/2000/svg" xml:space="preserve" width="100px" height="100px" version="1.1" style="shape-rendering:geometricPrecision; text-rendering:geometricPrecision; image-rendering:optimizeQuality; fill-rule:evenodd; clip-rule:evenodd" viewBox="0 0 200.28 166.5" xmlns:xlink="http://www.w3.org/1999/xlink">
 <defs>
  <style type="text/css">
   <![CDATA[
    .fil0 {fill:#178697}
    .fil1 {fill:#2D3641}
    .fil2 {fill:#EC651C}
   ]]>
  </style>
 </defs>
 <g id="Layer_x0020_1">
  <path class="fil0" d="M21.24 40.87c0.43,0.95 -0.52,0.95 4.13,0.94l14.69 -0.01c6.39,-0.01 12.78,-0.03 19.17,-0.06l57.93 -0.05c7.68,-0.03 11.68,-1.19 15.83,3.35 4.3,4.7 13.15,10.29 7.47,18.29 -0.92,1.29 -3.82,4.43 -5.13,4.95 -3.54,2.95 -6.37,6.91 -10.16,10.33l-20.61 21.05c-1.6,1.86 -3.83,3.66 -5.38,5.22 -0.91,0.92 -1.52,1.97 -2.46,2.83 -1.41,1.28 -1.32,0.89 -2.44,2.5 0.37,0.69 -0.47,0.76 2.87,0.69 9.82,-0.2 46.18,0.39 49.18,-0.37l11.11 -11.05c2.08,-2.11 3.48,-3.39 5.72,-5.68 6.06,-6.19 17.91,-17.46 22.65,-23.07 6.78,-8.05 4.75,-19.96 -0.54,-25.34l-5.8 -5.67c-0.64,-0.79 -0.46,-0.82 -1.26,-1.56l-28.47 -28.22c-0.67,-0.61 -0.81,-0.75 -1.46,-1.42 -3.07,-3.13 -5.29,-5.43 -10.19,-6.99 -6.52,-2.09 -14.09,-1.41 -21.33,-1.44 -15.21,-0.05 -30.48,-0.1 -45.69,0.01 -10.6,0.08 -7.21,-1.12 -15.39,6.98 -1.87,1.85 -3.99,3.63 -5.99,5.45 -2.16,1.95 -3.48,4.02 -5.57,5.86l-22.87 22.48z"/>
  <path class="fil1" d="M-0 108.01c0.43,2.21 -0.17,0.34 1.05,1.67 0.36,0.39 0.97,1.05 1.35,1.47 3.59,4.05 10.08,8.85 13.6,12.91 1.54,1.78 3.78,3.98 5.48,5.54 1.01,0.93 1.77,1.65 2.77,2.54l13.42 13.1c2.35,1.96 3.48,3.19 5.52,5.48 2.4,2.69 10.54,9.94 13.33,13.18 3.29,3.82 4.79,2.26 14.14,2.34 7.09,0.06 38.17,0.35 42.88,-0.21l-10.9 -11.11c-3.74,-3.01 -6.19,-6.8 -9.25,-9.09 -0.79,-0.59 -1.11,-0.98 -1.88,-1.83 -4.14,-4.59 -6.81,-6.54 -10.92,-11.1l-7.42 -7.27c-1.75,-2.3 -4.72,-4.65 -7.35,-7.32l-7.31 -7.39c-1.31,-1.54 -2.83,-2.3 -3.8,-3.99 0.86,-1.28 1.2,-1.21 2.35,-2.17l34.14 -34.4c0.87,-1 1.23,-1.87 2.48,-2.19 -0.4,-0.99 0.54,-1.09 -3.02,-1.03l-22.86 0.04c-8.93,0.05 -17.86,-0.07 -26.78,-0.02 -1.82,3.61 -3.98,3.35 -7.43,7.6l-10.11 10.27c-1.31,1.25 -1.63,1.08 -2.82,2.5 -1.52,1.82 -3.21,3.73 -5.07,5.13 -2.57,1.93 -3,2.85 -5.02,5.18l-10.56 10.17z"/>
  <path class="fil2" d="M200.28 165.6l-52.78 -52.94c-0.8,-0.23 -0.79,-0.29 -2.26,-0.36l-42.35 -0.17c-1.07,-0 -2.25,-0.05 -3.29,-0.03 -0.51,0.01 -1.04,0 -1.54,0.14l-1.34 0.82c0.09,0.54 48,50.08 50.74,52.13 2.13,1.59 10.26,0.96 13.77,0.95 5.18,-0 36.99,0.54 39.05,-0.54z"/>
 </g>
</svg>
</div>
)rawliteral";

struct Job{
  uint8_t hour;
  uint8_t minute;
  bool action;
  bool enabled;
};

#define MAX_JOBS 4
Job jobs1[MAX_JOBS];
Job jobs2[MAX_JOBS];

bool manualOverride1 = false;
bool manualOverride2 = false;
bool ntpSynced = false;
int lastEvaluatedMinute = -1;

bool lastButtonState = HIGH;
unsigned long buttonPressTime = 0;
bool isHolding = false;

int displayPage = 0;
void updateOLED();

void setRelay(int channel, bool state){
  if (channel == 1) {
    digitalWrite(RELAY1_PIN, state ? relayOnLevel : relayOffLevel);
  }
  else if (channel == 2) {
    digitalWrite(RELAY2_PIN, state ? relayOnLevel : relayOffLevel);
  }
}

bool getRelay(int channel){
  if (channel == 1)
    return digitalRead(RELAY1_PIN) == relayOnLevel;
  return digitalRead(RELAY2_PIN) == relayOnLevel;
}

void setRelayPolarityAndForceOff(bool isActiveLow){
  applyPolarity(isActiveLow);
  setRelay(1, false);
  setRelay(2, false);
  manualOverride1 = false;
  manualOverride2 = false;
  savePolarity();
}

void loadJobs(){
  preferences.begin("sched", true);
  String json1 = preferences.getString("jobs1", "[]");
  String json2 = preferences.getString("jobs2", "[]");
  preferences.end();

  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, json1);
  if (!error) {
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
      jobs1[i].hour = arr[i]["h"] | 0;
      jobs1[i].minute = arr[i]["m"] | 0;
      jobs1[i].action = arr[i]["a"] | false;
      jobs1[i].enabled = arr[i]["e"] | false;
    }
  }

  doc.clear();
  error = deserializeJson(doc, json2);
  if (!error) {
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
      jobs2[i].hour = arr[i]["h"] | 0;
      jobs2[i].minute = arr[i]["m"] | 0;
      jobs2[i].action = arr[i]["a"] | false;
      jobs2[i].enabled = arr[i]["e"] | false;
    }
  }
}

void saveJobs(){
  JsonDocument doc;
  JsonArray arr1 = doc.to<JsonArray>();
  for (int i = 0; i < MAX_JOBS; i++) {
    JsonObject obj = arr1.add<JsonObject>();
    obj["h"] = jobs1[i].hour;
    obj["m"] = jobs1[i].minute;
    obj["a"] = jobs1[i].action;
    obj["e"] = jobs1[i].enabled;
  }
  String json1;
  serializeJson(doc, json1);

  doc.clear();
  JsonArray arr2 = doc.to<JsonArray>();
  for (int i = 0; i < MAX_JOBS; i++) {
    JsonObject obj = arr2.add<JsonObject>();
    obj["h"] = jobs2[i].hour;
    obj["m"] = jobs2[i].minute;
    obj["a"] = jobs2[i].action;
    obj["e"] = jobs2[i].enabled;
  }
  String json2;
  serializeJson(doc, json2);

  preferences.begin("sched", false);
  preferences.putString("jobs1", json1);
  preferences.putString("jobs2", json2);
  preferences.end();
}

int getMinutesFromMidnight(int h, int m){
  return h * 60 + m;
}

void reconcileState(int channel, Job *jobs, int currentMin){
  int bestJobIndex = -1;
  int minDiff = 24 * 60;

  for (int i = 0; i < MAX_JOBS; i++) {
    if (!jobs[i].enabled)
      continue;
    int jobMin = getMinutesFromMidnight(jobs[i].hour, jobs[i].minute);

    int diff = currentMin - jobMin;
    if (diff < 0) {
      diff += 24 * 60;
    }

    if (diff < minDiff) {
      minDiff = diff;
      bestJobIndex = i;
    }
  }

  if (bestJobIndex != -1) {
    setRelay(channel, jobs[bestJobIndex].action);
  }
}

void checkSchedules(int h, int m){
  int currentMin = getMinutesFromMidnight(h, m);
  if (currentMin == lastEvaluatedMinute)
    return;

  for (int i = 0; i < MAX_JOBS; i++) {
    if (jobs1[i].enabled && getMinutesFromMidnight(jobs1[i].hour, jobs1[i].minute) == currentMin) {
      setRelay(1, jobs1[i].action);
      manualOverride1 = false;
    }
  }

  for (int i = 0; i < MAX_JOBS; i++) {
    if (jobs2[i].enabled && getMinutesFromMidnight(jobs2[i].hour, jobs2[i].minute) == currentMin) {
      setRelay(2, jobs2[i].action);
      manualOverride2 = false;
    }
  }

  lastEvaluatedMinute = currentMin;
}

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

void setupAPI(){
  // Enable CORS headers for Web clients (e.g. Flutter Web, Chrome, Edge)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  // Handle preflight OPTIONS requests via onNotFound fallback
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      request->send(200);
    } else {
      request->send(404, "text/plain", "Not Found");
    } });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request)
 {
    JsonDocument doc;
    doc["ip"] = WiFi.localIP().toString();
    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
    
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
      char timeStringBuff[50];
      strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
      doc["time"] = String(timeStringBuff);
    } else {
      doc["time"] = "Not Synced";
    }

    doc["relay1"] = getRelay(1) ? "ON" : "OFF";
    doc["relay2"] = getRelay(2) ? "ON" : "OFF";
    doc["displayPage"] = displayPage;
    doc["activeLow"] = activeLow;

    JsonArray arr1 = doc["jobs1"].to<JsonArray>();
    for (int i = 0; i < MAX_JOBS; i++) {
      JsonObject obj = arr1.add<JsonObject>();
      obj["h"] = jobs1[i].hour;
      obj["m"] = jobs1[i].minute;
      obj["a"] = jobs1[i].action ? "ON" : "OFF";
      obj["e"] = jobs1[i].enabled;
    }
    
    JsonArray arr2 = doc["jobs2"].to<JsonArray>();
    for (int i = 0; i < MAX_JOBS; i++) {
      JsonObject obj = arr2.add<JsonObject>();
      obj["h"] = jobs2[i].hour;
      obj["m"] = jobs2[i].minute;
      obj["a"] = jobs2[i].action ? "ON" : "OFF";
      obj["e"] = jobs2[i].enabled;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });

  server.on("/api/display", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
 {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data, len);
      if (!error && doc["page"].is<int>()) {
        int page = doc["page"].as<int>();
        if (page >= 0 && page <= 1) {
          displayPage = page;
        } else {
          displayPage = (displayPage + 1) % 2;
        }
      } else {
        // Toggle if no specific page requested
        displayPage = (displayPage + 1) % 2;
      }
      updateOLED();
      request->send(200, "application/json", "{\"status\":\"OK\",\"displayPage\":" + String(displayPage) + "}"); });

  server.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
 {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data, len);
      if (!error) {
        int channel = doc["channel"] | 0;
        String state = doc["state"] | "";
        if (channel == 1 || channel == 2) {
          bool st = (state == "ON");
          setRelay(channel, st);
          if (channel == 1) manualOverride1 = true;
          if (channel == 2) manualOverride2 = true;
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\"}"); });

  server.on("/api/relay/polarity", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
 {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data, len);
      if (!error && doc["activeLow"].is<bool>()) {
        bool next = doc["activeLow"].as<bool>();
        setRelayPolarityAndForceOff(next);

        JsonDocument resp;
        resp["status"] = "OK";
        resp["activeLow"] = activeLow;
        resp["relay1"] = "OFF";
        resp["relay2"] = "OFF";
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
        return;
      }
      request->send(400, "application/json", "{\"status\":\"Error\"}"); });

  server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
 {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data, len);
      if (!error) {
        int channel = doc["channel"] | 0;
        JsonArray arr = doc["jobs"].as<JsonArray>();
        
        if (channel == 1) {
          for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
            jobs1[i].hour = arr[i]["h"] | 0;
            jobs1[i].minute = arr[i]["m"] | 0;
            jobs1[i].action = (arr[i]["a"] == "ON");
            jobs1[i].enabled = arr[i]["e"] | false;
          }
          saveJobs();
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        } else if (channel == 2) {
          for (int i = 0; i < MAX_JOBS && i < arr.size(); i++) {
            jobs2[i].hour = arr[i]["h"] | 0;
            jobs2[i].minute = arr[i]["m"] | 0;
            jobs2[i].action = (arr[i]["a"] == "ON");
            jobs2[i].enabled = arr[i]["e"] | false;
          }
          saveJobs();
          request->send(200, "application/json", "{\"status\":\"OK\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"status\":\"Error\"}"); });

  // Endpoint to remotely trigger WiFi configuration portal
  server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request)
 {
    request->send(200, "application/json", "{\"status\":\"OK\",\"message\":\"Resetting WiFi credentials. Opening Portal 'R-Sync'...\"}");
    delay(600);
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart(); });
}

void setup(){
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadPolarity();
  digitalWrite(RELAY1_PIN, relayOffLevel);
  digitalWrite(RELAY2_PIN, relayOffLevel);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed or not connected"));
    oledConnected = false;
  }
  else {
    oledConnected = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    drawOledHeader("- SYSTEM START -");
    display.setCursor(0, 20);
    display.println("Booting R-Sync...");
    display.println("Please wait...");
    drawOledFooter();
    display.display();
  }

  loadJobs();

  // Check if Boot button (GPIO 0) is pressed at startup for Instant WiFi Reset
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

  // Stored WiFi connection timeout: 60s (ideal tolerance for router boot after blackout)
  wm.setConnectTimeout(60);
  // Portal timeout after 2 minutes of inactivity (auto-restarts to retry WiFi connection)
  wm.setConfigPortalTimeout(120);

  // Callback when AP portal is active
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
    } });

  // Callback when user saves new WiFi credentials in web portal
  bool wifiConfigured = false;
  wm.setSaveConfigCallback([&wifiConfigured]()
                { wifiConfigured = true; });

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
  }
  else {
    res = wm.autoConnect("R-Sync");
  }

  if (!res) {
    Serial.println("Failed to connect or portal timeout");
    ESP.restart();
  }

  // Auto clean reboot after saving WiFi from portal
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

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // OTA Setup
  ArduinoOTA.setHostname("r-sync");
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
  ArduinoOTA.onEnd([]() {
    ota_updating = false;
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- SYSTEM UPDATE -");
      display.setCursor(0, 20);
      display.println("Update Success!");
      display.println("Rebooting...");
      drawOledFooter();
      display.display();
    } });
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
    } });
  ArduinoOTA.onError([](ota_error_t error)
          {
    ota_updating = false;
    if (oledConnected) {
      display.clearDisplay();
      drawOledHeader("- SYSTEM UPDATE -");
      display.setCursor(0, 20);
      display.println("Update Failed!");
      display.println("Reverting changes...");
      drawOledFooter();
      display.display();
    } });
  ArduinoOTA.begin();

  setupAPI();
  server.begin();
}

unsigned long lastOledUpdate = 0;

void updateOLED(){
  if (!oledConnected)
    return;

  display.clearDisplay();

  if (displayPage == 0) {
    // Header
    drawOledHeader("- DEVICE STATUS -");

    // WiFi
    display.setCursor(0, 13);
    display.print("WiFi: ");
    if (WiFi.status() == WL_CONNECTED) {
      display.println(WiFi.localIP());
    }
    else {
      display.println("Disconnected");
    }

    // Time
    display.setCursor(0, 23);
    display.print("Time: ");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      char timeStr[20];
      sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      display.println(timeStr);
    }
    else {
      display.println("Syncing...");
    }

    // Relays
    display.setCursor(0, 35);
    display.print("Relay 1: ");
    display.println(getRelay(1) ? "ON" : "OFF");
    display.setCursor(0, 45);
    display.print("Relay 2: ");
    display.println(getRelay(2) ? "ON" : "OFF");
  }
  else {
    // Header
    drawOledHeader("- SCHEDULER LIST -");

    auto drawScheduleGrid = [](int channel, Job jobs[], int startY) {
      display.setCursor(0, startY);
      display.printf("R%d:", channel);

      char items[4][8];
      int count = 0;
      for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].enabled) {
          snprintf(items[count], sizeof(items[count]), "%02d:%02d%c",
                   jobs[i].hour, jobs[i].minute, jobs[i].action ? '+' : '-');
          count++;
        }
      }

      if (count == 0) {
        display.setCursor(24, startY);
        display.print("No Sched");
      }
      else {
        // Baris 1 (Jadwal 1 & 2)
        if (count >= 1) {
          display.setCursor(24, startY);
          display.print(items[0]);
        }
        if (count >= 2) {
          display.setCursor(76, startY);
          display.print(items[1]);
        }
        // Baris 2 (Jadwal 3 & 4) -> Membentuk tabel 2x2 yang simetris dan rapi
        if (count >= 3) {
          display.setCursor(24, startY + 10);
          display.print(items[2]);
        }
        if (count >= 4) {
          display.setCursor(76, startY + 10);
          display.print(items[3]);
        }
      }
    };

    // Render Relay 1 (Tabel 2x2)
    drawScheduleGrid(1, jobs1, 11);

    // Garis pemisah horizontal antara Relay 1 dan Relay 2
    display.drawFastHLine(0, 31, 128, SSD1306_WHITE);

    // Render Relay 2 (Tabel 2x2)
    drawScheduleGrid(2, jobs2, 33);
  }

  drawOledFooter();
  display.display();
}

void drawOledCountdown(int secondsRemaining){
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

void handleButtonPress(){
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    // Button pressed down
    buttonPressTime = millis();
    isHolding = false;
  }
  else if (lastButtonState == LOW && currentButtonState == LOW) {
    // Button is being held down
    unsigned long duration = millis() - buttonPressTime;

    if (duration >= 1000 && duration < 11000) {
      isHolding = true;
      int elapsedSeconds = (duration - 1000) / 1000;
      int remaining = 10 - elapsedSeconds;
      if (remaining < 0)
        remaining = 0;

      static int lastDisplayedSec = -1;
      if (lastDisplayedSec != remaining) {
        lastDisplayedSec = remaining;
        drawOledCountdown(remaining);
      }
    }
    else if (duration >= 11000) {
      // Toggle Polarity Triggered
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
    // Button released
    unsigned long duration = millis() - buttonPressTime;

    if (!isHolding && duration < 1000) {
      // Short Press Action
      displayPage = (displayPage + 1) % 2;
      updateOLED();
    }
    else if (isHolding) {
      // Released early during countdown -> Cancel operation
      updateOLED();
    }
    isHolding = false;
  }

  lastButtonState = currentButtonState;
}

void loop(){
  ArduinoOTA.handle();

  // Jika sedang OTA, hentikan semua proses lain (tombol, layar, waktu) agar tidak mengganggu transfer
  if (ota_updating) {
    delay(10);
    return;
  }

  handleButtonPress();

  unsigned long currentMillis = millis();
  if (currentMillis - lastOledUpdate >= 1000) {
    lastOledUpdate = currentMillis;

    struct tm timeinfo;
    // Parameter 10ms memastikan proses getLocalTime tidak nge-block loop berlama-lama jika gagal sync
    bool gotTime = getLocalTime(&timeinfo, 10);

    if (gotTime) {
      if (!ntpSynced) {
        ntpSynced = true;
        int currentMin = getMinutesFromMidnight(timeinfo.tm_hour, timeinfo.tm_min);
        reconcileState(1, jobs1, currentMin);
        reconcileState(2, jobs2, currentMin);
        manualOverride1 = false;
        manualOverride2 = false;
      }
      else {
        checkSchedules(timeinfo.tm_hour, timeinfo.tm_min);
      }
    }

    // Refresh layar secara periodik hanya jika sedang tidak menahan tombol
    if (!isHolding) {
      updateOLED();
    }
  }
}