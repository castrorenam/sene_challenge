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


  Serial.printf("JPEG image width: %zu \n", output_img.width);
  Serial.printf("JPEG image height: %zu \n", output_img.height);
  Serial.printf("JPEG image length: %zu \n", output_img.output_len);
  Serial.printf("Reading pixel values\n");

  size_t size_WH = output_img.height * output_img.width;
  //uint8_t *out_gray = (uint8_t *)malloc(size_WH * sizeof(uint8_t));
  uint8_t *out_gray = (uint8_t *)malloc(size_WH);
  size_t idx;
  uint16_t _luma;

  for (size_t c = 0; c <size_WH; c++) 
  {
      _luma = (306 * output[c*3 + 0] + 601 * output[c*3 + 1] + 117 * output[c*3 + 2]) >> 10;  // divide by 1024
      out_gray[c] =  (uint8_t)_luma;      
  }

  Serial.printf("Max idx : %zu\n", idx);
  Serial.printf("End reading pixel values\n");
  
  *out = out_gray;
  //*out_len = output_size;
  *out_len = size_WH;
  ret = true;

fail:
  if (!ret && output) {
    free(output);
  }
  return ret;
}