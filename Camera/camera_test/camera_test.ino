#include "esp_camera.h"

// Camera Control Pins
#define PWDN_GPIO_NUM    16
#define RESET_GPIO_NUM   15

// Unknown DCLK pin (testing as XCLK)
#define PCLK_GPIO_NUM   17
#define XCLK_GPIO_NUM   7

// SCCB / I2C
#define SIOD_GPIO_NUM    42
#define SIOC_GPIO_NUM    41

// Camera Data Bus
#define Y2_GPIO_NUM      5
#define Y3_GPIO_NUM      6
#define Y4_GPIO_NUM      18
#define Y5_GPIO_NUM      8
#define Y6_GPIO_NUM      9
#define Y7_GPIO_NUM      10
#define Y8_GPIO_NUM      11
#define Y9_GPIO_NUM      12

// Sync Pins
#define VSYNC_GPIO_NUM   13
#define HREF_GPIO_NUM    14

// TEMPORARY TEST
// We don't know the real PCLK pin yet.
#define PCLK_GPIO_NUM    17

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("OV2640 Test Starting...");
  Serial.println();

  camera_config_t config;

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

  if (psramFound())
  {
    Serial.println("PSRAM Found");

    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }
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

  Serial.println("Camera Init Success!");

  sensor_t *s = esp_camera_sensor_get();

  
  Serial.println("Reading Camera Info");

  Serial.printf("PID: 0x%02X\n", s->id.PID);

  s->set_brightness(s, 1);

  Serial.println("Brightness command sent");


  if (s != nullptr)
  {
    Serial.printf(
      "Detected Sensor PID: 0x%02X\n",
      s->id.PID
    );
  }

  Serial.println("Attempting Image Capture...");

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb)
  {
    Serial.println("Frame Capture Failed!");
    return;
  }

  Serial.println("Frame Capture Success!");
  Serial.printf("Image Size: %u bytes\n", fb->len);
  Serial.printf("Width: %u\n", fb->width);
  Serial.printf("Height: %u\n", fb->height);

  esp_camera_fb_return(fb);

  Serial.println();
  Serial.println("Test Complete.");
}

void loop()
{
  delay(1000);
}