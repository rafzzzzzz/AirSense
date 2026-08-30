#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Adafruit_GFX.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <DFRobot_ENS160.h>
#include <ESP32Encoder.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <SPI.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <bsec.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error                                                                         \
    "Missing include/secrets.h. Copy include/secrets.example.h to include/secrets.h and configure it."
#endif

static_assert(sizeof(WIFI_MANAGER_AP_PASSWORD) - 1 >= 8,
              "WIFI_MANAGER_AP_PASSWORD must contain at least 8 characters");
static_assert(sizeof(WIFI_MANAGER_AP_PASSWORD) - 1 <= 63,
              "WIFI_MANAGER_AP_PASSWORD must contain at most 63 characters");
static_assert(sizeof(WIFI_MANAGER_AP_NAME) - 1 <= 32,
              "WIFI_MANAGER_AP_NAME must contain at most 32 characters");
static_assert(sizeof(SYSTEM_NAME_DEFAULT) - 1 <= 19,
              "SYSTEM_NAME_DEFAULT must contain at most 19 characters");

#ifdef AIR_SENSE_DEBUG
#define DEBUG_BEGIN(baud) Serial.begin(baud)
#define DEBUG_PRINT(value) Serial.print(value)
#define DEBUG_PRINTLN(value) Serial.println(value)
#else
#define DEBUG_BEGIN(baud) ((void)0)
#define DEBUG_PRINT(value) ((void)0)
#define DEBUG_PRINTLN(value) ((void)0)
#endif

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET,
                      INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensor(INFLUXDB_MEASUREMENT);
WiFiManager wm;

#define SEALEVELPRESSURE_HPA (1017)
Adafruit_BME680 bme;
Adafruit_BME280 bme280;
SCD4x sdc41;

#define RED_PIN 13
#define YELLOW_PIN 12
#define GREEN_PIN 14
#define BUZZER_PIN 27

ESP32Encoder encoder;
unsigned long encoderCurrentMillis;
unsigned long encoderPreviousMillis = 0UL;

#define RXD2 34 // Connects to the PMS5003 TX pin.
#define TXD2 33 // Connects to the PMS5003 RX pin.
int particles03um;
int particles05um;
int particles10um;
int particles25um;
int particles50um;
int particles100um;
int pm10standard;
int pm25standard;
int pm100standard;

TFT_eSPI tft = TFT_eSPI();

unsigned long previousMillis = 0UL;
unsigned long interval = 10000UL;
unsigned long currentMillis;

void printToDisplay();
void printToSerial();
void beep(int count);
void updateLeds();
void pushToDatabase();
String updateFrequency(int encoderValue);
boolean readPMSData(Stream *stream);

struct PMS5003Data {
  uint16_t frameLength;
  uint16_t pm10Standard, pm25Standard, pm100Standard;
  uint16_t pm10Environment, pm25Environment, pm100Environment;
  uint16_t particles03um, particles05um, particles10um, particles25um,
      particles50um, particles100um;
  uint16_t unused;
  uint16_t checksum;
};
PMS5003Data data;

void setup() {
  DEBUG_BEGIN(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  beep(1);

  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 50, 4);
  tft.setTextColor(TFT_WHITE);
  tft.fillRectVGradient(0, 0, 128, 160, TFT_ORANGE, TFT_RED);
  tft.setRotation(0);
  tft.println("Air\nSense");
  tft.setTextFont(2);
  tft.println("V1.0");

  WiFiManagerParameter systemName("system_name", "System name",
                                  SYSTEM_NAME_DEFAULT, 20);
  wm.addParameter(&systemName);
  wm.setTimeout(120);
#ifdef AIR_SENSE_DEBUG
  wm.setDebugOutput(true);
#else
  wm.setDebugOutput(false);
#endif
  const bool connected =
      wm.autoConnect(WIFI_MANAGER_AP_NAME, WIFI_MANAGER_AP_PASSWORD);

  if (!connected) {
    DEBUG_PRINTLN("Wi-Fi provisioning timed out");
  } else {
    DEBUG_PRINTLN("Wi-Fi connected");
    beep(2);
  }

  if (client.validateConnection()) {
    DEBUG_PRINTLN("InfluxDB connection validated");
    beep(3);
  } else {
    DEBUG_PRINT("InfluxDB connection failed: ");
    DEBUG_PRINTLN(client.getLastErrorMessage());
  }

  sensor.addTag("device", DEVICE_ID);
  sensor.addTag("System", systemName.getValue());

  Wire.begin();
  bme.begin();
  bme280.begin(0x76);
  sdc41.begin();

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

  ESP32Encoder::useInternalWeakPullResistors = UP;
  encoder.attachHalfQuad(26, 25);
  encoder.setCount(30);

  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);
  if (!Serial1) {
    beep(3);
  }

  tft.fillScreen(TFT_BLACK);
}

void loop() {
  if (encoder.getCount() > 60) {
    encoder.setCount(60);
  } else if (encoder.getCount() < 0) {
    encoder.setCount(0);
  }

  // The PMS5003 stream must be drained continuously so complete 32-byte frames
  // stay aligned and checksum validation remains reliable.
  if (readPMSData(&Serial1)) {
    particles03um = data.particles03um;
    particles05um = data.particles05um;
    particles10um = data.particles10um;
    particles25um = data.particles25um;
    particles50um = data.particles50um;
    particles100um = data.particles100um;
    pm10standard = data.pm10Standard;
    pm25standard = data.pm25Standard;
    pm100standard = data.pm100Standard;
  }

  encoderCurrentMillis = millis();
  if (encoderCurrentMillis - encoderPreviousMillis > 33L) {
    encoderPreviousMillis = encoderCurrentMillis;
    tft.setCursor(0, 145, 1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(updateFrequency(encoder.getCount()));
  }

  if (millis() < 15000) {
    interval = 1000UL;
  }

  currentMillis = millis();
  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    printToDisplay();
    printToSerial();
    updateLeds();
    pushToDatabase();
  }
}

void printToDisplay() {
  tft.setCursor(0, 0, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("Temperature: ");
  tft.print(bme.readTemperature());
  tft.println(" C");
  tft.print("Temperature2: ");
  tft.print(bme280.readTemperature());
  tft.println(" C");
  tft.print("Temperature3: ");
  tft.print(sdc41.getTemperature());
  tft.println(" C");
  tft.print("Humidity: ");
  tft.print(bme.readHumidity());
  tft.println(" %");
  tft.print("Humidity2: ");
  tft.print(bme280.readHumidity());
  tft.println(" %");
  tft.print("Humidity3: ");
  tft.print(sdc41.getHumidity());
  tft.println(" %");
  tft.print("Pressure: ");
  tft.print(bme.readPressure() / 100.0);
  tft.println(" hPa");
  tft.print("Pressure2: ");
  tft.print(bme280.readPressure() / 100.0);
  tft.println(" hPa");
  tft.print("CO2: ");
  tft.print(sdc41.getCO2());
  tft.println(" ppm");
  tft.println("PM 1.0 = " + String(pm10standard));
  tft.println("PM 2.5 = " + String(pm25standard));
  tft.println("PM 10.0 = " + String(pm100standard) + "\n");
}

void printToSerial() {
#ifdef AIR_SENSE_DEBUG
  Serial.println();
  Serial.println(" Refresh rate = " + String(interval / 1000) + "s\n");
  Serial.println("  Temperature = " + String(bme.temperature) + " C");
  Serial.println(" Temperature2 = " + String(bme280.readTemperature()) + " C");
  Serial.println(" Temperature3 = " + String(sdc41.getTemperature()) + " C");
  Serial.println("      Humidity = " + String(bme.humidity) + "%");
  Serial.println("     Humidity2 = " + String(bme280.readHumidity()) + "%");
  Serial.println("     Humidity3 = " + String(sdc41.getHumidity()) + "%");
  Serial.println("      Pressure = " + String(bme.pressure / 100) + " hPa");
  Serial.println("     Pressure2 = " + String(bme280.readPressure() / 100) +
                 " hPa");
  Serial.println("           CO2 = " + String(sdc41.getCO2()) + " ppm");
  Serial.println("        PM 1.0 = " + String(pm10standard));
  Serial.println("        PM 2.5 = " + String(pm25standard));
  Serial.println("       PM 10.0 = " + String(pm100standard));
  Serial.println("---------------------------------------");
  Serial.print("Particles > 0.3um / 0.1L air: ");
  Serial.println(particles03um);
  Serial.print("Particles > 0.5um / 0.1L air: ");
  Serial.println(particles05um);
  Serial.print("Particles > 1.0um / 0.1L air: ");
  Serial.println(particles10um);
  Serial.print("Particles > 2.5um / 0.1L air: ");
  Serial.println(particles25um);
  Serial.print("Particles > 5.0um / 0.1L air: ");
  Serial.println(particles50um);
  Serial.print("Particles > 10.0um / 0.1L air: ");
  Serial.println(particles100um);
#endif
}

void pushToDatabase() {
  sensor.clearFields();
  sensor.addField("temperature", bme.temperature);
  sensor.addField("humidity", bme.humidity);
  sensor.addField("pressure", bme.pressure / 100);
  sensor.addField("temperature2", bme280.readTemperature());
  sensor.addField("humidity2", bme280.readHumidity());
  sensor.addField("pressure2", bme280.readPressure() / 100);
  sensor.addField("temperature3", sdc41.getTemperature());
  sensor.addField("humidity3", sdc41.getHumidity());
  sensor.addField("co2", sdc41.getCO2());
  sensor.addField("pm10", pm10standard);
  sensor.addField("pm25", pm25standard);
  sensor.addField("pm100", pm100standard);
  sensor.addField("particles03", particles03um);
  sensor.addField("particles05", particles05um);
  sensor.addField("particles10", particles10um);
  sensor.addField("particles25", particles25um);
  sensor.addField("particles50", particles50um);
  sensor.addField("particles100", particles100um);

  if (WiFi.status() == WL_CONNECTED && client.writePoint(sensor)) {
    DEBUG_PRINTLN("Measurement written to InfluxDB");
  } else if (WiFi.status() != WL_CONNECTED && millis() > 10800000) {
    ESP.restart();
  }
}

void beep(int count) {
  if (count == 1) {
    tone(BUZZER_PIN, 500, 200);
  } else if (count == 2) {
    tone(BUZZER_PIN, 500, 100);
    tone(BUZZER_PIN, 1000, 100);
  } else if (count == 3) {
    tone(BUZZER_PIN, 500, 75);
    tone(BUZZER_PIN, 1000, 75);
    tone(BUZZER_PIN, 1500, 75);
  }
}

void updateLeds() {
  if (bme.humidity >= 60) {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(YELLOW_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
  } else if (bme.humidity >= 55) {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(YELLOW_PIN, HIGH);
    digitalWrite(GREEN_PIN, LOW);
  } else {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(YELLOW_PIN, LOW);
    digitalWrite(GREEN_PIN, HIGH);
  }
}

String updateFrequency(int encoderValue) {
  String frequencyText = "Refresh Rate - ";
  if (encoderValue >= 60) {
    interval = 300000UL;
    frequencyText += "300s  \n/                    ";
  } else if (encoderValue >= 54) {
    interval = 200000UL;
    frequencyText += "200s  \n///                  ";
  } else if (encoderValue >= 48) {
    interval = 150000UL;
    frequencyText += "150s  \n/////                ";
  } else if (encoderValue >= 42) {
    interval = 100000UL;
    frequencyText += "100s  \n///////              ";
  } else if (encoderValue >= 36) {
    interval = 80000UL;
    frequencyText += "80s   \n/////////            ";
  } else if (encoderValue >= 30) {
    interval = 60000UL;
    frequencyText += "60s   \n///////////          ";
  } else if (encoderValue >= 24) {
    interval = 50000UL;
    frequencyText += "50s   \n/////////////        ";
  } else if (encoderValue >= 18) {
    interval = 40000UL;
    frequencyText += "40s   \n///////////////      ";
  } else if (encoderValue >= 12) {
    interval = 30000UL;
    frequencyText += "30s   \n/////////////////    ";
  } else if (encoderValue >= 6) {
    interval = 20000UL;
    frequencyText += "20s   \n///////////////////  ";
  } else {
    interval = 10000UL;
    frequencyText += "10s   \n/////////////////////";
  }
  return frequencyText;
}

boolean readPMSData(Stream *stream) {
  if (!stream->available()) {
    return false;
  }

  if (stream->peek() != 0x42) {
    stream->read();
    return false;
  }

  if (stream->available() < 32) {
    return false;
  }

  uint8_t buffer[32];
  uint16_t sum = 0;
  stream->readBytes(buffer, 32);
  for (uint8_t i = 0; i < 30; i++) {
    sum += buffer[i];
  }

  uint16_t bufferU16[15];
  for (uint8_t i = 0; i < 15; i++) {
    bufferU16[i] = buffer[2 + i * 2 + 1];
    bufferU16[i] += (buffer[2 + i * 2] << 8);
  }
  memcpy(&data, bufferU16, 30);

  if (sum != data.checksum) {
    DEBUG_PRINTLN("PMS5003 checksum failure");
    return false;
  }
  return true;
}
