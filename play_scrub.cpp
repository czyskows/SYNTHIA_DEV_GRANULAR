#include "play_scrub.h"
// #include <Arduino.h> // Already included via play_scrub.h if structured as above
// #include <SPI.h>     // Already included via play_scrub.h
// #include <SD.h>      // Already included via play_scrub.h
// #include <Audio.h>   // Already included via play_scrub.h

AudioPlayScrub::AudioPlayScrub() :
    AudioStream(0, NULL),     // Base class constructor
    file(),                   // Default initialize File object
    buf(nullptr),
    // bufLength is const and initialized in the .h
    fileSize(0),
    playhead(0.0f),
    playheadReference(0.0f),
    targetPlayhead(0.0f),
    index(0),
    bufCounter(0),
    speed(1.0f),
    mode(3),                  // Default to stopped mode
    isActive(false),
    scrubRateFactor(0.0001f)
{
    currentScrubFilename[0] = '\0'; // Initialize filename to empty
    Serial.println("AudioPlayScrub instance created (constructor).");
}

void AudioPlayScrub::activate(bool active_val, int new_mode_val) {
    // Note: Original had AudioStart/StopUsingSPI here.
    // These were removed from setFile/grabBuffer due to conflicts.
    // If they are needed for the audio codec specifically when activating/deactivating
    // the scrubber object (distinct from SD card access), they might be okay here.
    // However, setMode also handles AudioStart/StopUsingSPI.
    // For now, let's keep it simple and let setMode manage SPI for audio.
    isActive = active_val;
    mode = new_mode_val; // Use a different parameter name
    Serial.print("AudioPlayScrub::activate - isActive: "); Serial.print(isActive);
    Serial.print(", mode set to: "); Serial.println(mode);
    // If activating to a playing mode (0 or 1), setMode will handle AudioStartUsingSPI.
    // If deactivating (e.g., new_mode_val = 3), setMode will handle AudioStopUsingSPI.
    if (!isActive && mode != 3) { // If explicitly told not active, ensure mode is stopped
        setMode(3);
    } else if (isActive && mode == 3) { // If told active but mode is stopped, pick a default active mode
        setMode(1); // Default to direct scrub if activated without a specific play mode
    }
}


// --- Definitions for AudioPlayScrub methods ---

bool AudioPlayScrub::setFile(const char *fn_param) {
    Serial.println("AudioPlayScrub::setFile - ENTER");
    Serial.print("AudioPlayScrub::setFile - Attempting to load: "); Serial.println(fn_param);
    bool previousActiveState = isActive; // Remember if it was active
    int previousMode = mode;             // Remember current mode

    // --- Aggressive SPI and SD Re-initialization ---
    Serial.println("AudioPlayScrub::setFile - Disabling audio interrupts, re-init SPI and SD...");
    AudioNoInterrupts(); 

    SPI.end();    
    delay(50);    
    SPI.begin();  
    delay(50);    

    pinMode(SDCARD_CS_PIN, OUTPUT);      
    digitalWrite(SDCARD_CS_PIN, HIGH);   
    delay(20);                           

    bool sdBeginSuccess = SD.begin(SDCARD_CS_PIN); 

    if (!sdBeginSuccess) {
        Serial.println("AudioPlayScrub::setFile - CRITICAL: SD.begin() FAILED during internal re-init!");
        AudioInterrupts(); 
        fileSize = 0;
        playhead = 0.0f;
        targetPlayhead = 0.0f;
        currentScrubFilename[0] = '\0';
        setMode(3); // This will call AudioStopUsingSPI()
        Serial.println("AudioPlayScrub::setFile - EXIT (SD re-init failed)");
        return false;
    }
    Serial.println("AudioPlayScrub::setFile - SD card re-initialized successfully internally.");
    delay(20); 
    Serial.flush(); 

    if (file) {
        Serial.println("AudioPlayScrub::setFile - Closing previously open file (if any).");
        file.close();
    }

    Serial.print("AudioPlayScrub::setFile - Attempting SD.open for: "); Serial.println(fn_param);
    file = SD.open(fn_param, FILE_READ);

    if (!file) {
        Serial.print("AudioPlayScrub::setFile - SD.open() FAILED for: "); Serial.println(fn_param);
        AudioInterrupts(); 
        fileSize = 0;
        playhead = 0.0f;
        targetPlayhead = 0.0f;
        currentScrubFilename[0] = '\0';
        setMode(3); // This will call AudioStopUsingSPI()
        Serial.println("AudioPlayScrub::setFile - EXIT (SD.open failed)");
        return false;
    }

    Serial.print("AudioPlayScrub::setFile - SD.open() SUCCEEDED for: "); Serial.println(fn_param);
    fileSize = file.size();
    Serial.print("AudioPlayScrub::setFile - File Size (bytes): "); Serial.println(fileSize);

    strncpy(currentScrubFilename, fn_param, sizeof(currentScrubFilename) - 1);
    currentScrubFilename[sizeof(currentScrubFilename) - 1] = '\0'; 

    playhead = 0.0f;
    targetPlayhead = 0.0f;
    playheadReference = 0.0f;
    index = 0;
    bufCounter = 0;

    if (buf && bufLength > 0 && fileSize > 0) {
        Serial.println("AudioPlayScrub::setFile - Pre-filling buffer.");
        grabBuffer(buf); 
    } else {
        if (!buf) Serial.println("AudioPlayScrub::setFile - Buffer (buf) is null.");
        if (bufLength == 0) Serial.println("AudioPlayScrub::setFile - Buffer length (bufLength) is 0.");
        if (fileSize == 0) Serial.println("AudioPlayScrub::setFile - File size is 0, not pre-filling buffer.");
    }
    
    AudioInterrupts(); 

    if (fileSize == 0) {
        Serial.println("AudioPlayScrub::setFile - File opened but size is 0. Treating as failure.");
        if(file) file.close();
        currentScrubFilename[0] = '\0';
        setMode(3); // This will call AudioStopUsingSPI()
        Serial.println("AudioPlayScrub::setFile - EXIT (File size 0 after open)");
        return false;
    }
    
    // Restore previous active state/mode if it wasn't stop, otherwise stop.
    if (previousActiveState && previousMode != 3) {
        setMode(previousMode); // This will call AudioStartUsingSPI()
    } else {
        setMode(3); // This will call AudioStopUsingSPI()
    }
    Serial.println("AudioPlayScrub::setFile - EXIT (Success)");
    return true;
}

const char* AudioPlayScrub::getFilename() const {
    return currentScrubFilename;
}

// ... (Your existing definitions for setTarget, setRate, setSpeed, stop, setScrubBuffer, update, grabBuffer)
// Make sure they do NOT contain AudioStartUsingSPI() or AudioStopUsingSPI() if they perform SD operations.
// setMode IS the correct place to manage AudioStart/StopUsingSPI for the audio object's activity.

void AudioPlayScrub::setMode(int mode_val) {
    Serial.print("AudioPlayScrub::setMode - Setting mode to: "); Serial.println(mode_val);
    int oldMode = mode;
    mode = mode_val;

    if (mode == 3) { // Stopped
        isActive = false;
        if (oldMode != 3) { // Only call AudioStopUsingSPI if it was previously active
            AudioStopUsingSPI();
            Serial.println("AudioPlayScrub::setMode - Mode 3 (Stopped). SPI released for audio.");
        } else {
            Serial.println("AudioPlayScrub::setMode - Mode 3 (Stopped). SPI was already released or not used by audio.");
        }
    } else { // Playback (mode 0) or Scrub (mode 1)
        isActive = true;
        // Only call AudioStartUsingSPI if it's transitioning from a stopped state or if SPI wasn't for audio
        // This assumes that if mode was already 0 or 1, SPI was already configured for audio.
        // If changing between mode 0 and 1 while active, SPI context for audio should be maintained.
        if (oldMode == 3 || !AudioIsSPI()) { // AudioIsSPI() is a hypothetical function,
                                            // more robustly, just call AudioStartUsingSPI() if becoming active
            AudioStartUsingSPI();
            Serial.print("AudioPlayScrub::setMode - Mode "); Serial.print(mode); Serial.println(" (Active). SPI acquired for audio.");
        } else {
            Serial.print("AudioPlayScrub::setMode - Mode "); Serial.print(mode); Serial.println(" (Active). SPI presumed already for audio.");
        }
    }
}