#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// ---- CRITICAL INCLUDES FOR TYPES AND CORE LIBRARIES ----
#include <ILI9341_t3.h>
#include <XPT2046_Touchscreen.h>
#include <AT42QT2120.h>
#include <Bounce.h>
#include <CapacitiveSensor.h>

// ---- CUSTOM AUDIO PLAYBACK CLASS (for scrubbing) ----
#include "play_scrub.h" // This will now bring in mapFloat

// GUItool: begin automatically generated code
AudioPlaySdWav           loopPlayer;      // For normal looping playback
AudioPlayScrub           audioScrubber;   // For scrubbing functionality (mono output)
AudioMixer4              mixer1;
AudioOutputI2S           i2s1;

AudioConnection          patchCordLoop1(loopPlayer, 0, mixer1, 0);      // Loop Player L -> Mixer Ch 0
AudioConnection          patchCordLoop2(loopPlayer, 1, mixer1, 1);      // Loop Player R -> Mixer Ch 1

AudioConnection          patchCordScrub1(audioScrubber, 0, mixer1, 2);  // Audio Scrubber (Mono) -> Mixer Ch 2

AudioConnection          patchCordMixOut1(mixer1, 0, i2s1, 0);           // Mixer Output 0 -> I2S Left
AudioConnection          patchCordMixOut2(mixer1, 0, i2s1, 1);           // Mixer Output 0 -> I2S Right (Mixer output is mono)
AudioControlSGTL5000     sgtl5000_1;
// GUItool: end automatically generated code


// ---- PROJECT SPECIFIC HEADERS ----
#include "frequencies.h" 
#include "display.h"     
#include "sdFiles.h"     

// ---- OBJECT DEFINITIONS (Screen and Global States) ----
ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC);
XPT2046_Touchscreen ts(CS_PIN);
bool granularRunning = false;
bool isScrubbing = false; 

enum PlaybackMode {
  PLAYBACK_MODE_LOOP,
  PLAYBACK_MODE_SCRUB_AUDIO 
};
PlaybackMode currentPlaybackMode = PLAYBACK_MODE_LOOP; 

Bounce Ckey =   Bounce(cKey, 15); 
Bounce CSkey =  Bounce(csKey, 15);
Bounce Dkey =   Bounce(dKey, 15);
Bounce DSkey =  Bounce(dsKey, 15);
Bounce Ekey =   Bounce(eKey, 15);
Bounce Fkey =   Bounce(fKey, 15);
Bounce FSkey =  Bounce(fsKey, 15);
Bounce Gkey =   Bounce(gKey, 15);
Bounce GSkey =  Bounce(gsKey, 15);
Bounce Akey =   Bounce(aKey, 15);
Bounce ASkey =  Bounce(asKey, 15);
Bounce Bkey =   Bounce(bKey, 15);


// ---- GLOBAL VARIABLES & CONSTANTS ----
#define change1 28 
#define change2 29 
#define change3 30 
#define change4 31 
#define change5 32 

#define muxS0Pin 0
#define muxS1Pin 1
#define muxS2Pin 2
#define muxS3Pin 3

#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11 
#define SDCARD_SCK_PIN   13 

#define STATUS_KEY1     0   
#define STATUS_KEY2     4   
#define muxSignalPin    22  
#define capSenseSendPin 4   

AT42QT2120 touch_sensor; 
CapacitiveSensor capSense = CapacitiveSensor( capSenseSendPin, muxSignalPin );

#define sampleRate      10  
#define n_inputs        12  

#define SCRUB_BUFFER_SIZE_SAMPLES (AUDIO_BLOCK_SAMPLES * 128) 
int16_t scrubBuffer[SCRUB_BUFFER_SIZE_SAMPLES]; 


unsigned long lastShiftPress = 0;
unsigned long shiftPressTime = 0;
const unsigned long shiftTimer = 3500;
bool shiftPressed = false;
bool P_shiftPressed = false;
bool shiftActionTakenThisPress = false;

char** fileList = nullptr;
int fileNum = 0;
int currentFileIndex = -1;
int loopingFileIndex = -1;
uint32_t currentFileSizeInBytes = 0; // Local cache in .ino for the scrubber's file size

int sliderVal1, P_sliderVal1, sliderVal2, P_sliderVal2, sliderVal3, P_sliderVal3, sliderVal4, P_sliderVal4, wheelVal, P_wheelVal;
bool sliChange1_active, sliChange2_active, sliChange3_active, sliChange4_active, sliChange5_active;
int touch[n_inputs] = {};

const int RESET_DELAY = 200;
const int CALIBRATION_DELAY = 150;
elapsedMillis calibration_timeout;

const int maxPeaks = 1000; 
int16_t peaks[maxPeaks];
int numPeaks = 0;

int prevPlaybackLineX = -1;
int16_t currentMaxPeakForWaveform = 0;
elapsedMillis playbackLineTimer;
const int playbackLineUpdateInterval = 30; 

// ---- FORWARD DECLARATIONS ----
void reloadFileList();
void handleMenuSelection();
// mapFloat is now in play_scrub.h

// ---- UTILITY FUNCTIONS ----
uint32_t estimateTotalMsFromByteSize(uint32_t totalBytes) {
    if (totalBytes == 0) return 1; 
    uint32_t assumedBytesPerSecond = 44100 * 2 * 2; 
    if (assumedBytesPerSecond == 0) return 1;
    float totalDurationSeconds = (float)totalBytes / assumedBytesPerSecond;
    uint32_t durationMs = (uint32_t)(totalDurationSeconds * 1000);
    return (durationMs > 0) ? durationMs : 1; 
}

bool isAudioScrubberReadyForTarget() {
    if (!granularRunning || currentPlaybackMode != PLAYBACK_MODE_SCRUB_AUDIO) {
        return false;
    }
    // Check if scrubber has a file and is in an active mode (0 or 1)
    return audioScrubber.get_file_size_bytes() > 0 && (audioScrubber.getMode() == 0 || audioScrubber.getMode() == 1);
}


////////////////////////READ FROM MUX (Analog MUX for capsense) //////////////////////////////////
int readMux(int channel) {
    int controlPin[] = {muxS0Pin, muxS1Pin, muxS2Pin, muxS3Pin};
    for (int k = 0; k < 4; k++) {
      digitalWrite(controlPin[k], (channel >> k) & 0x01); 
    }
    return capSense.capacitiveSensor(sampleRate); 
}

//////////////////////// Select I2C BUS ON TCA9548 //////////////////////////////////
void TCA9548A(uint8_t bus){
    Wire.beginTransmission(0x70);  
    Wire.write(1 << bus);          
    Wire.endTransmission();
}

///////////////////////////////////SETUP///////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  unsigned long setupStartTime = millis();
  Serial.println("SYNTHIA Booting Up...");
  Wire.begin(); 

  pinMode(change1, INPUT_PULLUP); 
  pinMode(change2, INPUT_PULLUP); 
  pinMode(change3, INPUT_PULLUP); 
  pinMode(change4, INPUT_PULLUP); 
  pinMode(change5, INPUT_PULLUP); 

  pinMode(cKey,   INPUT_PULLUP); pinMode(csKey,  INPUT_PULLUP); pinMode(dKey,   INPUT_PULLUP);
  pinMode(dsKey,  INPUT_PULLUP); pinMode(eKey,   INPUT_PULLUP); pinMode(fKey,   INPUT_PULLUP);
  pinMode(fsKey,  INPUT_PULLUP); pinMode(gKey,   INPUT_PULLUP); pinMode(gsKey,  INPUT_PULLUP);
  pinMode(aKey,   INPUT_PULLUP); pinMode(asKey,  INPUT_PULLUP); pinMode(bKey,   INPUT_PULLUP);

  Serial.println("Initializing TFT display...");
  tft.begin();
  tft.setRotation(3); 
  tft.fillScreen(ILI9341_BLACK);

  Serial.println("Initializing Touchscreen...");
  ts.begin();
  ts.setRotation(3); 

  tft.setCursor(20,100);
  tft.setFont(LiberationMono_48); 
  tft.setTextColor(ILI9341_WHITE);
  tft.print("SYNTHIA");
  delay(1000); 

  Serial.println("Initializing Audio System...");
  AudioMemory(896); 
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.5); 

  mixer1.gain(0, 0.75); 
  mixer1.gain(1, 0.75); 
  mixer1.gain(2, 0.0);  
  mixer1.gain(3, 0.0);  

  Serial.println("Setting up audioScrubber...");
  Serial.print("SCRUB_BUFFER_DECLARED_SIZE_SAMPLES: "); Serial.println(SCRUB_BUFFER_SIZE_SAMPLES); 
  audioScrubber.setScrubBuffer(scrubBuffer); 
  audioScrubber.setRate(1.0f); 
  audioScrubber.setSpeed(1.0f); 
  audioScrubber.setMode(3); 
  Serial.println("audioScrubber setup complete.");


  Serial.println("Initializing SD card...");
  pinMode(SDCARD_CS_PIN, OUTPUT); // Ensure CS pin is output before SD.begin
  digitalWrite(SDCARD_CS_PIN, HIGH); // Deselect card
  if (!(SD.begin(SDCARD_CS_PIN))) {
    Serial.println("Unable to access the SD card!");
    tft.fillScreen(ILI9341_RED); tft.setCursor(10,10); tft.setFont(LiberationMono_16);
    tft.setTextColor(ILI9341_WHITE); tft.print("SD Card Error!");
    while (1) delay(100); 
  }
  Serial.println("SD card initialized.");

  Serial.println("Calibrating QTouch sensors...");
  for(int i = 0; i < 5; i++){ 
    TCA9548A(i); 
    Serial.print("  Initializing QTouch on MUX channel "); Serial.println(i);
    touch_sensor.begin(); 
    
    if (i == 4) { 
        touch_sensor.enableWheel();
        Serial.println("    QTouch enabled as WHEEL.");
    } else { 
        touch_sensor.enableSlider();
        Serial.println("    QTouch enabled as SLIDER.");
    }

    Serial.print("    Resetting QTouch on MUX channel "); Serial.println(i);
    touch_sensor.reset();
    delay(RESET_DELAY / 5); 

    Serial.print("    Triggering calibration for QTouch on MUX channel "); Serial.println(i);
    touch_sensor.triggerCalibration();
    calibration_timeout = 0; 
    while (touch_sensor.calibrating() && calibration_timeout < 3000){ 
      Serial.print(".");
      delay(CALIBRATION_DELAY / 2);
    }
    if (touch_sensor.calibrating()) { 
        Serial.println("\n    Calibration FAILED for QTouch on MUX channel "); Serial.println(i);
    } else {
        Serial.println("\n    Finished calibrating QTouch on MUX channel "); Serial.println(i);
    }
    delay(100); 
  }
  Serial.println("QTouch calibration sequence complete.");

  pinMode(muxS0Pin, OUTPUT); digitalWrite(muxS0Pin, LOW);
  pinMode(muxS1Pin, OUTPUT); digitalWrite(muxS1Pin, LOW);
  pinMode(muxS2Pin, OUTPUT); digitalWrite(muxS2Pin, LOW);
  pinMode(muxS3Pin, OUTPUT); digitalWrite(muxS3Pin, LOW);

  Serial.println("Loading file list...");
  reloadFileList(); 
  Serial.print("Setup complete. Duration: "); Serial.print(millis() - setupStartTime); Serial.println(" ms");
} 

//////////////////////////////////////////MAIN LOOP//////////////////////////////////////////////////////
void loop(){
  sliChange4_active = !digitalRead(change4); 
  sliChange3_active = !digitalRead(change3); 
  sliChange2_active = !digitalRead(change2); 
  sliChange1_active = !digitalRead(change1); 
  sliChange5_active = !digitalRead(change5); 

  TCA9548A(STATUS_KEY2); 
  AT42QT2120::Status status2_qtouch = touch_sensor.getStatus();
  shiftPressed = (status2_qtouch.keys == 8); 

  uint8_t mode_keys = 0; 
  if (granularRunning) { 
    TCA9548A(STATUS_KEY1);
    AT42QT2120::Status status1_qtouch = touch_sensor.getStatus();
    mode_keys = status1_qtouch.keys; 
  }

  TCA9548A(0); touch_sensor.enableSlider(); sliderVal1 = touch_sensor.getSliderValue(); 
  TCA9548A(1); touch_sensor.enableSlider(); sliderVal2 = touch_sensor.getSliderValue(); 
  TCA9548A(2); touch_sensor.enableSlider(); sliderVal3 = touch_sensor.getSliderValue(); 
  TCA9548A(3); touch_sensor.enableSlider(); sliderVal4 = touch_sensor.getSliderValue(); 
  TCA9548A(4); touch_sensor.enableWheel();  wheelVal = map(touch_sensor.getSliderValue(), 0, 255, 255, 0); 

  for (byte channel = 0; channel < n_inputs; ++channel) {
    touch[channel] = readMux(channel);
  }

  Ckey.update(); CSkey.update(); Dkey.update(); DSkey.update(); Ekey.update();
  Fkey.update(); FSkey.update(); Gkey.update(); GSkey.update(); Akey.update();
  ASkey.update(); Bkey.update();

  if (shiftPressed) { 
    if (!P_shiftPressed) { 
      lastShiftPress = millis(); 
      shiftActionTakenThisPress = false; 
      Serial.println("Shift: Pressed.");
    }
    shiftPressTime = millis() - lastShiftPress; 
    if (shiftPressTime >= shiftTimer && !shiftActionTakenThisPress) { 
      Serial.println("Shift: Long press -> Reloading file list.");
      reloadFileList(); 
      shiftActionTakenThisPress = true; 
    }
  } else { 
    if (P_shiftPressed) { 
      Serial.println("Shift: Released.");
      if (!shiftActionTakenThisPress && currentFileIndex != -1 && fileList != nullptr) { 
        Serial.print("Shift: Short press on file index: "); Serial.println(currentFileIndex);
        if (granularRunning && loopingFileIndex == currentFileIndex) { 
          Serial.println("Exiting granular mode.");
          granularRunning = false; 
          loopPlayer.stop(); 
          audioScrubber.stop(); 
          loopingFileIndex = -1; prevPlaybackLineX = -1; currentMaxPeakForWaveform = 0;
          currentFileSizeInBytes = 0; 
          if(isScrubbing) { isScrubbing = false; } 
          mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
          mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);  
          currentPlaybackMode = PLAYBACK_MODE_LOOP; 
          tft.fillScreen(ILI9341_BLACK); printFileList(fileList, fileNum); 
          if (currentFileIndex >=0 && currentFileIndex < fileNum) { 
             tft.drawRect(2, (currentFileIndex * 20)+1, 150, 19, SYNTHIA_LIGHTBLUE);
          }
        } else { 
          Serial.println("Entering granular mode.");
          granularRunning = true; loopingFileIndex = currentFileIndex; 
          loopPlayer.stop(); 
          audioScrubber.stop(); 
          currentPlaybackMode = PLAYBACK_MODE_LOOP; 
          isScrubbing = false; 
          mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
          mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);  

          Serial.print("Preparing waveform for: "); Serial.println(fileList[currentFileIndex]);
          numPeaks = readWavAndGetPeaks(fileList[currentFileIndex], peaks, maxPeaks); 
          
          granularScreen(peaks, numPeaks, &currentMaxPeakForWaveform); 
          prevPlaybackLineX = -1; 
          
          currentFileSizeInBytes = 0; 
          
          if (loopingFileIndex >= 0 && SD.exists(fileList[loopingFileIndex])) {
             Serial.print("Granular entry: Starting loopPlayer with "); Serial.println(fileList[loopingFileIndex]);
             loopPlayer.play(fileList[loopingFileIndex]); 
             Serial.println("Granular entry: loopPlayer.play() called.");
             Serial.flush();
          } else {
             Serial.println("Granular entry: File does not exist.");
          }
        }
      }
      shiftActionTakenThisPress = false; 
    }
  }
  P_shiftPressed = shiftPressed; 

  if (granularRunning && loopingFileIndex != -1) { 
    // --- Overall Mode Switching (Loop vs Scrub Audio) ---
    if (mode_keys == 8 && currentPlaybackMode != PLAYBACK_MODE_LOOP) { // Button 1: Switch to Loop Mode
        Serial.println("Mode: Switching to PLAYBACK_MODE_LOOP.");
        audioScrubber.stop(); 
        mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
        mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);  

        currentPlaybackMode = PLAYBACK_MODE_LOOP;
        isScrubbing = false; 
        AudioStartUsingSPI(); 
        if (!loopPlayer.isPlaying() && SD.exists(fileList[loopingFileIndex])) {
            Serial.print("LOOP mode: Starting loopPlayer with "); Serial.println(fileList[loopingFileIndex]);
            loopPlayer.play(fileList[loopingFileIndex]); 
        } else if (loopPlayer.isPlaying()) {
             Serial.println("LOOP mode: loopPlayer already playing, ensuring SPI context.");
        } else {
            Serial.println("LOOP mode: loopPlayer not playing and file may not exist or other issue.");
        }
        prevPlaybackLineX = -1; 
    } else if (mode_keys == 16 && currentPlaybackMode != PLAYBACK_MODE_SCRUB_AUDIO) { // Button 2: Switch to Scrub Audio Mode
        Serial.println("Mode: Switching to PLAYBACK_MODE_SCRUB_AUDIO.");
        if (loopPlayer.isPlaying()) { 
            loopPlayer.stop(); 
            Serial.println("Mode switch to SCRUB_AUDIO: loopPlayer stopped.");
        }
        audioScrubber.stop(); 
        Serial.println("Mode switch to SCRUB_AUDIO: audioScrubber stopped.");
        Serial.flush();
        
        mixer1.gain(0, 0.0); mixer1.gain(1, 0.0); 
        mixer1.gain(2, 0.75); mixer1.gain(3, 0.0);  

        currentPlaybackMode = PLAYBACK_MODE_SCRUB_AUDIO;
        
        if (SD.exists(fileList[loopingFileIndex])) { 
            Serial.println("SCRUB_AUDIO mode: Disabling audio interrupts, re-init SPI and SD...");
            AudioNoInterrupts(); 
            SPI.end();    
            delay(50);    
            SPI.begin();  
            delay(50);
            pinMode(SDCARD_CS_PIN, OUTPUT);      // Explicitly set CS pin mode
            digitalWrite(SDCARD_CS_PIN, HIGH);   // Deselect SD card
            delay(10);                           // Short delay
            if (!SD.begin(SDCARD_CS_PIN)) { 
                Serial.println("SCRUB_AUDIO mode: CRITICAL - SD.begin() FAILED during re-init!");
                currentFileSizeInBytes = 0;
            } else {
                Serial.println("SCRUB_AUDIO mode: SD card re-initialized successfully.");
                delay(20);
                Serial.flush();
                Serial.print("SCRUB_AUDIO mode: Attempting to set file for audioScrubber: "); Serial.println(fileList[loopingFileIndex]);
                if (!audioScrubber.setFile(fileList[loopingFileIndex])) {
                    Serial.println("SCRUB_AUDIO mode: audioScrubber.setFile FAILED.");
                    currentFileSizeInBytes = 0;
                } else {
                    currentFileSizeInBytes = audioScrubber.get_file_size_bytes();
                    Serial.print("SCRUB_AUDIO mode: audioScrubber.setFile SUCCEEDED. Size: "); Serial.println(currentFileSizeInBytes);
                }
            }
            AudioInterrupts(); 

            if(currentFileSizeInBytes > 0) {
                audioScrubber.setMode(1); 
                Serial.println("SCRUB_AUDIO mode: audioScrubber internal mode set to 1 (Direct Scrub).");
            } else {
                audioScrubber.setMode(3); 
            }

        } else {
            Serial.println("SCRUB_AUDIO mode: File for scrub does not exist.");
            currentFileSizeInBytes = 0;
            audioScrubber.setMode(3); 
        }
         prevPlaybackLineX = -1; 
         Serial.flush();
    }

    // --- Actions based on Current Mode ---
    if (currentPlaybackMode == PLAYBACK_MODE_SCRUB_AUDIO) {
        // --- audioScrubber internal sub-mode switching (Buttons 3 & 4 on QTouch Panel 1) ---
        if (mode_keys == 32 && audioScrubber.getMode() != 0) { // Button 3 for continuous play sub-mode
            Serial.println("AudioScrubber internal: Switching to Mode 0 (Continuous Play)");
            audioScrubber.setMode(0); 
        } else if (mode_keys == 64 && audioScrubber.getMode() != 1) { // Button 4 for direct scrub sub-mode
            Serial.println("AudioScrubber internal: Switching to Mode 1 (Direct Scrub)");
            audioScrubber.setMode(1); 
            audioScrubber.setSpeed(0.0f); 
        }

        // --- Master Scrub Interaction Slider (sliChange1_active) ---
        if (sliChange1_active) { 
            if (!isScrubbing) {
                isScrubbing = true;
                Serial.println("Audio Scrub: Master Slider Active.");
            }
            
            bool fileSetAttempted = false;
            if (audioScrubber.getMode() == 3 || currentFileSizeInBytes == 0) { 
                fileSetAttempted = true;
                Serial.println("AudioScrubber stopped or file not loaded, attempting setup for master slider.");
                if (SD.exists(fileList[loopingFileIndex])) {
                    Serial.println("INO: Scrub Master Slider: File exists. Stopping audio, re-init SPI & SD.");
                    if(loopPlayer.isPlaying()){ 
                        loopPlayer.stop(); 
                    }
                    audioScrubber.stop(); 
                    
                    AudioNoInterrupts();
                    SPI.end();
                    delay(50);
                    SPI.begin();
                    delay(50);
                    pinMode(SDCARD_CS_PIN, OUTPUT);      // Explicitly set CS pin mode
                    digitalWrite(SDCARD_CS_PIN, HIGH);   // Deselect SD card
                    delay(10);                           // Short delay
                    bool sdReinitSuccess = SD.begin(SDCARD_CS_PIN);
                    
                    if (!sdReinitSuccess) { 
                        Serial.println("INO: Scrub Master Slider: CRITICAL - SD.begin() FAILED during re-init!");
                        currentFileSizeInBytes = 0; 
                    } else {
                        Serial.println("INO: Scrub Master Slider: SD card re-initialized successfully.");
                        delay(20); 
                        Serial.flush();

                        if(audioScrubber.setFile(fileList[loopingFileIndex])) {
                            currentFileSizeInBytes = audioScrubber.get_file_size_bytes(); 
                            Serial.print("Scrub (Master Slider Active): File re-set. Size: "); Serial.println(currentFileSizeInBytes);
                        } else {
                            Serial.println("Scrub (Master Slider Active): audioScrubber.setFile() returned false.");
                            currentFileSizeInBytes = 0; 
                        }
                    }
                    AudioInterrupts(); 

                    if (currentFileSizeInBytes > 0) { 
                        audioScrubber.setMode(1); 
                        audioScrubber.setSpeed(0.0f);
                        Serial.println("INO: Scrub Master Slider: Set to Mode 1 after successful setFile.");
                    } else {
                        audioScrubber.setMode(3); 
                        Serial.println("INO: Scrub Master Slider: Scrubber remains stopped due to setFile issue or zero size.");
                    }

                } else {
                    Serial.println("Scrub (Master Slider Active): File does not exist. Cannot operate.");
                    audioScrubber.setMode(3); 
                    currentFileSizeInBytes = 0;
                }
            } else if (!audioScrubber.isPlaying() && (audioScrubber.getMode() == 0 || audioScrubber.getMode() == 1) && !fileSetAttempted ) { 
                 Serial.println("AudioScrubber was inactive but not mode 3, re-asserting mode to ensure SPI.");
                 audioScrubber.setMode(audioScrubber.getMode()); 
            }
            
            if (currentFileSizeInBytes > 0 && (audioScrubber.getMode() == 0 || audioScrubber.getMode() == 1) ) {
                if (audioScrubber.getMode() == 0) { 
                    float speedControlValue = mapFloat(sliderVal4, 0, 255, -2.0f, 2.0f); 
                    audioScrubber.setSpeed(speedControlValue);
                    
                    float visualTargetNormalized = (float)sliderVal3 / 255.0f;
                    uint32_t totalDurationMs_estimate = estimateTotalMsFromByteSize(currentFileSizeInBytes);
                    uint32_t visualMs = visualTargetNormalized * totalDurationMs_estimate;
                    int currentLineX = waveformX + map(visualMs, 0, totalDurationMs_estimate, 0, waveformWidth - 1);
                    currentLineX = constrain(currentLineX, waveformX, waveformX + waveformWidth - 1);
                    if (currentLineX != prevPlaybackLineX) {
                        updatePlaybackLine(prevPlaybackLineX, currentLineX, peaks, numPeaks, currentMaxPeakForWaveform);
                        prevPlaybackLineX = currentLineX;
                    }

                } else if (audioScrubber.getMode() == 1) { 
                    float targetPositionNormalized = (float)sliderVal3 / 255.0f; 
                    audioScrubber.setTarget(targetPositionNormalized);

                    uint32_t totalDurationMs_estimate = estimateTotalMsFromByteSize(currentFileSizeInBytes);
                    uint32_t visualScrubToMs = targetPositionNormalized * totalDurationMs_estimate;
                    int currentLineX = waveformX + map(visualScrubToMs, 0, totalDurationMs_estimate, 0, waveformWidth - 1);
                    currentLineX = constrain(currentLineX, waveformX, waveformX + waveformWidth - 1);
                    if (currentLineX != prevPlaybackLineX) {
                        updatePlaybackLine(prevPlaybackLineX, currentLineX, peaks, numPeaks, currentMaxPeakForWaveform);
                        prevPlaybackLineX = currentLineX;
                    }
                }
            } else if (audioScrubber.getMode() != 3) { 
                 Serial.println("Scrub (Master Slider Active): File size is 0 or scrubber not in active mode. Cannot process slider input.");
            }

        } else { 
            if (isScrubbing) {
                isScrubbing = false;
                Serial.println("Audio Scrub: Master Slider Inactive.");
                if (audioScrubber.getMode() == 0) { 
                    audioScrubber.setSpeed(0.0f); 
                    Serial.println("AudioScrubber Mode 0: Speed set to 0 (Master slider inactive).");
                }
            }
            if (audioScrubber.getMode() == 1 && playbackLineTimer >= playbackLineUpdateInterval * 2 && currentFileSizeInBytes > 0) { 
                 playbackLineTimer = 0;
                 float currentTargetNormalized = (float)sliderVal3 / 255.0f; 
                 uint32_t totalDurationMs_estimate = estimateTotalMsFromByteSize(currentFileSizeInBytes);
                 uint32_t visualMs = currentTargetNormalized * totalDurationMs_estimate;

                 if (totalDurationMs_estimate > 0) {
                    int currentLineX = waveformX + map(visualMs, 0, totalDurationMs_estimate, 0, waveformWidth - 1);
                    currentLineX = constrain(currentLineX, waveformX, waveformX + waveformWidth - 1);
                    if (currentLineX != prevPlaybackLineX) {
                        updatePlaybackLine(prevPlaybackLineX, currentLineX, peaks, numPeaks, currentMaxPeakForWaveform);
                        prevPlaybackLineX = currentLineX;
                    }
                 }
            }
        }
    } else { 
        isScrubbing = false; 
        if (loopPlayer.isPlaying()) { 
            if (playbackLineTimer >= playbackLineUpdateInterval) { 
                playbackLineTimer = 0; 
                uint32_t currentPosMillis = loopPlayer.positionMillis(); 
                uint32_t totalLengthMillis = loopPlayer.lengthMillis();  
                if (totalLengthMillis > 0) {
                    int currentLineX = waveformX + map(currentPosMillis, 0, totalLengthMillis, 0, waveformWidth - 1);
                    currentLineX = constrain(currentLineX, waveformX, waveformX + waveformWidth - 1);
                    if (currentLineX != prevPlaybackLineX) { 
                        updatePlaybackLine(prevPlaybackLineX, currentLineX, peaks, numPeaks, currentMaxPeakForWaveform);
                        prevPlaybackLineX = currentLineX;
                    }
                }
            }
        } else { 
            if (prevPlaybackLineX != -1) { 
                updatePlaybackLine(prevPlaybackLineX, -1, peaks, numPeaks, currentMaxPeakForWaveform);
                prevPlaybackLineX = -1;
            }
            
            if (SD.exists(fileList[loopingFileIndex])) {
                Serial.print("Looping: Replaying "); Serial.println(fileList[loopingFileIndex]);
                loopPlayer.play(fileList[loopingFileIndex]); 
                prevPlaybackLineX = -1; 
            } else { 
                Serial.println("Looping file became invalid. Stopping granular mode.");
                granularRunning = false; loopingFileIndex = -1; currentFileSizeInBytes = 0;
                mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
                mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);
                tft.fillScreen(ILI9341_BLACK); printFileList(fileList, fileNum); 
            }
        }
    }
  } else { 
    if (isScrubbing) { 
        isScrubbing = false;
        audioScrubber.setMode(3); 
    }
    
    mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
    mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);
    audioScrubber.setRate(1.0f); 
    audioScrubber.setSpeed(1.0f);
    currentPlaybackMode = PLAYBACK_MODE_LOOP; 

    if (sliChange4_active) { 
        handleMenuSelection(); 
    }
  }

  P_sliderVal1 = sliderVal1;
  P_sliderVal4 = sliderVal4; 
} 

void reloadFileList() {
  Serial.println("Reloading file list...");
  loopPlayer.stop();
  audioScrubber.stop(); 
  granularRunning = false; 
  isScrubbing = false; 
  currentPlaybackMode = PLAYBACK_MODE_LOOP; 
  mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
  mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);  
  loopingFileIndex = -1; prevPlaybackLineX = -1; currentMaxPeakForWaveform = 0; 
  currentFileSizeInBytes = 0;

  if (fileList != nullptr) {
    for (int i = 0; i < fileNum; i++) {
      if (fileList[i] != nullptr) { delete[] fileList[i]; fileList[i] = nullptr; }
    }
    delete[] fileList; fileList = nullptr;
  }
  fileNum = 0; 
  fileList = getFileList(fileNum); 
  currentFileIndex = -1; 
  tft.fillScreen(ILI9341_BLACK); 
  printFileList(fileList, fileNum); 
  Serial.println("File list reloaded.");
}

void handleMenuSelection() { 
  int newSelection = -1;
  
  if (sliderVal1 >= 100 && sliderVal1 <= 125 && fileNum > 5) newSelection = 5;
  else if (sliderVal1 >= 126 && sliderVal1 <= 150 && fileNum > 4) newSelection = 4;
  else if (sliderVal1 >= 151 && sliderVal1 <= 175 && fileNum > 3) newSelection = 3;
  else if (sliderVal1 >= 176 && sliderVal1 <= 200 && fileNum > 2) newSelection = 2;
  else if (sliderVal1 >= 201 && sliderVal1 <= 225 && fileNum > 1) newSelection = 1;
  else if (sliderVal1 >= 226 && sliderVal1 <= 255 && fileNum > 0) newSelection = 0;

  if (newSelection != -1 && newSelection != currentFileIndex) {
    currentFileIndex = newSelection;
    tft.fillRect(0,0, 160, tft.height(), ILI9341_BLACK); 
    printFileList(fileList, fileNum); 
    if (currentFileIndex >= 0 && currentFileIndex < fileNum) {
        tft.drawRect(2, (currentFileIndex * 20) + 1, 150, 19, SYNTHIA_LIGHTBLUE); 
    }
    Serial.print("handleMenuSelection: New file selected: "); Serial.println(fileList[currentFileIndex]);
    
    loopPlayer.stop();    
    audioScrubber.stop(); 
    
    mixer1.gain(0, 0.75); mixer1.gain(1, 0.75); 
    mixer1.gain(2, 0.0);  mixer1.gain(3, 0.0);  

    if (SD.exists(fileList[currentFileIndex])) {
      Serial.print("Attempting to play (once with loopPlayer for menu preview): "); Serial.println(fileList[currentFileIndex]);
      if (!loopPlayer.play(fileList[currentFileIndex])) { 
        Serial.println("loopPlayer.play() command FAILED for menu preview.");
      } else {
        Serial.println("loopPlayer.play() command successful for menu preview.");
      }
    } else {
      Serial.print("File not found on SD card for menu preview: "); Serial.println(fileList[currentFileIndex]);
    }
  }
}
