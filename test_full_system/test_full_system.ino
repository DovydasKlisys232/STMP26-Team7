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
            double moved =
                TinyGPSPlus::distanceBetween(
                    currentLat,
                    currentLon,
                    lastLat,
                    lastLon
                );

            if (moved > 2.0)
            {
                totalDistance += moved;
            }
        }

        lastLat = currentLat;
        lastLon = currentLon;
    }
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
// HOME PAGE
// =========================

void handleRoot()
{
    String html;

    html += "<html><body>";
    html += "<h1>AquaVision GNSS + Camera Test</h1>";

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
    html += " knots</p>";

    html += "<p><b>Speed:</b> ";
    html += String(boatSpeed * 1.852, 2);
    html += " km/h</p>";

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

    html += "<img src='/capture' width='640'>";

    html += "</body></html>";

    server.send(
        200,
        "text/html",
        html
    );
}

// =========================
// GPS JSON OUTPUT
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

    server.send(
        200,
        "application/json",
        json
    );
}

// =========================
// CAMERA CAPTURE
// =========================

void handleCapture()
{
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb)
    {
        server.send(
            500,
            "text/plain",
            "Camera Capture Failed"
        );

        return;
    }

    server.send_P(
        200,
        "image/jpeg",
        (const char *)fb->buf,
        fb->len
    );

    esp_camera_fb_return(fb);
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
