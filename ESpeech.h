#ifndef ESPEECH_H
#define ESPEECH_H
#include <driver/i2s.h>
#include "FS.h"
#include "FFat.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
class ESpeech{
public:
    ESpeech(const char* ssid, const char* password, const char* serverUrl);
    void begin();
    void recordAudio();
    String getTranscription();
    String stt;

private:
    const char* ssid;
    const char* password;
    const char* serverUrl;
    File file;
    const char* filename = "/recording.wav";
    const int headerSize = 44;

    void i2sInit();
    void i2sDeInit();
    void i2s_adc();
    void i2s_adc_data_scale(uint8_t* d_buff, uint8_t* s_buff, uint32_t len);
    void wavHeader(byte* header, int wavSize);
    void FFATInit();
    void wifiConnect();
    void listFFAT();
};

#endif
