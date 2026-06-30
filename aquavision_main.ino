// ============================================================
//  AquaVision Real-Time Telemetry Pipeline (Cleaned Version)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

// --- Wi-Fi Configuration ---
const char* ssid     = "eduroam";     // Mit eurem Hotspot-Namen ersetzen
const char* password = "WIFI_PASSWORD"; 

// --- MQTT Broker Configuration ---
const char* mqtt_broker   = "broker.hivemq.com"; 
const char* mqtt_topic    = "aquavision/boat/telemetry";
const int   mqtt_port      = 1883; 

// --- Hardware Object Instances ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);
TinyGPSPlus gps;

// Hardware Serial 1 pins mapped to Waveshare GNSS receiver
#define GPS_RX 17 
#define GPS_TX 18
HardwareSerial GPSSerial(1);

// --- Telemetry Variables ---
double currentLat  = 0.0; 
double currentLon  = 0.0; 
double currentAlt  = 0.0;      
int    trackedSats = 0;        
float  boatSpeed   = 0.0;       

unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 5000; // Genau 5 Sekunden Sendeintervall

// Function Declarations
void connectToWiFi();
void connectToMQTT();
void publishTelemetry();

void setup() {
  // Start Debug Serial Monitor
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- AquaVision Real-Time Telemetry Pipeline ---");

  // Start GPS communication on UART1 (115200 baud)
  GPSSerial.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS UART Connection initialized.");

  // Network Startup
  connectToWiFi();
  mqttClient.setServer(mqtt_broker, mqtt_port);
}

void loop() {
  // 1. Keep MQTT Connection Alive
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();

  // 2. Continuous GPS Stream Parsing
  while (GPSSerial.available() > 0) {
    char nmeaChar = GPSSerial.read();
    gps.encode(nmeaChar); 
  }

  // 3. Non-blocking Timer: Publish every 5 seconds
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;

    if (gps.location.isValid()) {
      currentLat  = gps.location.lat();      
      currentLon  = gps.location.lng();      
      currentAlt  = gps.altitude.meters();    
      trackedSats = gps.satellites.value();   
      boatSpeed   = gps.speed.knots();        
      
      Serial.println("[GNSS Sync] Coordinates updated from satellite fix.");
    } else {
      Serial.println("[GNSS Warning] Waiting for satellite lock...");
    }

    // Pack JSON and send it over MQTT
    publishTelemetry();
  }
}

// --- Infrastructure Implementations ---

void connectToWiFi() {
  Serial.printf("Connecting to Wi-Fi: %s\n", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected successfully!");
}

void connectToMQTT() {
  while (!mqttClient.connected()) {
    String clientID = "ESP32S3-AquaVisionBoat-";
    clientID += String(random(0, 0xffff), HEX);
    
    Serial.printf("Connecting to MQTT Broker: %s...\n", mqtt_broker);
    
    if (mqttClient.connect(clientID.c_str())) {
      Serial.println("MQTT Connection: ESTABLISHED!");
    } else {
      Serial.printf("Failed, rc=%d. Retry in 5s...\n", mqttClient.state());
      delay(5000);
    }
  }
}

void publishTelemetry() {
  StaticJsonDocument<256> doc;
  
  doc["lat"]  = currentLat;
  doc["lon"]  = currentLon;
  doc["alt"]  = currentAlt;
  doc["sats"] = trackedSats;
  doc["spd"]  = boatSpeed;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  bool success = mqttClient.publish(mqtt_topic, jsonBuffer);
  
  if (success) {
    Serial.printf("[MQTT Success] Sent to %s -> %s\n", mqtt_topic, jsonBuffer);
  } else {
    Serial.println("⚠️ MQTT Transmit Error!");
  }
}