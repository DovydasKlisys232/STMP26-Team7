#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

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
const unsigned long publishInterval = 5000; // 5 seconds for testing

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

  // Test value. Later this can be replaced with a real temperature sensor reading.
  doc["temperature"] = 22.4;

  char jsonBuffer[128];
  serializeJson(doc, jsonBuffer);

  bool success = mqttClient.publish(mqtt_topic, jsonBuffer);

  if (success) {
    Serial.println("Publish SUCCESS");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("Publish FAILED");
  }
}
