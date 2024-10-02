#include "VADCoreESP32.h"

void VADCoreESP32::setCore(int coreId) {
    this->coreId = coreId;
}

void VADCoreESP32::setPriority(UBaseType_t priority) {
    this->priority = priority;
}
bool VADCoreESP32::getState(){return recording;}
void VADCoreESP32::apply_gain(int16_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        data[i] = (int16_t)(data[i] * GAIN_FACTOR);
        // Clipping to avoid overflow
        if (data[i] > INT16_MAX) data[i] = INT16_MAX;
        if (data[i] < INT16_MIN) data[i] = INT16_MIN;
    }
}

void VADCoreESP32::i2sInit(i2s_port_t i2sPort, int i2sBckPin, int i2sWsPin, int i2sDataPin) {
    i2s_Port=i2sPort;
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = VAD_SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(16),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 1,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_driver_install(i2s_Port, &i2s_config, 0, NULL);

    const i2s_pin_config_t pin_config = {
        .bck_io_num = i2sBckPin,
        .ws_io_num = i2sWsPin,
        .data_out_num = -1,
        .data_in_num = i2sDataPin
    };

    i2s_set_pin(i2s_Port, &pin_config);
}

bool VADCoreESP32::vadDetect() {
    // Read audio data from I2S
    size_t bytesRead;
    i2s_read(i2s_Port, (char *)i2sBuffer, FFT_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);
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

    return speechDetected;
}

float VADCoreESP32::calculateEnergy(const double* data, int len) {
    float sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i] * data[i]; // Sum of squared magnitudes
    }
    return sum;
}

bool VADCoreESP32::isSpeechDetected() {
    float energy = 0;

    // Calculate the frequency range indexes
    int startIndex = (SPEECH_FREQ_MIN * FFT_SIZE) / VAD_SAMPLE_RATE;
    int endIndex = (SPEECH_FREQ_MAX * FFT_SIZE) / VAD_SAMPLE_RATE;

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

float VADCoreESP32::smoothValue(float newValue, float oldValue, float alpha) {
    return alpha * oldValue + (1 - alpha) * newValue;
}
void VADCoreESP32::vadTask() {
    if (listening) {
        unsigned long currentTime = millis();
        if (recording) {
            if (vadDetect()) {
                bonusStarted = false;
                startTime = millis(); // Reset start time when speech is detected
                Serial.println("Speech Detected! Core:"+String(xPortGetCoreID()));
            } else {
                if (!bonusStarted) {
                    if (currentTime - startTime >= maxTime) {
                        // Stop recording if maximum time has expired
                        Serial.println("Stop Listening - Max Time Expired");
                        recording = false;
                        listening = false; // Stop listening
                        startTime = 0;     // Reset start time
                    } else if (currentTime - startTime >= bonusTime) {
                        // Stop recording if bonus time has expired
                        Serial.println("Stop Listening - Bonus Time Expired");
                        recording = false;
                        bonusStarted = false; // Reset bonus flag
                        startTime = 0;        // Reset start time
                    }
                }
            }
        }
    }
}

void VADCoreESP32::start() {
    xTaskCreatePinnedToCore(
        vadTaskWrapper,      // Task function
        "VADCoreESP32 Task",          // Task name
        4096,                // Stack size
        this,                // Task input parameter
        priority,                   // Task priority VERY hIGH
        &vadTaskHandle,      // Task handle
        coreId                    // Core ID (Core 0)
    );
    listening = true;
    startTime = millis();  // Start the timing for recording
    recording = true;     // Set recording flag
    bonusStarted = false; // Reset bonus flag
}

void VADCoreESP32::vadTaskWrapper(void *pvParameters) {
    VADCoreESP32 *instance = (VADCoreESP32 *)pvParameters;
    while (true) {
        if (instance->recording) {
            instance->vadTask();
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        else{
        Serial.println("Deleting VadTask Heap:"+String(ESP.getFreeHeap()));
        vTaskDelete(instance->vadTaskHandle);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Avoid task starvation
    }
}
