#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h> // Library to decode satellite NMEA sentences

// --- Wi-Fi Configuration ---
const char* ssid     = "eduroam";     // Replace with actual wifi and password - hotspot
const char* password = "WIFI_PASSWORD"; 

// --- MQTT Broker Configuration ---
const char* mqtt_broker   = "broker.hivemq.com"; 
const char* mqtt_topic    = "aquavision/boat/telemetry";
const int   mqtt_port     = 1883; 

// --- Hardware Interface Drivers ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);
TinyGPSPlus gps; // TinyGPS++ parser instance

// --- Hardware Serial Mapping (Waveshare GNSS HAT pins) ---
// Using Hardware Serial 1 on the ESP32-S3 to read the satellite stream
#define RX_PIN 17 // Blue/Yellow wire from Waveshare TX
#define TX_PIN 18 // Pin connected to Waveshare RX

// --- Dynamic Telemetry Storage Variables ---
// Variables are now initialized to 0.0 and will be overwritten by live satellite data
double currentLat  = 0.0; 
double currentLon  = 0.0; 
double currentAlt  = 0.0;      
int    trackedSats = 0;        
float  boatSpeed   = 0.0;       
float  airPressure = 1013.25; // Placeholder value (Requires an external I2C sensor like BMP280)
// variables saving distance traveled
double totalDistance = 0.0;   
double lastLat = 0.0;         
double lastLon = 0.0;

unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 5000; // MODIFIED: Exactly 5 seconds execution interval

void connectToWiFi();
void connectToMQTT();
void publishTelemetry();

void setup() {
  // Primary USB Port execution stream monitoring
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- AquaVision Real-Time Telemetry Pipeline ---");

  // Initialize Hardware Serial 1 linked to the Waveshare GNSS receiver at 115200 baud
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("Hardware Serial 1 connection linked to Waveshare GNSS at 115200 baud.");

  connectToWiFi();
  mqttClient.setServer(mqtt_broker, mqtt_port);
}

void loop() {
  // 1. Maintain active background data broker connection lines
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();

  // 2. INGESTION LOOP: Read raw NMEA stream from the Waveshare GNSS board continually
  // This must run constantly without delay filters so we never drop satellite coordinate frames
  while (Serial1.available() > 0) {
    char nmeaChar = Serial1.read();
    gps.encode(nmeaChar); // Feed characters one by one into the TinyGPS++ decoder engine
  }

  // 3. EXECUTION TIMER: Publish data packets precisely every 5 seconds
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;

    // Check if the Waveshare module has established a valid coordinate lock over the sky
    if (gps.location.isValid()) {
      // OVERWRITE local variables with real authentic data points extracted from the satellite signals
      currentLat  = gps.location.lat();      // Real live Latitude
      currentLon  = gps.location.lng();      // Real live Longitude
      currentAlt  = gps.altitude.meters();    // Real live Altitude
      trackedSats = gps.satellites.value();   // Active number of tracked satellites
      boatSpeed   = gps.speed.knots();        // Active calculated speed over ground


      
      if (lastLat != 0.0 && lastLon != 0.0) {
        // distance between last position and new position 
        double meterDriven = TinyGPSPlus::distanceBetween(currentLat, currentLon, lastLat, lastLon);
        
        // only if the boat moves more than 2 meters then it counts
        if (meterDriven > 2.0) { 
          totalDistance += meterDriven; // Adding Distance to totalDistance
        }
      } 

      // saves current position to old position, so the new distance can be added to the old one
      lastLat = currentLat;
      lastLon = currentLon 
      
      Serial.println("[GNSS Sync Alert] Real-time coordinates successfully updated from satellite fix.");
    } else {
      // If inside a building, the GPS won't have a fix. It will display 0 or default values.
      Serial.println("[GNSS Warning] Waiting for valid satellite lock. Take the hardware near a window or open roof.");
    }

    // Read additional hardware sensors here if applicable (e.g. Temperature, Pressure)
    // airPressure = readPressureSensor(); 

    // Bundle the real values and push them out over the air to the web client broker
    publishTelemetry();
  }
}

// --- Connectivity Infrastructure Implementations ---

void connectToWiFi() {
  Serial.print("Connecting to local Wi-Fi: ");
  Serial.println(ssid);
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
      Serial.printf("Failed to connect, error code state = %d. Re-trying in 5s...\n", mqttClient.state());
      delay(5000);
    }
  }
}

void publishTelemetry() {
  StaticJsonDocument<256> doc;
  
  // Fill payload fields with the values extracted straight out of the GNSS receiver
  doc["lat"]  = currentLat;
  doc["lon"]  = currentLon;
  doc["alt"]  = currentAlt;
  doc["sats"] = trackedSats;
  doc["spd"]  = boatSpeed;
  doc["pres"] = airPressure; // Transmits current pressure state metric

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  bool success = mqttClient.publish(mqtt_topic, jsonBuffer);
  
  if (success) {
    Serial.printf("[MQTT Transmit Success] Topic: %s Payload: %s\n", mqtt_topic, jsonBuffer);
  } else {
    Serial.println("⚠️ Packet Delivery Error: Failed to publish telemetry over the network broker.");
  }
}
