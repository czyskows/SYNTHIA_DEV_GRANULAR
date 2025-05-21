#include "play_scrub.h" // Should be first

// Constructor Definition
AudioPlayScrub::AudioPlayScrub() :
    AudioStream(0, NULL),     
    file(),                   
    buf(nullptr),
    // bufLength is const and initialized in the .h
    fileSize(0),
    playhead(0.0f),
    playheadReference(0.0f),
    targetPlayhead(0.0f),
    index(0),
    bufCounter(0),
    speed(1.0f),
    mode(3), // Default to stopped mode                 
    isActive(false),
    scrubRateFactor(0.0001f)
{
    currentScrubFilename[0] = '\0'; 
    Serial.println("AudioPlayScrub instance created (constructor).");
}

// Definition for activate
void AudioPlayScrub::activate(bool active_val, int new_mode_val) {
    isActive = active_val;
    Serial.print("AudioPlayScrub::activate - isActive set to: "); Serial.print(isActive);
    Serial.print(", current internal mode is: "); Serial.println(this->mode); 
    Serial.print("AudioPlayScrub::activate - requested new_mode_val: "); Serial.println(new_mode_val);

    if (!isActive && this->mode != 3) { 
        setMode(3); 
    } else if (isActive && (this->mode == 3 || this->mode != new_mode_val) && new_mode_val != 3) {
        // If activating, or changing to a different active mode
        setMode(new_mode_val); 
    } else if (isActive && new_mode_val == 3) { // If explicitly told to stop while active
        setMode(3); 
    }
    // If already active and new_mode_val is the same, setMode handles it gracefully.
}

// Definition for setFile (with aggressive SPI/SD reset)
bool AudioPlayScrub::setFile(const char *fn_param) {
    Serial.println("AudioPlayScrub::setFile - ENTER");
    Serial.print("AudioPlayScrub::setFile - Attempting to load: "); Serial.println(fn_param);
    
    bool wasActive = isActive;
    int previousMode = mode;

    // Ensure this object is not actively using SPI for audio output during SD operations
    if (isActive) {
      AudioNoInterrupts(); 
      AudioStopUsingSPI(); 
      isActive = false;    // Temporarily mark as inactive for file ops
      Serial.println("AudioPlayScrub::setFile - Marked inactive, SPI stopped for this object for SD ops.");
      AudioInterrupts();
    }

    Serial.println("AudioPlayScrub::setFile - Attempting full SPI and SD re-initialization...");
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
        // Do not call setMode(3) here as it might conflict with AudioInterrupts state
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

    if (!file) { // Check if file object is valid (file opened successfully)
        Serial.print("AudioPlayScrub::setFile - SD.open() FAILED for: "); Serial.println(fn_param);
        AudioInterrupts(); 
        fileSize = 0;
        playhead = 0.0f;
        targetPlayhead = 0.0f;
        currentScrubFilename[0] = '\0';
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
        // isActive is already false from above. mode will be 3.
        Serial.println("AudioPlayScrub::setFile - EXIT (File size 0 after open)");
        return false;
    }
    
    // If the object was active before setFile, restore its previous active mode.
    // Otherwise, it remains inactive (mode 3) until explicitly set by the main sketch.
    if (wasActive && previousMode != 3) {
        setMode(previousMode); 
    } else {
        // If it was not active, or was mode 3, ensure it's properly in mode 3 now.
        // This is important because setFile might be called when the object is already stopped.
        setMode(3); 
    }
    Serial.println("AudioPlayScrub::setFile - EXIT (Success)");
    return true;
}

const char* AudioPlayScrub::getFilename() const {
    return currentScrubFilename;
}

void AudioPlayScrub::setMode(int mode_val) {
    Serial.print("AudioPlayScrub::setMode - Setting mode to: "); Serial.println(mode_val);
    // int oldMode = mode; // Can remove if not strictly needed for complex logic paths
    bool oldActiveState = isActive;

    mode = mode_val; 

    if (mode == 3) { // Stopped
        isActive = false;
        if (oldActiveState) { // Only stop SPI if it was active
            AudioStopUsingSPI();
            Serial.println("AudioPlayScrub::setMode - Mode 3 (Stopped). SPI released for audio.");
        } else {
            Serial.println("AudioPlayScrub::setMode - Mode 3 (Stopped). Was already inactive.");
        }
    } else { // Playback (mode 0) or Scrub (mode 1)
        isActive = true;
        // Always call AudioStartUsingSPI when becoming active for audio processing.
        AudioStartUsingSPI();
        Serial.print("AudioPlayScrub::setMode - Mode "); Serial.print(mode); Serial.println(" (Active). SPI acquired for audio.");
    }
}

void AudioPlayScrub::setTarget(float target) {
    if (fileSize > 0) {
        target = constrain(target, 0.0f, 1.0f);
        uint64_t maxPlayheadBytes = (fileSize > 2) ? fileSize - 2 : 0; 
        targetPlayhead = target * maxPlayheadBytes; 
    } else {
        targetPlayhead = 0;
    }
}

void AudioPlayScrub::setRate(float rate) {
    scrubRateFactor = rate; 
}

void AudioPlayScrub::setSpeed(float speed_val) {
    speed = speed_val;
}

void AudioPlayScrub::stop(void) {
    Serial.println("AudioPlayScrub::stop - ENTER");
    setMode(3); 
    playhead = 0.0f;
    targetPlayhead = 0.0f;
    index = 0;
    bufCounter = 0;
    Serial.println("AudioPlayScrub::stop - Playback stopped. EXIT");
}

void AudioPlayScrub::setScrubBuffer(int16_t *b) {
    Serial.println("AudioPlayScrub::setScrubBuffer - ENTER");
    if (b == nullptr) {
        Serial.println("AudioPlayScrub::setScrubBuffer - Error: Provided buffer is null.");
    }
    buf = b; 
    Serial.print("AudioPlayScrub::setScrubBuffer - Buffer assigned. Expected length (samples): ");
    Serial.println(bufLength);
    Serial.println("AudioPlayScrub::setScrubBuffer - EXIT");
}

void AudioPlayScrub::update(void) {
    audio_block_t *block;
    int16_t *p; 

    if (!isActive || mode == 3 || !buf || bufLength == 0 || fileSize == 0) {
        block = allocate();
        if (block) {
            memset(block->data, 0, sizeof(block->data));
            transmit(block);
            release(block);
        }
        return;
    }

    block = allocate();
    if (!block) {
        return;
    }
    p = block->data;

    if (mode == 0) { 
        playhead += (float)(AUDIO_BLOCK_SAMPLES * 2) * speed; 
        while (playhead >= fileSize && fileSize > 0) playhead -= fileSize; 
        while (playhead < 0 && fileSize > 0) playhead += fileSize;      

        if (playhead < playheadReference || playhead >= (playheadReference + (bufLength * 2.0f))) {
             // Critical section for SD card access if grabBuffer reads from SD
             AudioNoInterrupts();
             // It's generally not recommended to do SD card I/O directly in update() due to timing.
             // For now, assuming grabBuffer is fast enough or handles SPI carefully if it does I/O.
             // Best practice: grabBuffer should only manipulate already loaded data if called from update.
             // If it MUST read from SD, it needs careful SPI management.
             // The current grabBuffer in this artifact does read from SD.
             // This might require temporarily stopping audio SPI if active.
             bool spiWasActiveForAudio = isActive; // Check if audio system thinks SPI is for it
             if(spiWasActiveForAudio) AudioStopUsingSPI();
             
             grabBuffer(buf); 
             
             if(spiWasActiveForAudio) AudioStartUsingSPI(); // Restore SPI for audio
             AudioInterrupts();
        }
        index = round(mapFloat(playhead - playheadReference, 0.0f, bufLength * 2.0f, 0.0f, bufLength - 1.0f));

    } else if (mode == 1) { 
        playhead = targetPlayhead; 
        if (playhead >= fileSize && fileSize > 0) playhead = fileSize - 2; 
        if (playhead < 0) playhead = 0;

        if (playhead < playheadReference || playhead >= (playheadReference + (bufLength * 2.0f))) {
            // Similar SPI management considerations as above if grabBuffer does I/O
            AudioNoInterrupts();
            bool spiWasActiveForAudio = isActive;
            if(spiWasActiveForAudio) AudioStopUsingSPI();
            
            grabBuffer(buf); 
            
            if(spiWasActiveForAudio) AudioStartUsingSPI();
            AudioInterrupts();
        }
        index = round(mapFloat(playhead - playheadReference, 0.0f, bufLength * 2.0f, 0.0f, bufLength - 1.0f));
    }
    
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        int currentSampleIndexInBuf = index + i; 
        if (currentSampleIndexInBuf >= 0 && currentSampleIndexInBuf < (int)bufLength) {
             *p++ = buf[currentSampleIndexInBuf];
        } else {
             *p++ = 0; 
        }
    }
    transmit(block);
    release(block);
}

void AudioPlayScrub::grabBuffer(int16_t* bufferToFill) {
    Serial.println("AudioPlayScrub::grabBuffer - ENTER"); 
    Serial.flush(); 

    // This function assumes the CALLER has managed SPI state (e.g., AudioNoInterrupts, SPI.begin, SD.begin)
    // It should NOT manage AudioStart/StopUsingSPI or SPI.begin/end itself.

    if (!file) { Serial.println("AudioPlayScrub::grabBuffer - No file object! EXIT"); return; }
    if (!file) { Serial.println("AudioPlayScrub::grabBuffer - File is not open (checked via !file)! EXIT"); return; } // Corrected check
    if (!bufferToFill) { Serial.println("AudioPlayScrub::grabBuffer - bufferToFill is null! EXIT"); return; }
    if (bufLength == 0) { Serial.println("AudioPlayScrub::grabBuffer - bufLength is 0! EXIT"); return; }
    if (fileSize == 0) { Serial.println("AudioPlayScrub::grabBuffer - fileSize is 0! EXIT"); return; }

    playheadReference = playhead - (bufLength / 2.0f) * 2.0f; 
    Serial.print("AudioPlayScrub::grabBuffer - Target playhead for buffer center (bytes): "); Serial.println(playhead);
    Serial.print("AudioPlayScrub::grabBuffer - Initial playheadReference (bytes): "); Serial.println(playheadReference);
    
    if (playheadReference < 0) playheadReference = 0;
    
    uint64_t bufferSizeInBytes = (uint64_t)bufLength * 2; 
    if (playheadReference > fileSize - bufferSizeInBytes) {
        if (fileSize > bufferSizeInBytes) {
            playheadReference = fileSize - bufferSizeInBytes;
        } else {
            playheadReference = 0; 
        }
    }
    Serial.print("AudioPlayScrub::grabBuffer - Clamped playheadReference (bytes): "); Serial.println(playheadReference);
    Serial.print("AudioPlayScrub::grabBuffer - Seeking to (bytes): "); Serial.println(playheadReference);
    
    if (!file.seek(playheadReference)) {
        Serial.println("AudioPlayScrub::grabBuffer - file.seek() FAILED!");
        for (uint32_t i = 0; i < bufLength; ++i) bufferToFill[i] = 0; 
        Serial.println("AudioPlayScrub::grabBuffer - EXIT (after seek fail)");
        return;
    }
    Serial.println("AudioPlayScrub::grabBuffer - file.seek() successful.");

    Serial.print("AudioPlayScrub::grabBuffer - Attempting to read total bytes: "); Serial.println(bufferSizeInBytes);
    
    const uint32_t chunkSize = 512; 
    uint32_t totalBytesRead = 0;
    bool readErrorOccurred = false; 

    Serial.println("AudioPlayScrub::grabBuffer - DEBUG: Starting chunked read loop...");
    Serial.flush();

    for (uint32_t offset = 0; offset < bufferSizeInBytes; offset += chunkSize) {
        uint32_t bytesToReadThisChunk = chunkSize;
        if (offset + chunkSize > bufferSizeInBytes) { 
            bytesToReadThisChunk = bufferSizeInBytes - offset;
        }
        
        uint32_t currentFilePos = file.position(); 
        if (currentFilePos + bytesToReadThisChunk > fileSize) { 
            if (fileSize > currentFilePos) {
                bytesToReadThisChunk = fileSize - currentFilePos;
            } else {
                bytesToReadThisChunk = 0; 
            }

            if (bytesToReadThisChunk == 0 && totalBytesRead < bufferSizeInBytes) {
                 Serial.println("AudioPlayScrub::grabBuffer - DEBUG: Calculated 0 bytes for chunk, but not all data read. EOF likely.");
                 break; 
            }
        }

        uint8_t* currentBufferPosition = (uint8_t*)bufferToFill + offset;

        Serial.print("AudioPlayScrub::grabBuffer - DEBUG: Reading chunk. File pos: "); Serial.print(currentFilePos);
        Serial.print(", Offset in buf: "); Serial.print(offset);
        Serial.print(", BytesToReadThisChunk: "); Serial.println(bytesToReadThisChunk);
        Serial.flush();
        
        if(bytesToReadThisChunk == 0 && offset < bufferSizeInBytes && totalBytesRead < bufferSizeInBytes) { 
            Serial.println("AudioPlayScrub::grabBuffer - DEBUG: Calculated 0 bytes for chunk before buffer full. Breaking.");
            break;
        }
        if (bytesToReadThisChunk == 0 && totalBytesRead >= bufferSizeInBytes) break; 


        int chunkBytesRead = file.read(currentBufferPosition, bytesToReadThisChunk);
        
        Serial.print("AudioPlayScrub::grabBuffer - DEBUG: Chunk read complete. Bytes read in chunk: "); Serial.println(chunkBytesRead);
        Serial.flush();

        if (chunkBytesRead < 0) { 
            Serial.println("AudioPlayScrub::grabBuffer - ERROR during chunk read!");
            readErrorOccurred = true; 
            break; 
        }
        
        totalBytesRead += chunkBytesRead;

        if (chunkBytesRead < (int)bytesToReadThisChunk) { 
            Serial.println("AudioPlayScrub::grabBuffer - WARNING: Partial chunk read or EOF. Read " + String(chunkBytesRead) + "/" + String(bytesToReadThisChunk));
            break; 
        }
         if (totalBytesRead >= bufferSizeInBytes) break; 
    }
    
    Serial.print("AudioPlayScrub::grabBuffer - Total bytes read from chunked loop: "); Serial.println(totalBytesRead);
    Serial.flush();

    if (readErrorOccurred) {
        Serial.println("AudioPlayScrub::grabBuffer - Read ERROR occurred. Filling buffer with silence.");
        for (uint32_t i = 0; i < bufLength; ++i) bufferToFill[i] = 0;
    } else if (totalBytesRead < bufferSizeInBytes) { 
        Serial.print("AudioPlayScrub::grabBuffer - Partial read overall. Read "); Serial.print(totalBytesRead); 
        Serial.print(" bytes, expected "); Serial.println(bufferSizeInBytes);
        uint32_t samplesActuallyRead = totalBytesRead / 2;
        for (uint32_t i = samplesActuallyRead; i < bufLength; ++i) { 
            bufferToFill[i] = 0;
        }
         if (totalBytesRead == 0 && bufferSizeInBytes > 0) {
             Serial.println("AudioPlayScrub::grabBuffer - No bytes read at all. Buffer is all silence.");
         }
    } else if (totalBytesRead == bufferSizeInBytes) { 
        Serial.println("AudioPlayScrub::grabBuffer - Successfully read all expected bytes via chunks.");
    } else { 
        Serial.print("AudioPlayScrub::grabBuffer - UNEXPECTED read outcome. totalBytesRead: "); Serial.print(totalBytesRead);
        Serial.print(", bufferSizeInBytes: "); Serial.println(bufferSizeInBytes);
        Serial.println("AudioPlayScrub::grabBuffer - Filling buffer with silence as a precaution.");
        for (uint32_t i = 0; i < bufLength; ++i) bufferToFill[i] = 0;
    }

    bufCounter = 0; 
    Serial.println("AudioPlayScrub::grabBuffer - EXIT");
}