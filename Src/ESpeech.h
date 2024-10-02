#ifndef ESPEECH_H
#define ESPEECH_H
#include <driver/i2s.h>
#include "FS.h"
#include "FFat.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> //Dependency.....
class ESpeech{
public:
    ESpeech(i2s_port_t i2s_port,int i2s_sck,int i2s_ws,int i2s_sd);
    void begin(const char* server_Url);
    void recordAudio();
    String getTranscription();
    String stt;
private:
    i2s_port_t I2S_PORT;
    int I2S_WS=-1,I2S_SD=-1,I2S_SCK=-1;
    const char* serverUrl;
    File file;
    const char* filename = "/recording.wav";
    const int headerSize = 44;
    void i2sInit();
    void i2s_adc();
    void varyGain(uint8_t * buf,uint32_t len, int16_t gain);
    void wavHeader(byte* header, int wavSize);
    void FFATInit();
};

#endif
