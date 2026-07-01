#define MQTT_MAX_PACKET_SIZE 16384

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h> // Library to decode satellite NMEA sentences
#include <base64.h> 
#include "esp_camera.h"
#include "camera_pins.h"

// wifi info
const char *ssid = "DK_wifi";
const char *password = "ghtx4445";

// --- MQTT Broker Configuration ---
const char* mqtt_broker = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* mqtt_topic = "esp32/telemetry";

// --- Hardware Interface Drivers ---
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- Dynamic Telemetry Storage Variables ---
// Variables are now initialized to 0.0 and will be overwritten by live satellite data
double currentLat  = 0.0; 
double currentLon  = 0.0; 
double currentAlt  = 0.0;      
int    trackedSats = 0;        
float  boatSpeed   = 0.0;       
float  airPressure = 1013.25; // Placeholder value (Requires an external I2C sensor like BMP280)

unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 5000; // MODIFIED: Exactly 5 seconds execution interval

void connectToWiFi();
void connectToMQTT();
void publishTelemetry();

// function for configuring the camera driver object
void camera_config(camera_config_t &config);
// function to capture an image
camera_fb_t *captureImage();
// functions for camera to website through mqtt
String capture_image_base64();

void setup() {
  // Primary USB Port execution stream monitoring
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(1000);
  Serial.println("\n--- AquaVision Real-Time Telemetry Pipeline ---");

  // setting up the driver
  camera_config_t config;
  camera_config(config);

  // camera is in psram
  if (psramFound())
  {
    Serial.println("PSRAM Found");

    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }

  // camera is in dram
  else
  {
    Serial.println("PSRAM NOT Found");

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
      "\nCamera Init Failed! Error: 0x%x\n",
      err
    );

    while (true)
    {
      delay(1000);
    }
  }

  // setting up camera parameters and printing its info
  Serial.println("Camera Init Success!");


  sensor_t *s = esp_camera_sensor_get();
  Serial.println("Reading Camera Info");
  Serial.printf("PID: 0x%02X\n", s->id.PID);
  s->set_brightness(s, 1);
  Serial.println("Brightness command sent");

  // recoginse the model of the camera
  if (s != nullptr)
  {
    Serial.printf(
      "Detected Sensor PID: 0x%02X\n",
      s->id.PID
    );
  }

  connectToWiFi();
  mqttClient.setServer(mqtt_broker, mqtt_port);
}

void loop() {
  // 1. Maintain active background data broker connection lines
  if (!mqttClient.connected()) {
    connectToMQTT();
  }
  mqttClient.loop();

  // 3. EXECUTION TIMER: Publish data packets precisely every 5 seconds
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;

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

void publishTelemetry()
{

  StaticJsonDocument<256> doc;
  doc["temperature"]= 22.4;
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

    bool success =
        mqttClient.publish(mqtt_topic, jsonBuffer);

    if(success)
    {
        Serial.println("Publish SUCCESS");
        Serial.println(jsonBuffer);
    }
    else
    {
        Serial.println("Publish FAILED");
    }
}

// function that configures the camera driver
void camera_config(camera_config_t &config){
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

// function to capture an image 
camera_fb_t *captureImage()
{
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb)
    {
        Serial.println("Capture failed");
        return nullptr;
    }

    return fb;
}

// image base64 function
String capture_image_base64()
{
    camera_fb_t *fb = captureImage();

    if (!fb)
    {
        Serial.println("Failed to capture image");
        return "";
    }

    String encodedImage =
        base64::encode(
            (uint8_t*)fb->buf,
            fb->len
        );

    esp_camera_fb_return(fb);

    return encodedImage;
}