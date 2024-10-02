# ESP32 Voice Recognition and Text Generation System

This project implements a voice recognition system using an ESP32. It detects the wake word "MARVIN", records audio, and transcribes it using a server. Additionally, the system can ask questions to the Gemini model for content generation via Google’s Generative Language API.

## Features

- **Wake Word Detection**: Detects the wake word "MARVIN".
- **Audio Recording**: Records 5 seconds of audio and stores it in `.wav` format.
- **Transcription**: Sends the audio file to a server for transcription.
- **Content Generation**: Asks questions to the Gemini model using Google’s API.

## Components

1. **ESpeech**: 
   - Handles audio recording and transcription.
   - Communicates with a server to upload audio and retrieve transcription.
   
2. **GeminiESP32**: 
   - Sends a question to the Google Gemini API for content generation.
   - Connects to WiFi, handles API requests, and processes responses.

## Hardware Requirements

- **ESP32** (with I2S support)
- **Microphone** (connected via I2S)
- **WiFi Access**
- **FFat (Flash File System)** for storing audio files

## Software Requirements

- **Arduino IDE 2.3.2** with ESP32 board 2.0.14 package installed.
- **Libraries**:
  - `Audio.h` v[3.0.12n]
  - `Marvin_WakeWord_inferencing.h` v[1.0.0]
  - `ArduinoJson.h` v[7.1.0]

## Setup Instructions

1. **Clone the repository**:
   ```
   git clone https://github.com/your-repository.git
   ```

2. **Install Required Libraries**:
   In the Arduino IDE, install the following libraries via the Library Manager:
   - `WiFi`
   - `HTTPClient`
   - `ArduinoJson`
   - `FFat`

3. **Configure ESP32**:
   Edit the `ESpeech.h` and `GeminiESP32.h` files with your WiFi credentials and Google API key.

   ```cpp
   #define SERVER_URL "http://your-server-ip:port/uploadAudio"
   const char* ssid = "your-ssid";
   const char* password = "your-password";
   const char* token = "your-google-api-token";
   ```

4. **Prepare the Server**:
   Set up a server that accepts audio files (`.wav`) and returns transcriptions. Example server technologies:
   - Flask with `speech_recognition` for handling speech-to-text.
   - Python WSGI for managing file uploads.

5. **Flash the ESP32**:
   Use Arduino IDE to compile and upload the code to your ESP32 board.

6. **Test the System**:
   - After uploading, reset the ESP32.
   - Say the wake word "MARVIN" to trigger the recording.
   - The recorded audio will be sent to the server for transcription.
   - You can ask questions to the Gemini model, and the response will be generated and displayed.

## Usage

1. **Wake Word Detection**: Say "MARVIN" to start recording.
2. **Transcription**: The audio file is sent to the server, and the transcription is retrieved.
3. **Questioning Gemini**:
   - Call `askQuestion()` from the `GeminiESP32` class.
   - Example:
     ```cpp
     String answer = gemini.askQuestion("What is AI?");
     Serial.println(answer);
     ```

## Classes and Functions

### ESpeech Class
- **`begin()`**: Initializes WiFi and file system.
- **`recordAudio()`**: Records a 5-second audio clip and saves it as a `.wav` file.
- **`getTranscription()`**: Sends the audio file to a server and returns the transcription.

### GeminiESP32 Class
- **`begin()`**: Connects to the WiFi network.
- **`askQuestion(String question)`**: Sends a question to the Gemini model and returns the generated response.

## Notes

- **FFat**: Ensure enough space is available on the ESP32 for recording audio.
- **API Key**: Replace the placeholder API key with your actual Google Gemini API key.

## Future Improvements

- Extend the wake word recognition capabilities.
- Implement local inferencing to avoid the need for a remote server.
- Support for longer audio recordings by adjusting the file system configuration.

---
