
// https://dronebotworkshop.com/esp32-cam-microsd/
// https://github.com/espressif/esp32-camera

// Camera libraries
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"

// MicroSD Libraries
#include "FS.h"
#include "SD_MMC.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// EEPROM Library
#include "EEPROM.h"

// Use 1 byte of EEPROM space
#define EEPROM_SIZE 1

// Counter for picture number
unsigned int pictureCount = 0;

// Pin definitions for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22


#ifndef LED_GPIO_NUM 4
#define LED_GPIO_NUM 4
#endif

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
#endif
}

// const char *ssid = "RESNET_2G";
// const char *password = "#R2Net144*";

const char *ssid = "VirusNet";
const char *password = "Rxy2390g";

static uint8_t work[3100];

AsyncWebServer server(80);

uint8_t _compute_lbp(uint8_t *src, size_t r_center, size_t c_center, size_t width, size_t height);

void compute_lbp(uint8_t *src, size_t src_len, size_t width, size_t height, pixformat_t format, uint8_t **out, size_t *out_len);

void compute_histogram(uint8_t *buffer, size_t len, uint32_t **histogram);

void get_image_block(camera_fb_t *frame, camera_fb_t *block, uint8_t block_size, uint8_t row, uint8_t col);
void readImage(void);
bool jpg2gray(const uint8_t *src, size_t src_len, uint8_t **out, size_t *out_len);


void initMicroSDCard();
void initWiFi();
void create_dataset();

// based on https://github.com/espressif/esp32-camera/blob/master/conversions/to_bmp.c
bool jpg2gray(const uint8_t *src, size_t src_len, uint8_t **out, size_t *out_len) {
  esp_jpeg_image_cfg_t jpeg_cfg = {
    .indata = (uint8_t *)src,
    .indata_size = src_len,
    .out_format = JPEG_IMAGE_FORMAT_RGB888,
    .out_scale = JPEG_IMAGE_SCALE_0,
    .flags{ .swap_color_bytes = 0 },
    .advanced{ .working_buffer = work, .working_buffer_size = sizeof(work) },
    // .advanced{ .working_buffer_size = sizeof(work) },
  };

  bool ret = false;
  uint8_t *output = NULL;
  esp_jpeg_image_output_t output_img = {};

  if (esp_jpeg_get_image_info(&jpeg_cfg, &output_img) != ESP_OK) {
    Serial.println("Failed to get image info");
    //goto fail;
  }

  // @todo here we allocate memory and we assume that the user will free it
  // this is not the best way to do it, but we need to keep the API
  // compatible with the previous version
  size_t output_size = output_img.output_len;

  output = (uint8_t *)malloc(output_size);

  if (!output) {
    Serial.println("Failed to allocate output buffer");
    //goto fail;
  }

  // Start writing decoded data after the BMP header
  jpeg_cfg.outbuf = output;
  jpeg_cfg.outbuf_size = output_img.output_len;

  if (esp_jpeg_decode(&jpeg_cfg, &output_img) != ESP_OK) {
    Serial.println("JPEG decode failed");
    //goto fail;
  }


  // Serial.printf("JPEG image width: %zu \n", output_img.width);
  // Serial.printf("JPEG image height: %zu \n", output_img.height);
  // Serial.printf("JPEG image length: %zu \n", output_img.output_len);
  // Serial.printf("Reading pixel values\n");

  size_t size_WH = output_img.height * output_img.width;
  uint8_t *out_gray = (uint8_t *)malloc(size_WH);
  uint16_t _luma;

  for (size_t c = 0; c <size_WH; c++) 
  {
      _luma = (306 * output[c*3 + 0] + 601 * output[c*3 + 1] + 117 * output[c*3 + 2]) >> 10;  // divide by 1024
      out_gray[c] =  (uint8_t)_luma;      
  }
  
  *out = out_gray;
  *out_len = size_WH;
  ret = true;

fail:
  if (!ret && output) {
    free(output);
  }
  return ret;
}


void compute_histogram(uint8_t *buffer, size_t len, uint32_t **vector)
{

  uint32_t histogram[256] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


  for (size_t i=0; i < len; i++)
  {
    histogram[ buffer[i] ]++;
  }

  // Serial.printf("histogram :: 0:%d, 1:%d, 2:%d,  ...., 200:%d, 255:%d\n", histogram[0], histogram[1], histogram[2], histogram[200], histogram[255]);

  *vector = histogram;

}

void compute_lbp(uint8_t *src, size_t src_len, size_t width, size_t height, pixformat_t format, uint8_t **out, size_t *out_len)
{

  size_t _size_WH = (width * height);
  uint8_t *_out = (uint8_t*)malloc(_size_WH * sizeof(uint8_t));

  for (size_t r=0; r < height; r++)
  {

    for (size_t c=0; c < width; c++)
    {
      if (r == 0 || r == (height-1))
      {
        _out[ width * r + c] = 0;
        continue;
      }
        
      if (c == 0 || c == (width-1))
      {
        _out[ width * r + c] = 0;
        continue;
      }

      _out[ width * r + c] = _compute_lbp(src, r, c, width, height);
    }
  }

  Serial.printf("LBP is done\n");

  *out_len = _size_WH;
  *out = _out;
  
}



uint8_t _compute_lbp(uint8_t *src, size_t r_center, size_t c_center, size_t width, size_t height)
{
  uint8_t tmp = 0;
  uint8_t value = 0;
  uint8_t bp[] = {0, 0, 0, 0, 0, 0, 0, 0};
  
  // 0: I(row-1, col)  > I(row, col)
  if (src[ width * (r_center-1) + (c_center)] > src[ width * (r_center) + c_center])
    bp[0] = 1;
  
  // 1: I(row-1, col+1)  > I(row, col)
  if (src[ width * (r_center-1) + (c_center + 1)] > src[ width * (r_center) + c_center])
    bp[1] = 1;
  
  // 2: I(row, col+1)  > I(row, col)
  if (src[ width * (r_center) + (c_center + 1)] > src[ width * (r_center) + c_center])
    bp[2] = 1;
  
  // 3: I(row+1, col+1)  > I(row, col)
  if (src[ width * (r_center+1) + (c_center + 1)] > src[ width * (r_center) + c_center])
    bp[3] = 1;

  // 4: I(row+1, col)  > I(row, col)
  if (src[ width * (r_center+1) + (c_center)] > src[ width * (r_center) + c_center])
    bp[4] = 1;

  // 5: I(row+1, col-1)  > I(row, col)
  if (src[ width * (r_center+1) + (c_center-1)] > src[ width * (r_center) + c_center])
    bp[5] = 1;

  // 6: I(row, col-1)  > I(row, col)camera_fb_t  * block;
  if (src[ width * (r_center) + (c_center-1)] > src[ width * (r_center) + c_center])
    bp[6] = 1;

  // 7: I(row-1, col-1)  > I(row, col)
  if (src[ width * (r_center-1) + (c_center-1)] > src[ width * (r_center) + c_center])
    bp[7] = 1;


  value = bp[0] + bp[1]*2 + bp[2]*4 + bp[3]*8 + bp[4]*16 + bp[5]*32 + bp[6]*64 + bp[7]*128;
  return value;
}


void configESPCamera() {

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
  config.frame_size = FRAMESIZE_VGA;    //FRAMESIZE_UXGA;  FRAMESIZE_HD FRAMESIZE_VGA
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming  PIXFORMAT_GRAYSCALE PIXFORMAT_JPEG
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
      config.frame_size = FRAMESIZE_VGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_VGA;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }


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
  // if (config.pixel_format == PIXFORMAT_JPEG) {
  //   s->set_framesize(s, FRAMESIZE_HD);
  // }

  // #if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  //   s->set_vflip(s, 1);
  //   s->set_hmirror(s, 1);
  // #endif

  // #if defined(CAMERA_MODEL_ESP32S3_EYE)
  //   s->set_vflip(s, 1);
  // #endif

  // // Setup LED FLash if LED pin is defined in camera_pins.h
  // #if defined(LED_GPIO_NUM)
  //   setupLedFlash();
  // #endif
}

void initMicroSDCard() 
{
  Serial.println("Mounting MicroSD Card");
  if (!SD_MMC.begin()) 
  {
    Serial.println("MicroSD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) 
  {
    Serial.println("No MicroSD Card found");
    return;
  }
}



void takeNewPhoto(String path) 
{

  // Setup frame buffer
  camera_fb_t *fb = esp_camera_fb_get();

  uint8_t *gray_image = NULL;
  size_t gray_image_len = 0;

  uint32_t *histogram = NULL;

  uint8_t *jpg_buf = NULL;

  uint8_t *test_buffer = NULL;
  size_t test_len = 0;

  uint8_t *lbp_image = NULL;
  size_t lbp_image_len = 0;

  int success = 0;
  uint8_t quality = 50;  // the higher the better

  size_t height = 0;
  size_t width = 0;
  pixformat_t format;

  height = fb->height;
  width = fb->width;
  format = fb->format;


  if (!fb) 
  {
    Serial.println("Camera capture failed\n");
    return;
  }

  Serial.printf("Image width: %zu\n", fb->width);
  Serial.printf("Image height: %zu\n", fb->height);
  Serial.printf("Image len: %zu\n", fb->len);
  Serial.printf("pixel format: %zu\n", fb->format);
  
  // save captured image
  fs::FS &fs = SD_MMC;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) 
  {
    Serial.println("Failed to open file in write mode");
    return;
  }

  file.write(fb->buf, fb->len);
  Serial.println("Saved acquired image");

  // convert to gray scale
  if (fb->format == PIXFORMAT_JPEG) 
  {
    // Serial.printf("[DEBUG] Before malloc: free heap %d\n", esp_get_free_heap_size());
    success = jpg2gray(fb->buf, fb->len, &gray_image, &gray_image_len);

    // Return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);

    Serial.printf("Convertion succeeded: %d \n", success);
    Serial.printf("Buffer size: %zu \n", gray_image_len);
  
    // now that we have a uncompressed grayscale image, let us convert it to jpeg

    // Serial.printf("[DEBUG] Before malloc: free heap %d\n", esp_get_free_heap_size());
    fmt2jpg(gray_image, gray_image_len, width, height, PIXFORMAT_GRAYSCALE, quality, &test_buffer, &test_len);

    // Serial.printf("Buffer size compressed to jpg: %zu \n", test_len);

    String path2 = "/gray_image.jpg";
    File file2 = fs.open(path2.c_str(), FILE_WRITE);
    
    if (!file2) 
    {
      Serial.println("[GRAYSCALE] Failed to open file in write mode");
      return;
    }

    file2.write(test_buffer, test_len);
    free(test_buffer);


    // extract features -> compute lbp 
    compute_lbp(gray_image, gray_image_len, width, height, format, &lbp_image, &lbp_image_len);

    String path3 = "/lbp.jpg";
    File file3 = fs.open(path3.c_str(), FILE_WRITE);
    
    if (!file3) 
    {
      Serial.println("[LBP] Failed to open file in write mode");
      return;
    }

    file3.write(lbp_image, lbp_image_len);


    // compute histogram
    compute_histogram(lbp_image, lbp_image_len, &histogram);

  }





  free(gray_image);
  free(test_buffer);

  
  

  //digitalWrite(LED_BUILTIN, LOW);
}


void check_attendance() 
{

// Start Serial Monitor
  Serial.begin(115200);
  Serial.printf("[DEBUG] Available heap at start up: free heap %d\n", esp_get_free_heap_size());
  
  //initWiFi();

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

  initWiFi();
  server_setup(path);

  // Update EEPROM picture number counter
  EEPROM.write(0, pictureCount);
  EEPROM.commit();


}


void create_dataset(void)
{

  // fs::FS &fs = SD_MMC;
  // const char * dirname = "/";
  // uint8_t levels = 4;

  // Serial.begin(115200);

  // // Initialize the MicroSD
  // Serial.print("Initializing the MicroSD card module...\n");
  // initMicroSDCard();

  // File file = fs.open(path);
  
  // if(!file)
  // {
  //   Serial.println("Failed to open file for reading");
  //   return;
  // }

  // Serial.print("Read from file: ");
  // while(file.available())
  // {
  //   Serial.write(file.read());
  // }
  // file.close();


  

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


void readImage(String filename) 
{
  fs::FS &fs = SD_MMC;

  File imageFile = fs.open(filename.c_str(), FILE_READ);

  if (!imageFile) 
  {
    Serial.println("Failed to open image file");
    return;
  }

  Serial.println("Reading JPG file...");
  size_t fileSize = imageFile.size();
  uint8_t *imageBuffer = (uint8_t *)malloc(fileSize);

  if (!imageBuffer) 
  {
    Serial.println("Failed to allocate memory for image");
    imageFile.close();
    return;
  }

  if (imageFile.read(imageBuffer, fileSize) != fileSize) 
  {
    Serial.println("Failed to read entire image file");
    free(imageBuffer);
    imageFile.close();
    return;
  }

  imageFile.close();

  camera_fb_t *fb = (camera_fb_t *)malloc(sizeof(camera_fb_t));
  if (!fb) 
  {
    Serial.println("Failed to allocate memory for camera_fb_t");
    free(imageBuffer);
    return;
  }

  fb->buf = imageBuffer;
  fb->len = fileSize;
  fb->format = PIXFORMAT_JPEG;  // Assuming the image is a JPEG
  fb->width = 1280;             // Set the width of your image
  fb->height = 720;             // Set the height of your image  1280 × 720
    // Other fields like timestamp can be set if needed

  uint8_t *rbg_buf;
  size_t out_len;



  // fmt2rgb888(const uint8_t *src_buf, size_t src_len, pixformat_t format, uint8_t *rgb_buf)
  // fmt2rgb888(fb->buf, fb->len, fb->format, rbg_buf);
  // bool jpg2gray(const uint8_t *src, size_t src_len, uint8_t ** out, size_t * out_len);
  // &jpg_buf, &jpg_size

  jpg2gray(fb->buf, fb->len, &rbg_buf, &out_len);

  Serial.printf("Image was read: %zu  \n", fileSize);
}

void server_setup(String path) 
{

  fs::FS &fs = SD_MMC;

  server.on("/", HTTP_GET, [path](AsyncWebServerRequest *request) {
    request->send(fs, path.c_str(), "image/jpg");
  });

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(fs, "/gray_image.jpg", "image/jpg");
  });

  //server.serveStatic("/", SD_MMC, "/");

  server.begin();
}


void setup() 
{

  

  check_attendance();

  // create_dataset();


}

void loop() 
{

}