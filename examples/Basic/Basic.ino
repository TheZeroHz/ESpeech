#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include <ESpeech.h>
ESpeech STT(I2S_NUM_1,I2S_SCK,I2S_WS,I2S_SD);
const char *ssid = "BRIC_301/B";                                // Your SSID
const char *password = "research@301";                       // Your PASS
#define serverUrl "https://espeechserver-iukg.onrender.com/uploadAudio"  // Change the IP Address according To Your Server's config

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
  Serial.println("\nConnected to WiFi");
  STT.serverURL(serverUrl);
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readString();
    if (cmd.indexOf("start") == 0) { //////apply any logic to begin stt. For ex use push_button logic or Upcoming Wakeword like 'hey marvin' !!!! /////////////////
      STT.recordAudio(); // This records the voice with vad
      String intent = STT.getTranscription(); // this returns the speech to text as string
      Serial.println(intent); // just showing the intent
    }
  }
}
