#define MQTT_MAX_PACKET_SIZE 16384

#include <WiFi.h>
#include <TinyGPS++.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h> 

// =========================================================================
// IL TRUCCO MAGICO: Risoluzione del conflitto "sensor_t" tra Camera e Adafruit
// =========================================================================
#define sensor_t camera_sensor_t
#include "esp_camera.h"
#undef sensor_t
// =========================================================================

#include <Wire.h>              
#include <Adafruit_BMP280.h>   
#include "camera_pins.h"

// =========================
// WIFI CONFIGURATION
// =========================

const char* mqtt_server = "10.26.119.138"; 
const int mqtt_port = 1883;
const char* mqtt_topic = "esp32/telemetry";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);

const char* ssid = "DK_wifi";
const char* password = "ghtx4445";

// =========================
// SENSOR BMP280 CONFIGURATION
// =========================
#define I2C_SDA_PIN 42   
#define I2C_SCL_PIN 41   

// Inizializziamo il sensore usando il bus Wire standard senza parametri extra
Adafruit_BMP280 bmp; 
bool bmpAvailable = false;

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

double totalDistance = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;

unsigned long lastPublish = 0;
const unsigned long publishInterval = 5000;
unsigned long lastMQTTReconnectAttempt = 0; 

// =========================
// FUNCTION DECLARATIONS
// =========================

void connectToWiFi();
void connectToMQTT();
void publishTelemetry();
void camera_config(camera_config_t &config);
void handleJpg(); 
void handleRoot();
void handleGPS();

// =========================
// SETUP
// =========================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== AquaVision Hybrid (Camera + GNSS + BMP280) ===");

    // =========================
    // 1. CAMERA SETUP (Deve avvenire per primo!)
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
    }
    else 
    {
        Serial.println("Camera Init Success");
        // Notare l'uso di camera_sensor_t invece di sensor_t per via del trucco!
        camera_sensor_t *s = esp_camera_sensor_get();
        if (s)
        {
            Serial.printf("Camera PID: 0x%02X\n", s->id.PID);
            s->set_brightness(s, 1);
        }
    }

    // =========================
    // 2. SENSOR BMP280 SETUP (Dopo la fotocamera)
    // =========================
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    
    if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
        Serial.println("Errore: BMP280 non trovato. Controlla I2C.");
        bmpAvailable = false;
    } else {
        bmpAvailable = true;
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,   
                        Adafruit_BMP280::SAMPLING_X16,  
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
        Serial.println("BMP280 Inizializzato con successo sul Bus condiviso!");
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

    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setBufferSize(1024, 1024);

    server.on("/", handleRoot);   
    server.on("/cam.jpg", handleJpg);
    server.on("/gps", handleGPS); 
    
    server.begin();
    Serial.println("HTTP Server started on port 80");
}

// =========================
// LOOP
// =========================

void loop()
{
    // Ascolta e gestisce le richieste HTTP
    server.handleClient();

    // Riconnessione MQTT NON bloccante
    if (!mqttClient.connected())
    {
        unsigned long currentMillis = millis();
        if (currentMillis - lastMQTTReconnectAttempt > 5000) 
        {
            lastMQTTReconnectAttempt = currentMillis;
            connectToMQTT();
        }
    }
    else 
    {
        mqttClient.loop();
    }

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
        if (mqttClient.connected()) {
            publishTelemetry();
        }
        lastPublish = millis();
    }
}

// =========================
// HTTP JPG HANDLER
// =========================

void handleJpg() {
    delay(50);
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed for HTTP request");
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// =========================
// ROOT PAGE 
// =========================

void handleRoot()
{
    String html;
    html += "<html><body>";
    html += "<h1>AquaVision GNSS + Camera + MQTT + BMP280</h1>";
    html += "<h2>GNSS Data</h2>";
    html += "<p><b>Latitude:</b> " + String(currentLat, 6) + "</p>";
    html += "<p><b>Longitude:</b> " + String(currentLon, 6) + "</p>";
    html += "<p><b>Altitude:</b> " + String(currentAlt, 2) + " m</p>";
    html += "<p><b>Satellites:</b> " + String(trackedSats) + "</p>";
    html += "<p><b>Speed:</b> " + String(boatSpeed, 2) + " knots (" + String(boatSpeed * 1.852, 2) + " km/h)</p>";
    html += "<p><b>Total Distance:</b> " + String(totalDistance, 2) + " m</p>";

    if (bmpAvailable) {
        html += "<h2>Atmosfera</h2>";
        html += "<p><b>Temperatura:</b> " + String(bmp.readTemperature(), 2) + " C</p>";
        html += "<p><b>Pressione:</b> " + String(bmp.readPressure() / 100.0F, 2) + " hPa</p>";
    }

    html += "<hr><a href='/gps'>GPS JSON</a><br><br>";
    html += "<img src='/cam.jpg' width='640'>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

// =========================
// GPS JSON OUTPUT 
// =========================

void handleGPS()
{
    String json = "{";
    json += "\"lat\":" + String(currentLat, 6) + ",";
    json += "\"lon\":" + String(currentLon, 6) + ",";
    json += "\"alt\":" + String(currentAlt, 2) + ",";
    json += "\"sats\":" + String(trackedSats) + ",";
    json += "\"speed_knots\":" + String(boatSpeed, 2) + ",";
    json += "\"speed_kmh\":" + String(boatSpeed * 1.852, 2) + ",";
    json += "\"distance\":" + String(totalDistance, 2);
    
    if (bmpAvailable) {
        json += ",\"temperature\":" + String(bmp.readTemperature(), 2) + ",";
        json += "\"pressure\":" + String(bmp.readPressure() / 100.0F, 2);
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

    Serial.println("\nWiFi Connected");
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
    }
}

// =========================
// TELEMETRY PUBLISHER
// =========================

void publishTelemetry()
{
    StaticJsonDocument<512> doc;

    if (bmpAvailable) {
        doc["temperature"] = bmp.readTemperature();
        doc["pressure"] = bmp.readPressure() / 100.0F;
    } else {
        doc["temperature"] = 0.0;
        doc["pressure"] = 0.0;
    }
    
    doc["lat"] = currentLat;
    doc["lon"] = currentLon;
    doc["alt"] = currentAlt;
    doc["sats"] = trackedSats;
    doc["speed"] = boatSpeed * 1.852;
    doc["distance"] = totalDistance;

    if (gps.hdop.isValid()) { doc["hdop"] = gps.hdop.value(); }
    if (gps.date.isValid()) {
        doc["day"] = gps.date.day();
        doc["month"] = gps.date.month();
        doc["year"] = gps.date.year();
    }
    if (gps.time.isValid()) {
        doc["hour"] = gps.time.hour();
        doc["minute"] = gps.time.minute();
        doc["second"] = gps.time.second();
    }

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