#include "arduinoFFT.h"
#include "driver/i2s.h"

#define I2S_SAMPLE_RATE 16000
#define FFT_SIZE (256)
#define SPEECH_THRESHOLD 3000
#define NOISE_THRESHOLD 1000  //within 800-1200
#define SPEECH_FREQ_MIN 300
#define SPEECH_FREQ_MAX 3400
#define I2S_PORT I2S_NUM_0
#define I2S_WS 16
#define I2S_SD 7
#define I2S_SCK 15
#define GAIN_FACTOR 1.5  // Gain factor (adjust as needed)

void apply_gain(int16_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        data[i] = (int16_t)(data[i] * GAIN_FACTOR);
        // Clipping to avoid overflow
        if (data[i] > INT16_MAX) data[i] = INT16_MAX;
        if (data[i] < INT16_MIN) data[i] = INT16_MIN;
    }
}

class VAD {
public:
    void i2sInit();
    bool vadDetect();

private:
    int16_t i2sBuffer[FFT_SIZE];  // Buffer for I2S audio samples
    double vReal[FFT_SIZE];       // Real part of the FFT input
    double vImag[FFT_SIZE];       // Imaginary part (set to zero)
    ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SIZE, I2S_SAMPLE_RATE);

    float calculateEnergy(const double* data, int len);
    bool isSpeechDetected();
    float smoothValue(float newValue, float oldValue, float alpha);
    
    float previousEnergy = 0;
};

void VAD::i2sInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(16),
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

bool VAD::vadDetect() {
    // Read audio data from I2S
    size_t bytesRead;
    i2s_read(I2S_PORT, (char *)i2sBuffer, FFT_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    apply_gain(i2sBuffer, FFT_SIZE / sizeof(int16_t));

    // Convert I2S buffer to double array for FFT processing
    for (int i = 0; i < FFT_SIZE; i++) {
        vReal[i] = (double)i2sBuffer[i];  // Real part
        vImag[i] = 0;                     // Imaginary part set to zero
    }

    // Perform FFT
    FFT.windowing(vReal, FFT_SIZE, FFTWindow::Hamming, FFTDirection::Forward, vReal, false);  // Apply Hamming window
    FFT.compute(vReal, vImag, FFT_SIZE, FFTDirection::Forward);                // Compute FFT
    FFT.complexToMagnitude(vReal, vImag, FFT_SIZE);                             // Convert to magnitude

    // Detect speech and calculate energy and noise
    bool speechDetected = isSpeechDetected();
    float energy = calculateEnergy(vReal, FFT_SIZE);
    float smoothedEnergy = smoothValue(energy, previousEnergy, 0.95);  // Smoothing with alpha = 0.9
    previousEnergy = smoothedEnergy;
    float noise = smoothedEnergy - energy;

    // Concatenate data into a single string
    String output = "Energy:" + String(energy) + " " +
                    "Senergy:" + String(smoothedEnergy) + " " +
                    "Noise:" + String(noise) + " " +
                    "Speech:" + (speechDetected ? "6000000000" : "0");
    
    if (speechDetected) Serial.println(speechDetected);

    return speechDetected;
}

float VAD::calculateEnergy(const double* data, int len) {
    float sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i] * data[i]; // Sum of squared magnitudes
    }
    return sum;
}

bool VAD::isSpeechDetected() {
    float energy = 0;

    // Calculate the frequency range indexes
    int startIndex = (SPEECH_FREQ_MIN * FFT_SIZE) / I2S_SAMPLE_RATE;
    int endIndex = (SPEECH_FREQ_MAX * FFT_SIZE) / I2S_SAMPLE_RATE;

    // Make sure indices are within bounds
    startIndex = max(startIndex, 0);
    endIndex = min(endIndex, FFT_SIZE / 2);

    // Sum the magnitudes in the speech frequency range
    for (int i = startIndex; i < endIndex; i++) {
        energy += vReal[i] * vReal[i]; // Square of the magnitude
    }

    // Calculate the average energy
    float averageEnergy = sqrt(energy / (endIndex - startIndex));

    // Compare energy with threshold
    return (averageEnergy > SPEECH_THRESHOLD);
}

float VAD::smoothValue(float newValue, float oldValue, float alpha) {
    return alpha * oldValue + (1 - alpha) * newValue;
}

// Global variables
VAD myVad;
unsigned long maxTime = 10000;  // Maximum recording time in milliseconds
unsigned long bonusTime = 2000; // Bonus time in milliseconds
unsigned long startTime = 0;
bool recording = false;
bool bonusStarted = false;
bool listening = false;

// FreeRTOS Task handles
TaskHandle_t vadTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;

// VAD Task (Core 0)
void vadTask(void *pvParameters) {
    while (true) {
        if (listening) {
            unsigned long currentTime = millis();
            if (recording) {
                if (myVad.vadDetect()) {
                    bonusStarted = false;
                    startTime = millis(); // Reset start time when speech is detected
                    Serial.println("Speech Detected!");
                } else {
                    if (!bonusStarted) {
                        if (currentTime - startTime >= maxTime) {
                            // Stop recording if maximum time has expired
                            Serial.println("Stop Listening - Max Time Expired");
                            recording = false;
                            listening = false; // Stop listening
                            startTime = 0;     // Reset start time
                            vTaskDelete(vadTaskHandle);
                        } else if (currentTime - startTime >= bonusTime) {
                            // Stop recording if bonus time has expired
                            Serial.println("Stop Listening - Bonus Time Expired");
                            recording = false;
                            bonusStarted = false; // Reset bonus flag
                            startTime = 0;        // Reset start time
                            vTaskDelete(vadTaskHandle);
                        }
                    }
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Avoid task starvation
    }
}

// Serial Task (Core 1)
void serialTask(void *pvParameters) {
    while (true) {
        if (Serial.available() > 0) {
            String cmd = Serial.readString();
            if (cmd.compareTo("start") == 0) {
                  // Create VAD task on Core 0
            xTaskCreatePinnedToCore(
            vadTask,      // Task function
            "VAD Task",   // Task name
            4096,         // Stack size
            NULL,         // Task input parameter
            1,            // Task priority
            &vadTaskHandle,  // Task handle
            0             // Core ID
            );
                listening = true;
                Serial.println("Start Listening Command Received");
                startTime = millis();  // Start the timing for recording
                recording = true;     // Set recording flag
                bonusStarted = false; // Reset bonus flag
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Avoid task starvation
    }
}

void setup() {
    Serial.begin(115200);
    myVad.i2sInit();



    // Create Serial task on Core 1
    xTaskCreatePinnedToCore(
        serialTask,      // Task function
        "Serial Task",   // Task name
        4096,            // Stack size
        NULL,            // Task input parameter
        1,               // Task priority
        &serialTaskHandle,  // Task handle
        1                // Core ID
    );
}

void loop() {
    // Empty loop since tasks handle everything
}
