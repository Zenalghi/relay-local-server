#include <Arduino.h>

// Definisikan pin untuk Relay 1 dan Relay 2
const int RELAY_PIN_1 = 25;
const int RELAY_PIN_2 = 26;

void setup()
{
  Serial.begin(115200);
  
  // Karena low level trigger, kita set HIGH dulu sebelum pinMode
  // untuk mencegah relay menyala sesaat (glitch) saat ESP32 booting.
  // Untuk ESP32, pin mode default adalah input/floating.
  // Kita set pin mode dulu lalu set HIGH.
  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);

  // Set OFF (HIGH)
  digitalWrite(RELAY_PIN_1, HIGH);
  digitalWrite(RELAY_PIN_2, HIGH);
}

void loop()
{
  // LOW LEVEL TRIGGER:
  // HIGH = OFF (Mati)
  // LOW  = ON (Menyala)

  // OFF 1 detik
  Serial.println("Relay OFF");
  digitalWrite(RELAY_PIN_1, HIGH);
  digitalWrite(RELAY_PIN_2, HIGH);
  delay(1000);

  // ON 5 detik
  Serial.println("Relay ON");
  digitalWrite(RELAY_PIN_1, LOW);
  digitalWrite(RELAY_PIN_2, LOW);
  delay(5000);
}