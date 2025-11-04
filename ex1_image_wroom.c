#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "board_config.h"

#include "FS.h"
#include "SD_MMC.h"
#include "EEPROM.h"

#define EEPROM_SIZE 1

// ===========================
// Enter your WiFi credentials
// ===========================
// const char *ssid = "**********";
// const char *password = "**********";

const char *ssid = "VirusNet";
const char *password = "Rxy2390g";

unsigned int pictureCount = 0;

AsyncWebServer server(80);


// void startCameraServer();
void setupLedFlash();
void server_setup(String path);


void configESPCamera() 
{

  Serial.begin(115200);
  Serial.setDebugOutput(true);
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
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  
}


void initMicroSDCard() 
{

#define SD_MMC_CMD  38 //Please do not modify it.
#define SD_MMC_CLK  39 //Please do not modify it. 
#define SD_MMC_D0   40 //Please do not modify it.

  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) 
  {
    Serial.println("Card Mount Failed");
    return;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  
  if(cardType == CARD_NONE)
  {
      Serial.println("No SD_MMC card attached");
      return;
  }
  
  Serial.print("SD_MMC Card Type: ");
  if(cardType == CARD_MMC)
  {
      Serial.println("MMC");
  } 
  else if(cardType == CARD_SD)
  {
      Serial.println("SDSC");
  } 
  else if(cardType == CARD_SDHC)
  {
      Serial.println("SDHC");
  } 
  else 
  {
      Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);  
  Serial.printf("Total space: %lluMB\r\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\r\n", SD_MMC.usedBytes() / (1024 * 1024));

}


void initWiFi() 
{

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

}

void server_setup(String path)
{
  fs::FS &fs = SD_MMC;
    
  server.on("/", HTTP_GET, [path](AsyncWebServerRequest *request)
  {
    request->send(fs, path.c_str(), "image/jpg");
  });
  
  server.begin();

}


void takeNewPhoto(String path) 
{
  // Setup frame buffer
  camera_fb_t  * fb = esp_camera_fb_get();
  
  if (!fb) 
  {
    Serial.println("Camera capture failed\n");
    return;
  }

  Serial.printf("Image width: %zu\n", fb->width);
  Serial.printf("Image height: %zu\n", fb->height);
  Serial.printf("Image len: %zu\n", fb->len);
  Serial.printf("pixel format: %d\n", fb->format);


  // Save picture to microSD card
  fs::FS &fs = SD_MMC;
    
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) 
  {
    Serial.println("Failed to open file in write mode");
  }
  else 
  {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    file.close();
  }

  // Return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);
  delay(1000);

}


void setup() 
{

  // Start Serial Monitor
  Serial.begin(115200);
  initWiFi();

  // Initialize the camera
  Serial.print("Initializing the camera module...\n");
  configESPCamera();
  Serial.println("Camera OK!");

  // Initialize the MicroSD
  Serial.print("Initializing the MicroSD card module...\n");
  initMicroSDCard();

  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  //EEPROM.write(0, 0); // reset 
  pictureCount = EEPROM.read(0) + 1;

  // Path where new picture will be saved in SD Card
  String path = "/image_" + String(pictureCount) + ".jpg";
  Serial.printf("Picture file name: %s\n", path.c_str());

  // Take and Save Photo
  takeNewPhoto(path);
  server_setup(path);

  // Update EEPROM picture number counter
  EEPROM.write(0, pictureCount);
  EEPROM.commit();

}


void loop() {
  // Do nothing. Everything is done in another task by the web server
  delay(10000);
}
