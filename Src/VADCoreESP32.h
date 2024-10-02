#ifndef VADCOREESP32_H
#define VADCOREESP32_H
#include<Arduino.h>
#include "arduinoFFT.h"
#include "driver/i2s.h"
#define VAD_VOICE true
#define VAD_SILENCE false
#define VAD_SAMPLE_RATE 16000
#define FFT_SIZE (256)
#define SPEECH_THRESHOLD 3000
#define NOISE_THRESHOLD 1000  //within 800-1200
#define SPEECH_FREQ_MIN 300
#define SPEECH_FREQ_MAX 3400
#define GAIN_FACTOR 1.5  // Gain factor (adjust as needed)

class VADCoreESP32 {
public:
    VADCoreESP32() : coreId(xPortGetCoreID()), priority(1), vadTaskHandle(NULL) {}
    void i2sInit(i2s_port_t i2sPort, int i2sBckPin, int i2sWsPin, int i2sDataPin);
    void start();
    bool getState();
    void setCore(int coreId);  // Set core for task
    void setPriority(UBaseType_t priority); // Set priority for task
private:
    i2s_port_t i2s_Port;
    int coreId; // Core ID to pin the task
    UBaseType_t priority; // Priority of the task
    float previousEnergy = 0;
    int16_t i2sBuffer[FFT_SIZE];  // Buffer for I2S audio samples
    double vReal[FFT_SIZE];       // Real part of the FFT input
    double vImag[FFT_SIZE];       // Imaginary part (set to zero)
    ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SIZE, VAD_SAMPLE_RATE);

    void apply_gain(int16_t *data, size_t length);
    float calculateEnergy(const double* data, int len);
    bool isSpeechDetected();
    float smoothValue(float newValue, float oldValue, float alpha);
    bool vadDetect();
    unsigned long maxTime = 10000;  // Maximum recording time in milliseconds
    unsigned long bonusTime = 3000; // Bonus time in milliseconds
    unsigned long startTime = 0;
    bool recording = false;
    bool bonusStarted = false;
    bool listening = false;
    TaskHandle_t vadTaskHandle = NULL;
    static void vadTaskWrapper(void *pvParameters);  // Wrapper for the FreeRTOS task
    void vadTask();
};

#endif
