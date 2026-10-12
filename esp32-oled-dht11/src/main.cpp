#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
#define DHT_PIN 4
#define DHT_TYPE DHT11

const unsigned long DISPLAY_REFRESH_MS = 125;
const unsigned long SENSOR_REFRESH_MS = 2000;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);

float temperatureC = NAN;
float humidity = NAN;
bool sensorOkay = false;
unsigned long lastDisplayRefresh = 0;
unsigned long lastSensorRefresh = 0;

void readSensor() {
  float newHumidity = dht.readHumidity();
  float newTemperatureC = dht.readTemperature();

  if (isnan(newHumidity) || isnan(newTemperatureC)) {
    sensorOkay = false;
    return;
  }

  humidity = newHumidity;
  temperatureC = newTemperatureC;
  sensorOkay = true;
}

void drawDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(0.75);
  display.setCursor(0, 0);
  display.println("Temperature/Humidity Reading");
  display.println("--------------");

  if (!sensorOkay) {
    display.println();
    display.println("Sensor read failed");
    display.println("Check D4 wiring");
  } else {
    display.print("Temp: ");
    display.print(temperatureC, 1);
    display.println(" C");

    display.print("Temp: ");
    display.print((temperatureC * 9.0 / 5.0) + 32.0, 1);
    display.println(" F");

    display.print("Humidity: ");
    display.print(humidity, 1);
    display.println(" %");
  }

  display.display();
}

void setup() {
  Wire.begin(21, 22); // SDA = D21, SCL = D22

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    while (true) {}
  }

  readSensor();
  drawDisplay();
  lastSensorRefresh = millis();
  lastDisplayRefresh = millis();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorRefresh >= SENSOR_REFRESH_MS) {
    lastSensorRefresh = now;
    readSensor();
  }

  if (now - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    lastDisplayRefresh = now;
    drawDisplay();
  }
}
