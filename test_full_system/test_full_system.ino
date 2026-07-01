#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>

#include "esp_camera.h"
#include "camera_pins.h"

// =========================
// WIFI CONFIGURATION
// =========================

const char* ssid = "DK_wifi";
const char* password = "ghtx4445";

// =========================
// GNSS CONFIGURATION
// =========================

#define RX_PIN 17
#define TX_PIN 18

TinyGPSPlus gps;

// =========================
// WEB SERVER
// =========================

WebServer server(80);

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

// =========================
// FUNCTION DECLARATIONS
// =========================

void connectToWiFi();
void camera_config(camera_config_t &config);

void handleRoot();
void handleCapture();
void handleGPS();

// =========================
// SETUP
// =========================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== AquaVision Camera + GNSS Test ===");

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
        Serial.printf(
            "Camera Init Failed: 0x%x\n",
            err
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println("Camera Init Success");

    sensor_t *s = esp_camera_sensor_get();

    if (s)
    {
        Serial.printf(
            "Camera PID: 0x%02X\n",
            s->id.PID
        );

        s->set_brightness(s, 1);
    }

    // =========================
    // GNSS SETUP
    // =========================

    Serial1.begin(
        115200,
        SERIAL_8N1,
        RX_PIN,
        TX_PIN
    );

    Serial.println(
        "GNSS Serial Started"
    );

    // =========================
    // WIFI
    // =========================

    connectToWiFi();

    // =========================
    // WEB SERVER ENDPOINTS
    // =========================

    server.on("/", handleRoot);

    server.on(
        "/capture",
        handleCapture
    );

    server.on(
        "/gps",
        handleGPS
    );

    server.begin();

    Serial.println();
    Serial.println("Web Server Started");

    Serial.print(
        "Homepage: http://"
    );

    Serial.println(
        WiFi.localIP()
    );
}

// =========================
// LOOP
// =========================

void loop()
{
    server.handleClient();

    while (Serial1.available())
    {
        gps.encode(
            Serial1.read()
        );
    }

    if (gps.location.isValid())
    {
        currentLat =
            gps.location.lat();

        currentLon =
            gps.location.lng();
    }
}
