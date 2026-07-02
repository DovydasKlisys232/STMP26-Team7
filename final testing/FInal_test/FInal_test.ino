#define MQTT_MAX_PACKET_SIZE 16384

#include <WiFi.h>
#include <TinyGPS++.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h> // Library for HTTP Web Server

#include "esp_camera.h"
#include "camera_pins.h"

// =========================
// WIFI CONFIGURATION
// =========================

const char* mqtt_server = "10.26.119.138";
const int mqtt_port = 1883;
const char* mqtt_topic = "esp32/telemetry";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80); // Web Server initialization on port 80

const char* ssid = "DK_wifi";
const char* password = "ghtx4445";

// =========================
// GNSS CONFIGURATION
// =========================

#define RX_PIN 17
#define TX_PIN 18

TinyGPSPlus gps;

// =========================
// TELEMETRY VARIABLES
// =========================

double currentLat = 0.0;
double currentLon = 0.0;
double currentAlt = 0.0;

int trackedSats = 0;

float boatSpeed = 0.0;
float airPressure = 1013.25;

double totalDistance = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;

unsigned long lastPublish = 0;
const unsigned long publishInterval = 5000;

// =========================
// FUNCTION DECLARATIONS
// =========================

void connectToWiFi();
void connectToMQTT();
void publishTelemetry();
void camera_config(camera_config_t &config);
void handleJpg(); // Function declaration to send the photo via HTTP

// =========================
// SETUP
// =========================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== AquaVision Camera + GNSS Test (Hybrid MQTT+HTTP) ===");

    // =========================
    // CAMERA SETUP
    // =========================

    camera_config_t config;
    camera_config(config);

    if (psramFound())
    {
        Serial.println("PSRAM FOUND");
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15; 
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
        config.fb_location = CAMERA_FB_IN_PSRAM;
    }
    else
    {
        Serial.println("PSRAM NOT FOUND");
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    Serial.println("Initializing Camera...");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera Init Failed: 0x%x\n", err);
        while (true)
        {
            delay(1000);
        }
    }

    Serial.println("Camera Init Success");
    sensor_t *s = esp_camera_sensor_get();
    if (s)
    {
        Serial.printf("Camera PID: 0x%02X\n", s->id.PID);
        s->set_brightness(s, 1);
    }

    // =========================
    // GNSS SETUP
    // =========================

    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial.println("GNSS Serial Started");

    // =========================
    // WIFI & SERVER SETUP
    // =========================

    connectToWiFi();

    // MQTT Configuration
    mqttClient.setServer(mqtt_server, mqtt_port);

    // HTTP Endpoint Configuration for the camera - USING THE WORKING CODE FROM test_full_system.ino
    server.on("/", handleRoot);   // Add a root page like in test_full_system.ino
    server.on("/cam.jpg", handleJpg);
    server.on("/gps", handleGPS); // Add GPS JSON endpoint
    
    server.begin();
    Serial.println("HTTP Server started on port 80");
}

// =========================
// LOOP
// =========================

void loop()
{
    // Listen for and handle incoming HTTP requests from the browser
    server.handleClient();

    if (!mqttClient.connected())
    {
        connectToMQTT();
    }

    mqttClient.loop();

    while (Serial1.available())
    {
        gps.encode(Serial1.read());
    }

    if (gps.location.isValid())
    {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();

        if (gps.altitude.isValid())
        {
            currentAlt = gps.altitude.meters();
        }

        if (gps.satellites.isValid())
        {
            trackedSats = gps.satellites.value();
        }

        if (gps.speed.isValid())
        {
            boatSpeed = gps.speed.knots();
        }

        if (lastLat != 0.0 && lastLon != 0.0)
        {
            double moved = TinyGPSPlus::distanceBetween(currentLat, currentLon, lastLat, lastLon);
            if (moved > 2.0)
            {
                totalDistance += moved;
            }
        }

        lastLat = currentLat;
        lastLon = currentLon;
    }

    if (millis() - lastPublish >= publishInterval)
    {
        publishTelemetry();
        lastPublish = millis();
    }
}

// =========================
// HTTP JPG HANDLER - FIXED WITH DELAY AND CORS
// =========================

void handleJpg() {
    // Add a small delay to let the camera stabilize
    delay(50);
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed for HTTP request");
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    // Add CORS header to allow your website (port 8080) to access the image
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    // Send JPEG image directly as binary to the browser
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// =========================
// ROOT PAGE - (Optional, copied from test_full_system.ino)
// =========================

void handleRoot()
{
    String html;

    html += "<html><body>";
    html += "<h1>AquaVision GNSS + Camera + MQTT</h1>";

    html += "<h2>GNSS Data</h2>";

    html += "<p><b>Latitude:</b> ";
    html += String(currentLat, 6);
    html += "</p>";

    html += "<p><b>Longitude:</b> ";
    html += String(currentLon, 6);
    html += "</p>";

    html += "<p><b>Altitude:</b> ";
    html += String(currentAlt, 2);
    html += " m</p>";

    html += "<p><b>Satellites:</b> ";
    html += String(trackedSats);
    html += "</p>";

    html += "<p><b>Speed:</b> ";
    html += String(boatSpeed, 2);
    html += " knots (";
    html += String(boatSpeed * 1.852, 2);
    html += " km/h)</p>";

    html += "<p><b>Total Distance:</b> ";
    html += String(totalDistance, 2);
    html += " m</p>";

    if (gps.hdop.isValid())
    {
        html += "<p><b>HDOP:</b> ";
        html += String(gps.hdop.value());
        html += "</p>";
    }

    if (gps.date.isValid())
    {
        html += "<p><b>Date:</b> ";
        html += String(gps.date.day());
        html += "/";
        html += String(gps.date.month());
        html += "/";
        html += String(gps.date.year());
        html += "</p>";
    }

    if (gps.time.isValid())
    {
        html += "<p><b>UTC Time:</b> ";
        html += String(gps.time.hour());
        html += ":";
        html += String(gps.time.minute());
        html += ":";
        html += String(gps.time.second());
        html += "</p>";
    }

    html += "<hr>";
    html += "<a href='/gps'>GPS JSON</a>";
    html += "<br><br>";
    html += "<img src='/cam.jpg' width='640'>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

// =========================
// GPS JSON OUTPUT - (Copied from test_full_system.ino)
// =========================

void handleGPS()
{
    String json = "{";

    json += "\"lat\":";
    json += String(currentLat, 6);

    json += ",\"lon\":";
    json += String(currentLon, 6);

    json += ",\"alt\":";
    json += String(currentAlt, 2);

    json += ",\"sats\":";
    json += String(trackedSats);

    json += ",\"speed_knots\":";
    json += String(boatSpeed, 2);

    json += ",\"speed_kmh\":";
    json += String(boatSpeed * 1.852, 2);

    json += ",\"distance\":";
    json += String(totalDistance, 2);

    if (gps.hdop.isValid())
    {
        json += ",\"hdop\":";
        json += String(gps.hdop.value());
    }

    if (gps.date.isValid())
    {
        json += ",\"day\":";
        json += String(gps.date.day());

        json += ",\"month\":";
        json += String(gps.date.month());

        json += ",\"year\":";
        json += String(gps.date.year());
    }

    if (gps.time.isValid())
    {
        json += ",\"hour\":";
        json += String(gps.time.hour());

        json += ",\"minute\":";
        json += String(gps.time.minute());

        json += ",\"second\":";
        json += String(gps.time.second());
    }

    json += "}";

    server.send(200, "application/json", json);
}

// =========================
// WIFI CONNECTION
// =========================

void connectToWiFi()
{
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

// =========================
// CAMERA CONFIGURATION
// =========================

void camera_config(camera_config_t &config)
{
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;

    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;

    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
}

// =========================
// MQTT CONNECTION
// =========================

void connectToMQTT()
{
    while (!mqttClient.connected())
    {
        String clientID = "ESP32S3-AquaVision-";
        clientID += String(random(0xffff), HEX);

        Serial.print("Connecting MQTT...");

        if (mqttClient.connect(clientID.c_str()))
        {
            Serial.println("CONNECTED");
        }
        else
        {
            Serial.print("FAILED state=");
            Serial.println(mqttClient.state());
            delay(5000);
        }
    }
}

// =========================
// TELEMETRY PUBLISHER
// =========================

void publishTelemetry()
{
    StaticJsonDocument<512> doc;

    doc["temperature"] = 22.4;
    doc["lat"] = currentLat;
    doc["lon"] = currentLon;
    doc["alt"] = currentAlt;
    doc["sats"] = trackedSats;
    doc["speed"] = boatSpeed * 1.852;
    doc["distance"] = totalDistance;

    if (gps.hdop.isValid())
    {
        doc["hdop"] = gps.hdop.value();
    }

    if (gps.date.isValid())
    {
        doc["day"] = gps.date.day();
        doc["month"] = gps.date.month();
        doc["year"] = gps.date.year();
    }

    if (gps.time.isValid())
    {
        doc["hour"] = gps.time.hour();
        doc["minute"] = gps.time.minute();
        doc["second"] = gps.time.second();
    }

    // Dynamically generate HTTP URL pointing to the ESP32's IP address
    String photoUrl = "http://" + WiFi.localIP().toString() + "/cam.jpg";
    doc["photo"] = photoUrl;

    String payload;
    serializeJson(doc, payload);

    bool success = mqttClient.publish(mqtt_topic, payload.c_str());

    if (success)
    {
        Serial.println("MQTT Publish SUCCESS");
        Serial.println(payload);
    }
    else
    {
        Serial.println("MQTT Publish FAILED");
    }
}