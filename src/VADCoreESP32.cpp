#include "VADCoreESP32.h"

void VADCoreESP32::setCore(int coreId) {
    this->coreId = coreId;
}

void VADCoreESP32::setMaxTime(unsigned long time){
    maxTime = time;
}

void VADCoreESP32::setBonusTime(unsigned long time){
    bonusTime = time;
}

void VADCoreESP32::setPriority(UBaseType_t priority) {
    this->priority = priority;
}

void VADCoreESP32::setI2SPort(i2s_port_t port) {
    i2s_Port = port;
    i2sInitialized = false; // We're using existing I2S, not initializing our own
}

bool VADCoreESP32::getState(){
    return recording;
}

void VADCoreESP32::apply_gain(int16_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        data[i] = (int16_t)(data[i] * GAIN_FACTOR);
        // Clipping to avoid overflow
        if (data[i] > INT16_MAX) data[i] = INT16_MAX;
        if (data[i] < INT16_MIN) data[i] = INT16_MIN;
    }
}

void VADCoreESP32::i2sInit(i2s_port_t i2sPort, int i2sBckPin, int i2sWsPin, int i2sDataPin) {
    i2s_Port = i2sPort;
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = VAD_SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(16),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8, // Smaller buffer for VAD
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    esp_err_t result = i2s_driver_install(i2s_Port, &i2s_config, 0, NULL);
    if (result == ESP_OK) {
        const i2s_pin_config_t pin_config = {
            .bck_io_num = i2sBckPin,
            .ws_io_num = i2sWsPin,
            .data_out_num = -1,
            .data_in_num = i2sDataPin
        };
        i2s_set_pin(i2s_Port, &pin_config);
        i2sInitialized = true;
    }
}

bool VADCoreESP32::vadDetect() {
    // Read audio data from I2S
    size_t bytesRead;
    esp_err_t result = i2s_read(i2s_Port, (char *)i2sBuffer, FFT_SIZE * sizeof(int16_t), &bytesRead, 50);
    
    if (result != ESP_OK || bytesRead == 0) {
        return false; // No data available or error
    }
    
    apply_gain(i2sBuffer, bytesRead / sizeof(int16_t));

    // Convert I2S buffer to double array for FFT processing
    for (int i = 0; i < FFT_SIZE; i++) {
        vReal[i] = (double)i2sBuffer[i];  // Real part
        vImag[i] = 0;                     // Imaginary part set to zero
    }

    // Perform FFT
    FFT.windowing(vReal, FFT_SIZE, FFTWindow::Hamming, FFTDirection::Forward, vReal, false);
    FFT.compute(vReal, vImag, FFT_SIZE, FFTDirection::Forward);
    FFT.complexToMagnitude(vReal, vImag, FFT_SIZE);

    // Detect speech and calculate energy
    bool speechDetected = isSpeechDetected();
    float energy = calculateEnergy(vReal, FFT_SIZE);
    float smoothedEnergy = smoothValue(energy, previousEnergy, 0.95);
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
            } else {
                if (!bonusStarted) {
                    if (currentTime - startTime >= maxTime) {
                        // Stop recording if maximum time has expired
                        recording = false;
                        listening = false;
                        startTime = 0;
                    } else if (currentTime - startTime >= bonusTime) {
                        // Stop recording if bonus time has expired
                        recording = false;
                        bonusStarted = false;
                        startTime = 0;
                    }
                }
            }
        }
    }
}

void VADCoreESP32::start() {
    xTaskCreatePinnedToCore(
        vadTaskWrapper,      // Task function
        "VADCoreESP32 Task", // Task name
        4096,                // Stack size
        this,                // Task input parameter
        priority,            // Task priority
        &vadTaskHandle,      // Task handle
        coreId               // Core ID
    );
    listening = true;
    startTime = millis();
    recording = true;
    bonusStarted = false;
}

void VADCoreESP32::vadTaskWrapper(void *pvParameters) {
    VADCoreESP32 *instance = (VADCoreESP32 *)pvParameters;
    while (true) {
        if (instance->recording) {
            instance->vadTask();
            vTaskDelay(50 / portTICK_PERIOD_MS);
        } else {
            // Clean up and delete task
            if (instance->vadTaskHandle) {
                vTaskDelete(instance->vadTaskHandle);
                instance->vadTaskHandle = NULL;
            }
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
