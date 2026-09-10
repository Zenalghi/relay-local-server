#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Deklarasi OLED display terhubung ke I2C (SDA, SCL)
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin Relay: 26 (Kiri/1m), 25 (3m), 33 (5m), 32 (Kanan/10m)
const int relayPins[4] = {26, 25, 33, 32};
const unsigned long relayDurations[4] = {60000, 180000, 300000, 600000}; // Durasi dalam ms (1m, 3m, 5m, 10m)

// Ganti ke LOW jika modul relay yang dipakai berjenis active-low
#define RELAY_ON HIGH
#define RELAY_OFF LOW

bool timersStarted = false;
unsigned long startTime = 0;

void setup() {
  Serial.begin(115200);

  // Inisialisasi pin relay
  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_OFF); // Pastikan awalnya mati
  }

  // Inisialisasi OLED, Address 0x3C umumnya dipakai untuk OLED 128x64
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Berhenti jika OLED gagal diinisialisasi
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("System Starting...");
  display.display();

  // Startup sequence: Relay paling kiri (pin 26) ON-OFF-ON-OFF
  for (int i = 0; i < 2; i++) {
    digitalWrite(relayPins[0], RELAY_ON);
    delay(500);
    digitalWrite(relayPins[0], RELAY_OFF);
    delay(500);
  }

  // Mulai timer utama setelah indikator selesai
  timersStarted = true;
  startTime = millis();
}

void loop() {
  if (!timersStarted) return;

  unsigned long currentMillis = millis();
  unsigned long elapsedTime = currentMillis - startTime;
  
  // Batasi timer maksimum agar tidak lewat dari 10 menit (600.000 ms) di layar
  if (elapsedTime > 600000) {
    elapsedTime = 600000; 
  }
  
  // Hitung menit dan detik untuk tampilan timer global
  unsigned long totalSeconds = elapsedTime / 1000;
  int displayMins = totalSeconds / 60;
  int displaySecs = totalSeconds % 60;

  display.clearDisplay();
  
  // Tampilkan teks judul
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Relay Timer Control");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE); // Garis pemisah

  // Tampilkan Timer Global
  display.setTextSize(2);
  display.setCursor(30, 15);
  display.printf("%02d:%02d", displayMins, displaySecs);
  
  // Tampilkan Status Tiap Relay
  display.setTextSize(1);
  for (int i = 0; i < 4; i++) {
    bool isON = false;
    
    // Relay ON jika waktu berjalan masih kurang dari durasi relay tersebut
    if (elapsedTime < relayDurations[i]) {
      isON = true;
      digitalWrite(relayPins[i], RELAY_ON);
    } else {
      isON = false;
      digitalWrite(relayPins[i], RELAY_OFF);
    }
    
    // Mengatur posisi kursor untuk layout tabel/grid di OLED
    int x = (i % 2) * 64; // Kolom 0 atau 64 (kiri / kanan)
    int y = 40 + (i / 2) * 12; // Baris 40 atau 52 (atas / bawah)
    
    display.setCursor(x, y);
    display.printf("R%d:%s", i+1, isON ? "ON" : "OFF");
  }
  
  display.display();
  
  delay(100); // Delay singkat untuk stabilisasi loop dan I2C
}