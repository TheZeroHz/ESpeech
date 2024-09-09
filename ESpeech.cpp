#include "ESpeech.h"
#define SERVER_URL "http://192.168.0.106:8888/uploadAudio" // Change the IP Address according To Your Server's config
#define I2S_WS 16
#define I2S_SD 7
#define I2S_SCK 15
#define I2S_PORT I2S_NUM_1
#define I2S_SAMPLE_RATE (16000)
#define I2S_SAMPLE_BITS (16)
#define I2S_READ_LEN (16 * 1024)
#define RECORD_TIME (5) // Your Desired Time In Seconds [note: If want to increase record time then change partition type]
#define I2S_CHANNEL_NUM (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)

ESpeech::ESpeech(const char* ssid, const char* password, const char* serverUrl)
    : ssid(ssid), password(password), serverUrl(serverUrl){}

void ESpeech::begin() {
    wifiConnect();
}




void ESpeech::recordAudio() {

    Serial.printf("recordAudio -1");
    delay(50);
    i2sInit(); 
    delay(50);
    Serial.printf("recordAudio 0"); 
    i2s_zero_dma_buffer(I2S_PORT); //clear DMA Buffers 
  delay(50);
    Serial.printf("recordAudio 1");
    FFATInit();                     // Reinitialize FFat for new file
    delay(500);                     // Add a small delay
    Serial.printf("recordAudio 2");
                     // Reinitialize I2S
    Serial.printf("recordAudio 3");
    i2s_adc();                      // Perform recording
    Serial.printf("recordAudio 4");
    i2s_zero_dma_buffer(I2S_PORT);
    delay(50);
}

String ESpeech::getTranscription() {
  String response="";
  unsigned long t1=millis();
  file = FFat.open(filename, FILE_READ);
  if (!file) {
    Serial.println("FILE IS NOT AVAILABLE!");
  }
  HTTPClient client;
  client.begin(SERVER_URL); // python wsgi server address
  client.addHeader("Content-Type", "audio/wav");
  int httpResponseCode = client.sendRequest("POST", &file, file.size());
  Serial.print("httpResponseCode: ");
  Serial.println(httpResponseCode);
  if (httpResponseCode == 200) {
     stt= client.getString();
    Serial.println("User:=>"+stt);
    const size_t capacity = JSON_OBJECT_SIZE(1) + 50;
    DynamicJsonDocument doc(capacity);
    DeserializationError error = deserializeJson(doc, stt);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
  }
   response = doc["transcription"].as<String>();
  } else {
    Serial.println("Error in HTTP request");
  }
  file.close();
  client.end();
  Serial.print("Total Time Taken:");
  Serial.println(millis()-t1);

  return response;
}

void ESpeech::FFATInit() {
  Serial.printf("ffat init 0");
    if (!FFat.begin(true)) {
        Serial.println("FFAT initialization failed!");
        while (1) yield();
    }
    Serial.printf("ffat init 1");
    listFFAT();
    Serial.printf("ffat init 2");
}



void ESpeech::i2sInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
        .data_in_num = I2S_SD
    };

    i2s_set_pin(I2S_PORT, &pin_config);
}

void ESpeech::i2sDeInit(){  
  i2s_zero_dma_buffer(I2S_PORT); //clear DMA Buffers  
  i2s_driver_uninstall(I2S_PORT); //stop & destroy i2s driver
  }

void ESpeech::i2s_adc() {
    int i2s_read_len = I2S_READ_LEN;
    int flash_wr_size = 0;
    size_t bytes_read;

    char *i2s_read_buff = (char *)calloc(i2s_read_len, sizeof(char));
    uint8_t *flash_write_buff = (uint8_t *)calloc(i2s_read_len, sizeof(char));
    FFat.remove(filename);
    file = FFat.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("File is not available!");
    }

    byte header[headerSize];
    wavHeader(header, FLASH_RECORD_SIZE);
    file.write(header, headerSize);
    Serial.println(" *** Recording Start *** ");
    while (flash_wr_size < FLASH_RECORD_SIZE) {
        i2s_read(I2S_PORT, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
        i2s_adc_data_scale(flash_write_buff, (uint8_t *)i2s_read_buff, i2s_read_len);
        file.write((const byte *)flash_write_buff, i2s_read_len);
        flash_wr_size += i2s_read_len;
        ets_printf("Sound recording %u%%\n", flash_wr_size * 100 / FLASH_RECORD_SIZE);
        ets_printf("Never Used Stack Size: %u\n", uxTaskGetStackHighWaterMark(NULL));
    }
    file.close();
    if(i2s_read_buff!=NULL){
    free(i2s_read_buff);
    i2s_read_buff = NULL;
    }
    if(flash_write_buff!=NULL){
    free(flash_write_buff);
    flash_write_buff = NULL;}
    listFFAT();
}

void ESpeech::i2s_adc_data_scale(uint8_t *d_buff, uint8_t *s_buff, uint32_t len) {
    uint32_t j = 0;
    uint32_t dac_value = 0;
    for (int i = 0; i < len; i += 2) {
        dac_value = ((((uint16_t)(s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 2048;
    }
}

void ESpeech::wavHeader(byte *header, int wavSize) {
    header[0] = 'R';
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    unsigned int fileSize = wavSize + headerSize - 8;
    header[4] = (byte)(fileSize & 0xFF);
    header[5] = (byte)((fileSize >> 8) & 0xFF);
    header[6] = (byte)((fileSize >> 16) & 0xFF);
    header[7] = (byte)((fileSize >> 24) & 0xFF);
    header[8] = 'W';
    header[9] = 'A';
    header[10] = 'V';
    header[11] = 'E';
    header[12] = 'f';
    header[13] = 'm';
    header[14] = 't';
    header[15] = ' ';
    header[16] = 0x10;
    header[17] = 0x00;
    header[18] = 0x00;
    header[19] = 0x00;
    header[20] = 0x01;
    header[21] = 0x00;
    header[22] = 0x01;
    header[23] = 0x00;
    header[24] = 0x80;
    header[25] = 0x3E;
    header[26] = 0x00;
    header[27] = 0x00;
    header[28] = 0x00;
    header[29] = 0x7D;
    header[30] = 0x01;
    header[31] = 0x00;
    header[32] = 0x02;
    header[33] = 0x00;
    header[34] = 0x10;
    header[35] = 0x00;
    header[36] = 'd';
    header[37] = 'a';
    header[38] = 't';
    header[39] = 'a';
    header[40] = (byte)(wavSize & 0xFF);
    header[41] = (byte)((wavSize >> 8) & 0xFF);
    header[42] = (byte)((wavSize >> 16) & 0xFF);
    header[43] = (byte)((wavSize >> 24) & 0xFF);
}


void ESpeech::listFFAT() {
    Serial.println(F("\r\nListing FFAT files:"));
    static const char line[] PROGMEM = "=================================================";

    Serial.println(FPSTR(line));
    Serial.println(F("  File name                              Size"));
    Serial.println(FPSTR(line));

    fs::File root = FFat.open("/");
    if (!root) {
        Serial.println(F("Failed to open directory"));
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(F("Not a directory"));
        return;
    }

    fs::File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("DIR : ");
            String fileName = file.name();
            Serial.print(fileName);
        } else {
            String fileName = file.name();
            Serial.print("  " + fileName);
            int spaces = 33 - fileName.length();
            if (spaces < 1) spaces = 1;
            while (spaces--) Serial.print(" ");
            String fileSize = (String)file.size();
            spaces = 10 - fileSize.length();
            if (spaces < 1) spaces = 1;
            while (spaces--) Serial.print(" ");
            Serial.println(fileSize + " bytes");
        }
        file = root.openNextFile();
    }
    Serial.println(FPSTR(line));
    Serial.println();
    delay(1000);
}


void ESpeech::wifiConnect() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
}
