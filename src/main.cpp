#include <Arduino.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledConnected = false;

const int RELAY_PIN_1 = 26; 
const int RELAY_PIN_2 = 25; 

#define RELAY_ON LOW
#define RELAY_OFF HIGH

volatile bool ota_updating = false;

unsigned long previousMillis = 0;
bool relayState = false; 

void updateMainPage() {
  if (!oledConnected) return;
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("=== MINIMAL OTA ===");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.println();
  display.print("Relay 1: ");
  display.println(relayState ? "ON" : "OFF");
  display.print("Relay 2: ");
  display.println(relayState ? "ON" : "OFF");
  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
  digitalWrite(RELAY_PIN_1, RELAY_OFF);
  digitalWrite(RELAY_PIN_2, RELAY_OFF);

  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    oledConnected = false;
  } else {
    oledConnected = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Booting...");
    display.display();
  }

  WiFiManager wm;
  if (oledConnected) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Connecting WiFi...");
    display.println("Portal: R-Sync");
    display.display();
  }
  
  bool res = wm.autoConnect("R-Sync");
  if(!res) {
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("r-sync");
  
  ArduinoOTA.onStart([]() {
    ota_updating = true;
    digitalWrite(RELAY_PIN_1, RELAY_OFF);
    digitalWrite(RELAY_PIN_2, RELAY_OFF);
    
    if (oledConnected) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("==== OTA UPDATE ====");
      display.display();
    }
  });
  
  ArduinoOTA.onEnd([]() {
    ota_updating = false;
    if (oledConnected) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("==== OTA UPDATE ====");
      display.println();
      display.println("Update Success!");
      display.println("Rebooting...");
      display.display();
    }
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Bar progres dihilangkan sesuai permintaan
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    ota_updating = false;
  });
  
  ArduinoOTA.begin();
  
  updateMainPage();
}

void loop() {
  ArduinoOTA.handle();

  if (ota_updating) {
    delay(10);
    return;
  }

  unsigned long currentMillis = millis();
  long interval = relayState ? 5000 : 1000;

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    relayState = !relayState;
    
    digitalWrite(RELAY_PIN_1, relayState ? RELAY_ON : RELAY_OFF);
    digitalWrite(RELAY_PIN_2, relayState ? RELAY_ON : RELAY_OFF);
    
    updateMainPage();
  }
}