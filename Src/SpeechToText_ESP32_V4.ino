#define EIDSP_QUANTIZE_FILTERBANK 0
#include "microphone.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include <Marvin_WakeWord_inferencing.h> //Dependency .....
#include "ESpeech.h"
#include "GeminiESP32.h"
#include "CoreEngine.h"

/** Audio buffers, pointers and selectors */
typedef struct {
  int16_t *buffer;
  uint8_t buf_ready;
  uint32_t buf_count;
  uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false;       // Set this to true to see e.g. features generated from the raw signal
static bool record_status = false;  // Start recording only when triggered


static void initwakeword();
static void audio_inference_callback(uint32_t n_bytes);
static void capture_samples(void *arg);
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
static int i2s_init(uint32_t sampling_rate);
static int i2s_deinit(void);
static bool detected();


const char *ssid = "Rakib";                                // Your SSID
const char *password = "rakib@2024";                       // Your PASS
#define serverUrl "http://192.168.0.106:8888/uploadAudio"  // Change the IP Address according To Your Server's config
#define GEMINI_API_TOKEN "AIzaSyDPNTBZEBFmlZBIStC-ExslDAMQPudkuOE"
#define I2S_WS 16
#define I2S_SD 7
#define I2S_SCK 15



ESpeech STT(I2S_NUM_1,I2S_SCK,I2S_WS,I2S_SD);
GeminiESP32 assistant(GEMINI_API_TOKEN);

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
  Serial.println("\nConnected to WiFi");
  Serial.printf("Free Heap at first: %d\n", ESP.getFreeHeap());
  initwakeword();
  Serial.printf("Free Heap after i2s microphone wakeword took place: %d\n", ESP.getFreeHeap());
  STT.begin(serverUrl);
  Serial.printf("Free Heap after i2s microphone STT took place: %d\n", ESP.getFreeHeap());
}

void loop() {
  Serial.printf("Free Heap before detection: %d\n", ESP.getFreeHeap());
  if (detected()) {
    if (inference.buffer != NULL) {
    free(inference.buffer);  // Free the allocated memory previously allocated in rtos task
    inference.buffer = NULL; // Set the pointer to NULL to avoid dangling pointers
    record_status = false;
    }
    Serial.printf("Free Heap after detection: %d\n", ESP.getFreeHeap());
    STT.recordAudio();
    Serial.printf("Free Heap after stt record: %d\n", ESP.getFreeHeap());
    String intent=STT.getTranscription();
    Serial.println(intent);
    CoreEngine core;
    core.addCommasToCommand(intent);
    core.processCommand(intent);
    if(core.getCloudCmd().isEmpty()){
    String answer=assistant.askQuestion(intent);
    Serial.println("Answer from GEMINI:=>"+answer);
    }
    else Serial.println("Pass to the Cloud:=>"+core.getCloudCmd());
    //audioConnecttoSpeech(answer.c_str(), "en");
    Serial.printf("Free Heap after stt transcript: %d\n", ESP.getFreeHeap());
    ei_sleep(500);
    if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
    ei_printf("ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
     }
}
}







static void initwakeword() {
  ei_printf("WakeWord Inferencing Demo");
  ei_printf("Inferencing settings:\n");
  ei_printf("\tInterval: ");
  ei_printf_float((float)EI_CLASSIFIER_INTERVAL_MS);
  ei_printf(" ms.\n");
  ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
  ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
  ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));
  ei_printf("\nStarting continious inference in 2 seconds...\n");
  //ei_sleep(2000);
  if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
    ei_printf("ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
    return;
  }
}


static void audio_inference_callback(uint32_t n_bytes) {
  for (int i = 0; i < n_bytes >> 1; i++) {
    inference.buffer[inference.buf_count++] = sampleBuffer[i];
    if (inference.buf_count >= inference.n_samples) {
      inference.buf_count = 0;
      inference.buf_ready = 1;
    }
  }
}


static void capture_samples(void *arg) {
  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;
  while (record_status) {
    i2s_read((i2s_port_t)1, (void *)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);
    if (bytes_read <= 0) ei_printf("Error in I2S read : %d", bytes_read);
    else {
      if (bytes_read < i2s_bytes_to_read) ei_printf("Partial I2S read");
      // scale the data (otherwise the sound is too quiet)
      for (int x = 0; x < i2s_bytes_to_read / 2; x++) sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
      if (record_status) audio_inference_callback(i2s_bytes_to_read);
      else break;
    }
  }
  vTaskDelete(NULL);
}


static bool microphone_inference_start(uint32_t n_samples) {
  inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));
  if (inference.buffer == NULL) return false;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;
  if (i2s_init(EI_CLASSIFIER_FREQUENCY)) ei_printf("Failed to start I2S!");
  ei_sleep(100);
  record_status = true;
  xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void *)sample_buffer_size, 10, NULL);
  return true;
}


static bool microphone_inference_record(void) {
  bool ret = true;
  while (inference.buf_ready == 0) delay(10);
  inference.buf_ready = 0;
  return ret;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
  return 0;
}


static void microphone_inference_end(void) {
  i2s_deinit();
  ei_free(inference.buffer);
}

static int i2s_deinit(void) {
  i2s_zero_dma_buffer((i2s_port_t)1);
  i2s_driver_uninstall((i2s_port_t)1);  //stop & destroy i2s driver
  return 0;
}

static int i2s_init(uint32_t sampling_rate) {
  // Start listening for audio: MONO @ 8/16KHz
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate = sampling_rate,
    .bits_per_sample = (i2s_bits_per_sample_t)16,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count =8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = -1,
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S__SCK,
    .ws_io_num = I2S__WS,
    .data_out_num = -1,
    .data_in_num = I2S__SD
  };
  esp_err_t ret = 0;

  ret = i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
  if (ret != ESP_OK) {
    ei_printf("Error in i2s_driver_install");
  }

  ret = i2s_set_pin((i2s_port_t)1, &pin_config);
  if (ret != ESP_OK) {
    ei_printf("Error in i2s_set_pin");
  }

  ret = i2s_zero_dma_buffer((i2s_port_t)1);
  if (ret != ESP_OK) {
    ei_printf("Error in initializing dma buffer with 0");
  }

  return int(ret);
}


static bool detected() {
  bool m = microphone_inference_record();
  if (!m) {
    ei_printf("ERR: Failed to record wakeword audio...\n");
    return false;
  }
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
  if (r != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\n", r);
    return false;
  }

  // print the predictions
  ei_printf("Predictions ");
  ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
            result.timing.dsp, result.timing.classification, result.timing.anomaly);
  ei_printf(": \n");
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    ei_printf("    %s: ", result.classification[ix].label);
    ei_printf_float(result.classification[ix].value);
    ei_printf("\n");
  }
  if (result.classification[1].value > 0.9) {
    record_status=false;
    i2s_zero_dma_buffer((i2s_port_t)1);
    delay(50);
    return true;
  }
  return false;
}