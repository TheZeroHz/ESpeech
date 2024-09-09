#include <WiFi.h>
#include "Audio.h"
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <FFat.h>
#include <SPI.h>

#define I2S_DOUT 6
#define I2S_BCLK 5
#define I2S_LRC 4

#define app_core 1
#define utility_core 0
#define audio_stack 5000

TaskHandle_t audio_handle = NULL;
Audio audio;
 
//****************************************************************************************
//                                   A U D I O _ T A S K  3.0.8e                               *
//****************************************************************************************
struct audioMessage {
  uint8_t cmd;
  const char* txt;
  uint32_t value;
  uint32_t ret;
  const char* lang;
} audioTxMessage, audioRxMessage;

enum : uint8_t { SET_VOLUME,
                 GET_VOLUME,
                 CONNECTTOHOST,
                 CONNECTTOSD,
                 CONNECTTOSPEECH,
                 GET_AUDIOSTATUS };

QueueHandle_t audioSetQueue = NULL;
QueueHandle_t audioGetQueue = NULL;

void CreateQueues() {
  audioSetQueue = xQueueCreate(10, sizeof(struct audioMessage));
  audioGetQueue = xQueueCreate(10, sizeof(struct audioMessage));
}

void audioTask(void* parameter) {
  CreateQueues();
  if (!audioSetQueue || !audioGetQueue) {
    log_e("queues are not initialized");
    while (true) { ; }  // endless loop
  }

  struct audioMessage audioRxTaskMessage;
  struct audioMessage audioTxTaskMessage;

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

  while (true) {
    if (xQueueReceive(audioSetQueue, &audioRxTaskMessage, 1) == pdPASS) {
      if (audioRxTaskMessage.cmd == SET_VOLUME) {
        audioTxTaskMessage.cmd = SET_VOLUME;
        audio.setVolume(audioRxTaskMessage.value);
        audioTxTaskMessage.ret = 1;
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else if (audioRxTaskMessage.cmd == CONNECTTOHOST) {
        audioTxTaskMessage.cmd = CONNECTTOHOST;
        audioTxTaskMessage.ret = audio.connecttohost(audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else if (audioRxTaskMessage.cmd == CONNECTTOSD) {
        audioTxTaskMessage.cmd = CONNECTTOSD;
        audioTxTaskMessage.ret = audio.connecttoFS(SD,audioRxTaskMessage.txt);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else if (audioRxTaskMessage.cmd == GET_VOLUME) {
        audioTxTaskMessage.cmd = GET_VOLUME;
        audioTxTaskMessage.ret = audio.getVolume();
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else if (audioRxTaskMessage.cmd == GET_AUDIOSTATUS) {
        audioTxTaskMessage.cmd = GET_AUDIOSTATUS;
        audioTxTaskMessage.ret = audio.isRunning();
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else if (audioRxTaskMessage.cmd == CONNECTTOSPEECH) {
        audioTxTaskMessage.cmd = CONNECTTOSPEECH;
        audioTxTaskMessage.ret = audio.connecttospeech(audioRxTaskMessage.txt, audioRxTaskMessage.lang);
        xQueueSend(audioGetQueue, &audioTxTaskMessage, portMAX_DELAY);
      } else {
        log_i("error");
      }
    }
    audio.loop();
    if (!audio.isRunning()) {
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
  }
}

void audioInit() {
  xTaskCreatePinnedToCore(
    audioTask,             /* Function to implement the task */
    "audioplay",           /* Name of the task */
    5000,                  /* Stack size in words */
    NULL,                  /* Task input parameter */
    2 | portPRIVILEGE_BIT, /* Priority of the task */
    NULL,                  /* Task handle. */
    1                      /* Core where the task should run */
  );
}


struct audioMessage transmitReceive(audioMessage msg) {
  xQueueSend(audioSetQueue, &msg, portMAX_DELAY);
  if (xQueueReceive(audioGetQueue, &audioRxMessage, portMAX_DELAY) == pdPASS) {
    if (msg.cmd != audioRxMessage.cmd) {
      log_e("wrong reply from message queue");
    }
  }
  return audioRxMessage;
}

void audioSetVolume(uint8_t vol) {
  audioTxMessage.cmd = SET_VOLUME;
  audioTxMessage.value = vol;
  audioMessage RX = transmitReceive(audioTxMessage);
}

uint8_t audioGetVolume() {
  audioTxMessage.cmd = GET_VOLUME;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttohost(const char* host) {
  audioTxMessage.cmd = CONNECTTOHOST;
  audioTxMessage.txt = host;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audioConnecttoSD(const char* filename) {
  audioTxMessage.cmd = CONNECTTOSD;
  audioTxMessage.txt = filename;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}
void audio_info(const char* info) {
  Serial.print("info        ");
  Serial.println(info);
}
bool audioConnecttoSpeech(const char* speech, const char* language) {
  audioTxMessage.cmd = CONNECTTOSPEECH;
  audioTxMessage.txt = speech;
  audioTxMessage.lang = language;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

bool audiostatus() {
  audioTxMessage.cmd = GET_AUDIOSTATUS;
  audioMessage RX = transmitReceive(audioTxMessage);
  return RX.ret;
}

//****************************************************************************************
//                                  END OF AUDIO PORTION                                 *
//****************************************************************************************