#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>              // Bus I2C, has to be added to communicate with BMP280
#include <Adafruit_BMP280.h>   

// Wi-Fi credentials
const char *ssid = "DK_wifi";
const char *password = "ghtx4445";

// MQTT broker
const char *mqtt_broker = "broker.emqx.io";
const char *mqtt_topic = "esp32/telemetry";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 5000;

// --- I2C pin definitions for this specific board (custom wiring discovered on the bench) ---
#define I2C_SDA_PIN 41   // Custom SDA pin used on this Tenstar ESP32-S3 board
#define I2C_SCL_PIN 42   // Custom SCL pin used on this Tenstar ESP32-S3 board

// --- Sensor BMP280 (temperature + pressure) ---
Adafruit_BMP280 bmp;  // The communinaction is with I2C using Wire
bool bmpDisponible = false;

void connectToWiFi();
void connectToMQTT();
void publishTelemetry();

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("--- AcquaVision MQTT Temperature Test ---");

  connectToWiFi();
  mqttClient.setServer(mqtt_broker, mqtt_port);

  // Initialize I2C bus with pins (GPIO41 = SDA, GPIO42 = SCL)
  // instead of relying on the default SDA/SCL pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // El BMP280 responde en la dirección 0x76 o 0x77 según el módulo
  if (!bmp.begin(0x76)) {
    Serial.println("BMP280 not found in direction 0x76, trying 0x77...");
    if (!bmp.begin(0x77)) {
      Serial.println("Error: BMP280 not detected. Look SDA/SCL wiring.");
      bmpDisponible = false;
    } else {
      bmpDisponible = true;
    }
  } else {
    bmpDisponible = true;
  }

  if (bmpDisponible) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                     Adafruit_BMP280::SAMPLING_X2,   // oversampling temperature
                     Adafruit_BMP280::SAMPLING_X16,  // oversampling pressure
                     Adafruit_BMP280::FILTER_X16,
                     Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("BMP280 correctly initialized.");
  }
}

void loop() {
  if (!mqttClient.connected()) {
    connectToMQTT();
  }

  mqttClient.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;
    publishTelemetry();
  }
}

void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("Wi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32-AcquaVision-";
    clientId += String(random(0xffff), HEX);
    
	Serial.print("Connecting to MQTT broker...");
    
	if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" connected!");
    } else {
      Serial.print(" failed, state=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void publishTelemetry() {
  StaticJsonDocument<128> doc;

  if (bmpDisponible) {
    float temperature = bmp.readTemperature();   // Dregree Celcius
    float pressure = bmp.readPressure() / 100.0F;  // Pa to hPa

    doc["temperature"] = temperature;
    doc["pressure"] = pressure;   // It would be good for the boat
  } else {
    Serial.println("BMP280 not ready, temperature will not be shown");
    return;
  }

  char jsonBuffer[160];
  serializeJson(doc, jsonBuffer);

  bool success = mqttClient.publish(mqtt_topic, jsonBuffer);

  if (success) {
    Serial.println("Publish SUCCESS");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("Publish FAILED");
  }
}