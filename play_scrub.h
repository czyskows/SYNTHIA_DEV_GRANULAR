#ifndef PLAY_SCRUB_H_ // Standard include guard
#define PLAY_SCRUB_H_

#include <Arduino.h> // For standard types, Serial, delay, math functions etc.
#include <Audio.h>   // For AudioStream and AUDIO_BLOCK_SAMPLES
#include <SD.h>      // For File object
#include <SPI.h>     // For SPI.begin(), SPI.end() if used inside the class

// --- Utility Functions (Inline in Header) ---
// mapFloat defined here to be available wherever play_scrub.h is included.
inline float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  if (in_max - in_min == 0) return out_min; // Avoid division by zero
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Fallback for SDCARD_CS_PIN if not defined elsewhere (e.g. in main sketch)
// Ideally, this should be passed to the constructor or configured if it can vary.
#ifndef SDCARD_CS_PIN
#define SDCARD_CS_PIN BUILTIN_SDCARD
#endif

class AudioPlayScrub : public AudioStream {
public:
    AudioPlayScrub(); // Constructor DECLARATION

    // --- Public Method Declarations ---
    bool setFile(const char *filename);
    void setTarget(float target);
    void setRate(float rate);
    void setSpeed(float speed_val);
    void setMode(int mode_val);
    void stop(void);
    void setScrubBuffer(int16_t* b);
    void activate(bool active_val, int new_mode); 

    // Override the pure virtual function from AudioStream
    virtual void update(void); // This is essential

    // --- Getter Method Declarations (definitions in .cpp or inline) ---
    const char* getFilename() const; 
    uint64_t get_file_size_bytes() const { return fileSize; } // Inline definition
    int getMode() const { return mode; }                     // Inline definition
    bool isPlaying() const { return isActive && mode != 3; } // Inline definition


private:
    // --- Private Member Variables ---
    File file;
    int16_t *buf;
    const uint bufLength = AUDIO_BLOCK_SAMPLES * 128; // Initialized here
    uint64_t fileSize; 
    float playhead;
    float playheadReference; 
    float targetPlayhead;    
    int index;               
    int bufCounter;          
    float speed;             
    int mode;                
    volatile bool isActive;
    volatile float scrubRateFactor; 
    char currentScrubFilename[100]; // Max filename length, ensure it's large enough

    // --- Private Helper Functions ---
    void grabBuffer(int16_t* bufferToFill); // Declaration

    // Inline template member function (definition must be in header)
    template <typename T_param> 
    int16_t lerp(int16_t a, int16_t b, float t) {
        return static_cast<int16_t>(a + (b - a) * t);
    }
}; 

#endif // PLAY_SCRUB_H_