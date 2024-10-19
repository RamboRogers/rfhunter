#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

/*

RFHunter V4.0

Code is property of Matthew Rogers, 2024 matthewrogers.org
This code is free to use under the GNU GPLv3 license.

https://x.com/rogerscissp
*/

/*
I'm including the parts I used in the BOM. I've included links to Amazon, but these are not affiliate links. You can buy these anywhere.

The attached 3mf file is the case I designed in Fusion 360. You'll want to use supports to print it, but it's aligned on the bed properly in two separate plates. All of these parts clip in perfectly.

Parts
1. ESP-WROOM-32 - https://www.amazon.com/gp/product/B08D5ZD528
2. AD8317 - https://www.amazon.com/gp/product/B07FHZXTCZ
3. 10k Potentiometer - https://www.amazon.com/gp/product/B0CSW6D1N4
5. 3000mah Battery - https://www.amazon.com/gp/product/B08T6GT7DV
6. Charge Controller - https://www.amazon.com/gp/product/B08DNK398S
7. Boost Converter - https://www.amazon.com/gp/product/B09S8ZDVBN
8. 128x64 OLED Display - https://www.amazon.com/gp/product/B0D2RLXLBZ
9. Piezo Buzzer - https://www.amazon.com/Cylewet-Terminals-Electronic-Electromagnetic-Impedance/dp/B01NCOXB2Q
10. Power Switch - https://www.amazon.com/gp/product/B007QAJUUS

Consumables and Tools
1. Dupont Wires - https://www.amazon.com/gp/product/B01EV70C78
2. Solder and Soldering Iron - https://www.amazon.com/gp/product/B0CZ94B328
3. Wire Stripper - https://www.amazon.com/gp/product/B0B4J8C8FD
*/

/*
Wiring Guide for RF Signal Scanner:

Power Circuit:
1. Battery (3.7V) positive terminal -> Power Switch
2. Power Switch -> TP4056 Charge Controller (B+)
3. TP4056 OUT+ -> ESP32 VIN and Boost Converter IN+
4. Boost Converter OUT+ (adjusted to 9V) -> AD8317 VCC
5. Battery negative terminal -> TP4056 B- and ESP32 GND and Boost Converter IN-
6. Boost Converter OUT- -> AD8317 GND

Signal and Control:
7. AD8317 VOUT -> ESP32 GPIO 34 (RF_SENSOR_PIN)
8. Potentiometer VCC -> ESP32 3.3V
9. Potentiometer GND -> ESP32 GND
10. Potentiometer Wiper -> ESP32 GPIO 35 (POT_PIN)
11. OLED Display VCC -> ESP32 3.3V
12. OLED Display GND -> ESP32 GND
13. OLED Display SDA -> ESP32 GPIO 21 (OLED_SDA)
14. OLED Display SCL -> ESP32 GPIO 22 (OLED_SCL)
15. Piezo Buzzer positive -> ESP32 GPIO 5 (BUZZER_PIN)
16. Piezo Buzzer negative -> ESP32 GND

Notes:
- The power switch controls the main power flow from the battery.
- The TP4056 charge controller manages battery charging and protection.
- The boost converter steps up the 3.3V from the battery to 9V for the AD8317 sensor.
- All GND connections should be common.
- Double-check all connections and voltage levels before powering on.
*/

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Pin definitions for ESP32
#define RF_SENSOR_PIN 34   // ADC1_CH6 (GPIO 34)
#define POT_PIN 35         // ADC1_CH7 (GPIO 35)
#define OLED_SDA 21        // Default SDA pin for ESP32
#define OLED_SCL 22        // Default SCL pin for ESP32
#define BUZZER_PIN 5       // You can choose any digital pin

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int numReadings = 20;
int readings[numReadings];
int readIndex = 0;
long total = 0;

int baselineRaw = 0;
int minRaw = 4095;  // ESP32 ADC is 12-bit (0-4095)
int maxRaw = 0;

float rawToVoltage(int raw) {
  return raw * (3.3f / 4095.0f);  // ESP32 ADC is 12-bit
}

int getBaselineRange() {
  int potValue = analogRead(POT_PIN);
  // Map potentiometer value to 0-200 range
  return map(potValue, 0, 4095, 0, 200);
}

// AD8317 specific constants
const float V_MIN = 0.33f;  // Minimum output voltage
const float V_MAX = 1.65f;  // Maximum output voltage
const float SLOPE = -22.0f; // mV/dB
const float INTERCEPT = 0.0f; // dBm at V_MAX (adjusted)

float voltageTodBm(float voltage) {
  if (voltage < V_MIN) return -70.0f;
  if (voltage > V_MAX) return 0.0f;
  return SLOPE * (voltage - V_MAX) + INTERCEPT;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n\nRF Signal Scanner - ESP32 with AD8317"));

  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  pinMode(RF_SENSOR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Establish baseline
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(RF_SENSOR_PIN);
    delay(10);
  }
  baselineRaw = sum / 100;
  minRaw = maxRaw = baselineRaw;

  Serial.print(F("Baseline Raw: "));
  Serial.println(baselineRaw);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("RF Signal Scanner"));
  display.setCursor(0, 16);
  display.print(F("Baseline: "));
  display.println(baselineRaw);
  display.display();
  delay(2000);

  for (int i = 0; i < numReadings; i++) {
    readings[i] = baselineRaw;
  }
  total = baselineRaw * numReadings;

  Serial.println(F("Setup complete"));
}

void loop() {
  int baselineRange = getBaselineRange();

  total = total - readings[readIndex];
  readings[readIndex] = analogRead(RF_SENSOR_PIN);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;

  int average = total / numReadings;

  if (average < minRaw) minRaw = average;
  if (average > maxRaw) maxRaw = average;

  float voltage = rawToVoltage(average);
  float dBm = voltageTodBm(voltage);
  int signalStrength = 0;
  if (baselineRange > 0) {
    signalStrength = map(average, baselineRaw, baselineRaw - baselineRange, 0, 10);
    signalStrength = constrain(signalStrength, 0, 10);
  }

  Serial.print(F("Raw: "));
  Serial.print(average);
  Serial.print(F(" Range: "));
  Serial.print(baselineRange);
  Serial.print(F(" V: "));
  Serial.print(voltage, 3);
  Serial.print(F("V dBm: "));
  Serial.print(dBm, 1);
  Serial.print(F(" Strength: "));
  Serial.println(signalStrength);

  display.clearDisplay();

  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("RF Signal Strength"));

  // Raw value and voltage
  display.setCursor(0, 16);
  display.print(F("Raw: "));
  display.print(average);
  display.setCursor(64, 16);
  display.print(F("V: "));
  display.print(voltage, 2);
  display.print(F("V"));

  // Range and dBm
  display.setCursor(0, 26);
  display.print(F("Range: "));
  display.print(baselineRange);
  display.setCursor(64, 26);
  display.print(F("dBm: "));
  display.print(dBm, 1);

  // Signal strength
  display.setCursor(0, 36);
  display.print(F("Strength: "));
  display.print(signalStrength);

  // Draw signal strength bars
  int barWidth = 12;
  int barSpacing = 2;
  int startX = (SCREEN_WIDTH - (barWidth * 10 + barSpacing * 9)) / 2;
  int barMaxHeight = 20;

  for (int i = 0; i < 10; i++) {
    int barHeight = (i < signalStrength) ? map(i, 0, 9, 5, barMaxHeight) : 2;
    display.fillRect(startX + i * (barWidth + barSpacing), SCREEN_HEIGHT - barHeight, barWidth, barHeight, SSD1306_WHITE);
  }

  display.display();

  // Beep based on signal strength
  if (signalStrength > 0) {
    int freq = map(signalStrength, 1, 10, 500, 2000);
    tone(BUZZER_PIN, freq, 50);
  }

  delay(100);
}