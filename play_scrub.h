// Target file: play_scrub.h

#ifndef PLAY_SCRUB_H_
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
    void activate(bool active_val, int new_mode); // Declaration

    // Override the pure virtual function from AudioStream
    virtual void update(void); // This is essential

    // --- Inline Getter Method Definitions ---
    const char* getFilename() const; // Declaration (definition in .cpp)
    uint64_t get_file_size_bytes() const { return fileSize; }
    int getMode() const { return mode; }
    bool isPlaying() const { return isActive && mode != 3; }


private:
    // --- Private Member Variables ---
    // Order can matter for initialization lists, but primarily for clarity.
    File file;
    int16_t *buf;
    const uint bufLength = AUDIO_BLOCK_SAMPLES * 128; // Initialized here
    uint64_t fileSize; // In bytes
    float playhead;
    float playheadReference; // Byte offset in file corresponding to buf[0]
    float targetPlayhead;    // Target position in bytes
    int index;               // Current sample index in 'buf' for playback
    int bufCounter;          // General purpose counter, if needed for buffer logic
    float speed;             // For continuous playback mode (mode 0)
    int mode;                // 0: Continuous, 1: Direct Scrub, 3: Stopped
    volatile bool isActive;
    volatile float scrubRateFactor; // For interpolated scrubbing (if re-enabled)
    char currentScrubFilename[100]; // Max filename length

    // --- Private Helper Functions ---
    void grabBuffer(int16_t* bufferToFill); // Declaration

    // Inline template member function (definition must be in header)
    template <typename T_param>
    int16_t lerp(int16_t a, int16_t b, float t) {
        return static_cast<int16_t>(a + (b - a) * t);
    }
    // Make sure there are no stray characters or syntax errors above this line.
}; // This should be the closing brace for the class AudioPlayScrub

#endif // PLAY_SCRUB_H_