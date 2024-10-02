from flask import Flask, request, jsonify
import os
import speech_recognition as sr

app = Flask(__name__)
file_name = 'recording.wav'

@app.route('/uploadAudio', methods=['POST'])
def upload_audio():
    if request.method == 'POST':
        try:
            # Save the uploaded file
            with open(file_name, 'wb') as f:
                f.write(request.data)
            
            # Transcribe the audio file
            transcription = speech_to_text(file_name)
            
            return jsonify({'transcription': transcription}), 200
        except Exception as e:
            return str(e), 500
    else:
        return 'Method Not Allowed', 405

def speech_to_text(file_name):
    # Initialize the recognizer
    recognizer = sr.Recognizer()
    
    # Open the audio file
    with sr.AudioFile(file_name) as source:
        # Listen for the data (load audio to memory)
        audio_data = recognizer.record(source)
        
        # Recognize (convert from speech to text)
        try:
            text = recognizer.recognize_google(audio_data)
            print(f'Transcription: {text}')
            return text
        except sr.UnknownValueError:
            return "Google Speech Recognition could not understand audio"
        except sr.RequestError as e:
            return f"Could not request results from Google Speech Recognition service; {e}"

if __name__ == '__main__':
    port = 8888
    app.run(host='0.0.0.0', port=port)
    print(f'Listening at {port}')
