#define USE_WAKEWORD
#define ESPEECH_USE_SPIFFS 0
#define ESPEECH_USE_FFAT 1

#define VAD_CORE 0
#define VAD_PRIORITY 1
#define VAD_BONUS_TIME 3000
#define VAD_MAX_TIME 10000

const int I2S_WS  = 16;
const int I2S_SD  = 18;
const int I2S_SCK = 17;


#if ESPEECH_USE_SPIFFS
  #include <SPIFFS.h>
  #define ESPEECH_FS SPIFFS
#elif ESPEECH_USE_FFAT
  #include <FFat.h>
  #define ESPEECH_FS FFat
#else
  #error "No FS defined for ESpeech. Please define ESPEECH_USE_SPIFFS or ESPEECH_USE_FFAT"
#endif
