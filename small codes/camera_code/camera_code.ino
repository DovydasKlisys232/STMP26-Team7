// ============================================================================
// 1. CRITICAL NETWORK BUFFER CONFIGURATION (MUST BE THE FIRST LINE)
// ============================================================================
// Expand PubSubClient default buffer to hold a 2 KB data chunk plus headers
#define MQTT_MAX_PACKET_SIZE 3072 

#include <WiFi.h>
#include <TinyGPS++.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "esp_camera.h"
#include "camera_pins.h" // Camera hardware mappings mapping OV2640 pins

// ============================================================================
// NETWORK & MQTT BROKER SETTINGS
// ============================================================================
const char* ssid = "DK_wifi";
const char* password = "ghtx4445";

// Local Private Broker IP for high-speed network transfers
const char* mqtt_server = "10.26.119.138";
const int mqtt_port = 1883;

// Isolated MQTT topics to split numerical data from massive binary streams
const char* mqtt_topic = "esp32/telemetry";
const char* mqtt_photo_topic = "esp32/telemetry/photo_blob";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================================
// GNSS SUBSYSTEM CONFIGURATION (WAVESHARE HAT)
// ============================================================================
// Pins 17 and 18 bypass onboard TFT screen pin collisions on Tenstar board
#define RX_PIN 17
#define TX_PIN 18

TinyGPSPlus gps;

// ============================================================================
// TELEMETRY STATE CONTAINERS
// ============================================================================
double currentLat = 0.0;
double currentLon = 0.0;
double currentAlt = 0.0;
int trackedSats = 0;
float boatSpeed = 0.0;
float airPressure = 1013.25;


// Odometer Variables
double totalDistance = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;

// Execution Timing Control (Non-blocking loop mechanism)
unsigned long lastPublish = 0;
const unsigned long publishInterval = 5000; // Trigger data cycles every 5 seconds

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void connectToWiFi();
void connectToMQTT();
void camera_config(camera_config_t &config);
void publishTelemetry();
void publishPhotoInChunks();

// ============================================================================
// INITIALIZATION BLOCK (EXECUTES ONCE AT POWER ON)
// ============================================================================
void setup()
{
    Serial.begin(115200); // USB Debugging Monitor Serial link
    delay(1000);

    Serial.println("\n=== AquaVision Camera + GNSS Test (Optimized Chunking) ===");

    // ------------------------------------------------------------------------
    // C&C CAMERA ARCHITECTURE HARDWARE ALLOCATION
    // ------------------------------------------------------------------------
    camera_config_t config;
    camera_config(config);

    // Dynamic Hardware Memory Check (PSRAM allocations optimize image buffering)
    if (psramFound())
    {
        Serial.println("PSRAM FOUND: Activating High Performance Mode");
        config.frame_size = FRAMESIZE_QVGA;     // Balanced resolution: 320x240
        config.jpeg_quality = 15;              // Compression factor (lower = sharper)
        config.fb_count = 2;                   // Double buffering for zero frame drops
        config.grab_mode = CAMERA_GRAB_LATEST;  // Always grab fresh image data
        config.fb_location = CAMERA_FB_IN_PSRAM;// Leverage external high capacity RAM
    }
    else
    {
        Serial.println("PSRAM NOT FOUND: Activating Low Memory Fallback Mode");
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;                   // Single buffer constraint
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location = CAMERA_FB_IN_DRAM; // Force into volatile internal RAM
    }

    Serial.println("Initializing Camera Subsystem...");
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera Init Failed: 0x%x\n", err);
        while (true) { delay(1000); } // Infinite safety trap if camera hardware is missing
    }
    Serial.println("Camera Init Success");

    sensor_t *s = esp_camera_sensor_get();
    if (s)
    {
        Serial.printf("Camera PID: 0x%02X\n", s->id.PID);
        s->set_brightness(s, 1); // Boost brightness level for aquatic marine deployment
    }

    // ------------------------------------------------------------------------
    // SECONDARY HARDWARE SERIAL PORT (GNSS INGESTION)
    // ------------------------------------------------------------------------
    // Waveshare hardware jumper MUST be set to Position B to communicate at 115200 baud
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial.println("GNSS Serial Line Initialized");

    // ------------------------------------------------------------------------
    // NETWORK STACK LAYER ACTIVATION
    // ------------------------------------------------------------------------
    connectToWiFi();
    mqttClient.setServer(mqtt_server, mqtt_port);
}

// ============================================================================
// CONTINUOUS PROCESSING LOOP
// ============================================================================
void loop()
{
    // Maintain broker heartbeat line
    if (!mqttClient.connected())
    {
        connectToMQTT();
    }
    mqttClient.loop(); // Handle background packet acknowledgements

    // Continuous Ingestion: Stream raw NMEA sentences from Waveshare Module
    while (Serial1.available())
    {
        gps.encode(Serial1.read()); // TinyGPS++ decodes stream char by char
    }

    // Filter lock: Process data only if satellite constellation fix is active
    if (gps.location.isValid())
    {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();

        if (gps.altitude.isValid())   { currentAlt = gps.altitude.meters(); }
        if (gps.satellites.isValid()) { trackedSats = gps.satellites.value(); }
        if (gps.speed.isValid())      { boatSpeed = gps.speed.knots(); }

        // Odometry Algorithm with Static Noise Filtering
        if (lastLat != 0.0 && lastLon != 0.0)
        {
            double moved = TinyGPSPlus::distanceBetween(currentLat, currentLon, lastLat, lastLon);
            
            // Filters out satellite multi-path drift jitter when stationary
            if (moved > 2.0) 
            {
                totalDistance += moved; // Accumulate genuine trip distance meters
            }
        }
        lastLat = currentLat;
        lastLon = currentLon;
    }

    // Non-blocking timer tracking execution cycles without using harmful delays
    if (millis() - lastPublish >= publishInterval)
    {
        publishTelemetry();     // Pipeline 1: Push lightweight numerical telemetry
        publishPhotoInChunks();  // Pipeline 2: Stream camera binary payload in 2 KB chunks
        lastPublish = millis();
    }
}

// ============================================================================
// PIPELINE 1: LIGHTWEIGHT DATA STRATIFICATION (JSON LITE)
// ============================================================================
void publishTelemetry()
{
    // Small memory allocation since large strings/base64 are stripped out
    StaticJsonDocument<512> doc;

    doc["temperature"] = 22.4;
    doc["lat"] = currentLat;
    doc["lon"] = currentLon;
    doc["alt"] = currentAlt;
    doc["sats"] = trackedSats;
    doc["speed"] = boatSpeed * 1.852; // Convert raw marine knots to km/h
    doc["distance"] = totalDistance;

    if (gps.hdop.isValid()) { doc["hdop"] = gps.hdop.value(); }

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

    String payload;
    serializeJson(doc, payload);

    bool success = mqttClient.publish(mqtt_topic, payload.c_str());
    if (success)
    {
        Serial.println("--> Telemetry JSON Publish SUCCESS");
        Serial.println(payload);
    }
    else
    {
        Serial.println("--> Telemetry JSON Publish FAILED");
    }
}

// ============================================================================
// PIPELINE 2: ZERO-COPY BINARY PAYLOAD CHUNKING (HIGH SPEED STREAMING)
// ============================================================================
void publishPhotoInChunks()
{
    // 1. Fetch direct pointer reference to the raw captured image frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera Capture Failed");
        return;
    }

    size_t image_size = fb->len;
    size_t chunk_size = 2048; // Max slice transmission window (2 KB fragments)
    size_t bytes_sent = 0;

    Serial.printf("Starting Binary Photo Stream... Total size: %d bytes\n", image_size);

    // 2. Initiate low-level fixed packet layout stream to destination topic
    // This tells the broker exactly how many total bytes are coming across the line
    if (mqttClient.beginPublish(mqtt_photo_topic, image_size, false))
    {
        while (bytes_sent < image_size)
        {
            // Dynamically calculate leftover slice sizes
            size_t bytes_to_send = min(chunk_size, image_size - bytes_sent);
            
            // Stream raw bytes directly out of memory buffer into network socket
            // No Base64 overhead, no CPU performance degradation
            mqttClient.write(fb->buf + bytes_sent, bytes_to_send);
            bytes_sent += bytes_to_send;
        }
        
        // 3. Finalize transmission block
        if (mqttClient.endPublish())
        {
            Serial.println("--> Binary Photo Stream SUCCESS!");
        }
        else
        {
            Serial.println("--> MQTT Photo Stream FAILED at endPublish.");
        }
    }
    else
    {
        Serial.println("--> Could not initiate MQTT beginPublish pipeline.");
    }

    // 4. CRITICAL: Hand back buffer memory lock to prevent system Heap memory leakage
    esp_camera_fb_return(fb);
}

// ============================================================================
// INFRASTRUCTURE LAYER SUBSYSTEMS
// ============================================================================
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

void connectToMQTT()
{
    while (!mqttClient.connected())
    {
        // Enforce random UUID generation to avoid client ID broker collision drops
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
            delay(5000); // Standby cooldown window before trying state re-entry
        }
    }
}

// Standard structural camera matrix pin binding definition configurations
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

    config.xclk_freq_hz = 20000000; // Drive standard internal clock processing frequency
    config.pixel_format = PIXFORMAT_JPEG; // Directly output compressed format data
}