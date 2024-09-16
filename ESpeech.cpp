#include "ESpeech.h"
#include "VADCoreESP32.h"
#define I2S_SAMPLE_RATE (16000)
#define I2S_SAMPLE_BITS (16)
#define I2S_READ_LEN (1024)

ESpeech::ESpeech(i2s_port_t i2s_port,int i2s_sck,int i2s_ws,int i2s_sd)
    : I2S_PORT(i2s_port), I2S_SCK(i2s_sck), I2S_WS(i2s_ws),I2S_SD(i2s_sd){}

void ESpeech::begin(const char* server_Url){
    serverUrl=server_Url;
}




void ESpeech::recordAudio() {
    i2sInit(); 
    i2s_zero_dma_buffer(I2S_PORT);
    FFATInit();
    i2s_adc();
    i2s_zero_dma_buffer(I2S_PORT);
}


String ESpeech::getTranscription() {
  String response="";
  unsigned long t1=millis();
  file = FFat.open(filename, FILE_READ);
  if (!file) {
    Serial.println("FILE IS NOT AVAILABLE!");
  }
  HTTPClient client;
  client.begin(serverUrl); // python wsgi server address
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
  Serial.print("Total Time Taken TTS:");
  Serial.println(millis()-t1);
  return response;
}

void ESpeech::FFATInit() {
  Serial.printf("ffat init 0");
    if (!FFat.begin(true)) {
        Serial.println("FFAT initialization failed!");
        while (1) yield();
    }
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
        .dma_buf_len = 1024,
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


void ESpeech::varyGain(uint8_t * buf,uint32_t len, int16_t gain)
{
    int16_t tempIS, tempUS;
    float value;

    for (int i = 0; i<(len/2); i++) {
        tempIS = buf[i*2+1] << 8 | buf[i*2];
        value = tempIS/32768.00000;
        value = value*gain; 
        if (value>1) value = 1;                   //clipping
        if (value<-1) value = -1;
        tempUS = value*32768;
        
        buf[i*2] = 0x00FF&tempUS;
        buf[i*2+1] = tempUS>>8;
    }
}



void ESpeech::i2s_adc() {
    size_t bytes_read;
    uint8_t *i2s_read_buff = (uint8_t *)calloc(I2S_READ_LEN, sizeof(uint8_t));
    //uint8_t *i2s_read_buff2 = (uint8_t *)calloc(I2S_READ_LEN, sizeof(uint8_t));
    FFat.remove(filename);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    file = FFat.open(filename, FILE_WRITE);
    // Write a placeholder WAV header
    byte header[headerSize] = {0};
    wavHeader(header, 0); // Write initial header with zero data size
    file.write(header, headerSize);
    Serial.println(" *** Recording Start *** ");

    // Initialize VAD object
    VADCoreESP32 vad;
    vad.i2sInit(I2S_PORT, I2S_SCK, I2S_WS, I2S_SD);
    vad.setCore(1); // Uncomment if your board has 2 cores
    vad.setPriority(1); // Uncomment if your board has 2 cores
    vad.start(); // Start VAD task
    int16_t gain_factor=30;  //1-40 is preffereble if you are fetching low sound issue
    size_t recordedSize = 0;
    while (true) {
        i2s_read(I2S_PORT, (void *)i2s_read_buff, I2S_READ_LEN, &bytes_read, portMAX_DELAY);
        varyGain(i2s_read_buff,I2S_READ_LEN,30);
        //i2s_adc_data_scale(i2s_read_buff2,i2s_read_buff,I2S_READ_LEN,gain_factor);
        file.write((const byte *)i2s_read_buff, I2S_READ_LEN);
        recordedSize += I2S_READ_LEN;
        ets_printf("Sound recording\n");
        if (vad.getState() == VAD_SILENCE) {
            ets_printf("Recording stop\n");
            break;
        }
    }
    
    // Update WAV header with actual recorded size
    file.seek(4); // Move to file size field
    uint32_t fileSize = recordedSize + 36; // File size = dataSize + 36
    file.write((byte*)&fileSize, sizeof(fileSize));
    file.seek(40); // Move to data size field
    file.write((byte*)&recordedSize, sizeof(recordedSize));

    file.close();
    free(i2s_read_buff);
    //free(i2s_read_buff2);
}




void ESpeech::wavHeader(byte *header, int wavSize) {
    // RIFF Chunk Descriptor
    header[0] = 'R';  // ChunkID
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    unsigned int fileSize = wavSize + 36; // File size = dataSize + 36
    header[4] = (byte)(fileSize & 0xFF);        // ChunkSize
    header[5] = (byte)((fileSize >> 8) & 0xFF);
    header[6] = (byte)((fileSize >> 16) & 0xFF);
    header[7] = (byte)((fileSize >> 24) & 0xFF);
    
    header[8] = 'W';  // Format
    header[9] = 'A';
    header[10] = 'V';
    header[11] = 'E';
    
    // fmt Subchunk
    header[12] = 'f';  // Subchunk1ID
    header[13] = 'm';
    header[14] = 't';
    header[15] = ' ';
    header[16] = 0x10; // Subchunk1Size (16 for PCM)
    header[17] = 0x00;
    header[18] = 0x00;
    header[19] = 0x00;
    header[20] = 0x01; // AudioFormat (1 for PCM)
    header[21] = 0x00;
    header[22] = 0x01; // NumChannels (1 for mono)
    header[23] = 0x00;
    header[24] = (byte)(I2S_SAMPLE_RATE & 0xFF); // SampleRate (16000 Hz)
    header[25] = (byte)((I2S_SAMPLE_RATE >> 8) & 0xFF);
    header[26] = (byte)((I2S_SAMPLE_RATE >> 16) & 0xFF);
    header[27] = (byte)((I2S_SAMPLE_RATE >> 24) & 0xFF);
    uint32_t byteRate = I2S_SAMPLE_RATE * 1 * (I2S_SAMPLE_BITS / 8);
    header[28] = (byte)(byteRate & 0xFF); // ByteRate
    header[29] = (byte)((byteRate >> 8) & 0xFF);
    header[30] = (byte)((byteRate >> 16) & 0xFF);
    header[31] = (byte)((byteRate >> 24) & 0xFF);
    header[32] = (byte)(1 * (I2S_SAMPLE_BITS / 8)); // BlockAlign
    header[33] = 0x00;
    header[34] = I2S_SAMPLE_BITS; // BitsPerSample
    header[35] = 0x00;
    
    // data Subchunk
    header[36] = 'd';  // Subchunk2ID
    header[37] = 'a';
    header[38] = 't';
    header[39] = 'a';
    header[40] = (byte)(wavSize & 0xFF);        // Subchunk2Size (dataSize)
    header[41] = (byte)((wavSize >> 8) & 0xFF);
    header[42] = (byte)((wavSize >> 16) & 0xFF);
    header[43] = (byte)((wavSize >> 24) & 0xFF);
}
