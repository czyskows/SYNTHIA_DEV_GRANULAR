#ifndef DISPLAY_H
#define DISPLAY_H

#include <ILI9341_t3.h>
#include <font_Arial.h> // from ILI9341_t3 (ensure this font file is available or remove if not used)
#include <font_LiberationMonoBold.h> // (ensure this font file is available)
#include <font_LiberationMono.h>     // (ensure this font file is available)
#include <font_GeorgiaBoldItalic.h>  // (ensure this font file is available or remove if not used)
#include <XPT2046_Touchscreen.h>
#include <SD.h> // Required for File type

// TFT and Touchscreen pins
#define TFT_DC 14
#define TFT_CS 15
#define CS_PIN 35 // Touchscreen CS
#define TIRQ_PIN 2 // Touchscreen IRQ (optional, if used)


// Column definitions for layout (if used by other screen functions)
#define COL_A 5
#define COL_B 60
#define COL_C 115
#define COL_D 170

// External declaration of the TFT object (defined in SYNTHIA_DEV_GRANULAR.ino)
extern ILI9341_t3 tft;
// External declaration of the Touchscreen object (defined in SYNTHIA_DEV_GRANULAR.ino)
extern XPT2046_Touchscreen ts;


// Waveform display properties
const int waveformX = 10;      // X-coordinate of the waveform rectangle (User updated to 10)
const int waveformY = 50;      // Y-coordinate of the waveform rectangle
const int waveformWidth = 280; // Width of the waveform display area
const int waveformHeight = 120; // Height of the waveform display area

// Global state variable, true if granular mode is active (defined in SYNTHIA_DEV_GRANULAR.ino)
extern bool granularRunning;


// Function to read WAV file data, extract peaks, and calculate dynamic samplesPerPeak
int readWavAndGetPeaks(const char* filename, int16_t* peakBuffer, int peakBufferSize) {
  File wavFile = SD.open(filename);
  if (!wavFile) {
    Serial.println("Error opening WAV file!");
    return 0; // Return 0 peaks on error
  }

  // -- WAV Header Parsing --
  uint16_t numChannels = 0;
  uint32_t fileSampleRate = 0; 
  uint16_t bitsPerSample = 0;
  uint32_t dataChunkSize = 0; 

  char chunkID[5] = {0};
  uint32_t chunkSize = 0;
  bool fmtChunkFound = false;
  bool dataChunkFound = false;

  wavFile.seek(0); 

  wavFile.readBytes(chunkID, 4); 
  if (strcmp(chunkID, "RIFF") != 0) {
    Serial.println("Error: Not a RIFF file.");
    wavFile.close();
    return 0;
  }
  wavFile.seek(wavFile.position() + 4); 
  wavFile.readBytes(chunkID, 4); 
  if (strcmp(chunkID, "WAVE") != 0) {
    Serial.println("Error: Not a WAVE file.");
    wavFile.close();
    return 0;
  }

  while (wavFile.available() > 8) { 
    wavFile.readBytes(chunkID, 4);
    chunkID[4] = '\0'; 
    chunkSize = wavFile.read() | (wavFile.read() << 8) | (wavFile.read() << 16) | (wavFile.read() << 24);

    if (strcmp(chunkID, "fmt ") == 0) {
      fmtChunkFound = true;
      wavFile.seek(wavFile.position() + 2); 
      numChannels = wavFile.read() | (wavFile.read() << 8);
      fileSampleRate = wavFile.read() | (wavFile.read() << 8) | (wavFile.read() << 16) | (wavFile.read() << 24);
      wavFile.seek(wavFile.position() + 6); 
      bitsPerSample = wavFile.read() | (wavFile.read() << 8);
      if (chunkSize > 16) {
        wavFile.seek(wavFile.position() + (chunkSize - 16));
      }
    } else if (strcmp(chunkID, "data") == 0) {
      dataChunkFound = true;
      dataChunkSize = chunkSize;
      break; 
    } else {
      wavFile.seek(wavFile.position() + chunkSize);
    }
  }

  if (!fmtChunkFound || !dataChunkFound) {
    Serial.println("Error: 'fmt ' or 'data' chunk not found or file corrupted.");
    wavFile.close();
    return 0;
  }

  Serial.print("Channels: "); Serial.println(numChannels);
  Serial.print("Sample Rate (file): "); Serial.println(fileSampleRate);
  Serial.print("Bits per Sample: "); Serial.println(bitsPerSample);
  Serial.print("Data chunk size (bytes): "); Serial.println(dataChunkSize);

  if (bitsPerSample == 0 || numChannels == 0) {
    Serial.println("Error: Invalid WAV header (bitsPerSample or numChannels is zero).");
    wavFile.close();
    return 0;
  }
  uint16_t bytesPerSampleFrame = numChannels * (bitsPerSample / 8);
  if (bytesPerSampleFrame == 0) {
    Serial.println("Error: Calculated bytesPerSampleFrame is zero (likely invalid bitsPerSample).");
    wavFile.close();
    return 0;
  }
  
  uint32_t totalSampleFramesInFile = dataChunkSize / bytesPerSampleFrame;
  Serial.print("Total sample frames in file: "); Serial.println(totalSampleFramesInFile);

  int dynamicSamplesPerPeak = 1; 
  if (waveformWidth > 0 && totalSampleFramesInFile > 0) { 
    dynamicSamplesPerPeak = totalSampleFramesInFile / waveformWidth;
    if (dynamicSamplesPerPeak == 0) { 
      dynamicSamplesPerPeak = 1;
    }
  }
  Serial.print("Dynamic samplesPerPeak: "); Serial.println(dynamicSamplesPerPeak);

  int peakIndex = 0;
  long samplesReadInCurrentBlock = 0;
  int16_t maxSampleInBlock = 0; 
  
  for (uint32_t frameNum = 0; frameNum < totalSampleFramesInFile && peakIndex < peakBufferSize; ++frameNum) {
    if (wavFile.available() < bytesPerSampleFrame) {
        Serial.println("Warning: Not enough data left in file for a full sample frame.");
        break; 
    }

    int16_t currentFrameSample = 0; 

    if (bitsPerSample == 8) {
      int8_t sample8bit = wavFile.read() - 128; 
      currentFrameSample = (int16_t)sample8bit * 256; 
      for (uint16_t ch = 1; ch < numChannels; ++ch) wavFile.read(); 
    } else if (bitsPerSample == 16) {
      currentFrameSample = (int16_t)(wavFile.read() | (wavFile.read() << 8)); 
      for (uint16_t ch = 1; ch < numChannels; ++ch) { 
        wavFile.read(); wavFile.read();
      }
    } else {
      Serial.println("Unsupported bitsPerSample for peak extraction.");
      break; 
    }

    if (abs(currentFrameSample) > maxSampleInBlock) { 
        maxSampleInBlock = abs(currentFrameSample);
    }
    
    samplesReadInCurrentBlock++;

    if (samplesReadInCurrentBlock >= dynamicSamplesPerPeak) {
      peakBuffer[peakIndex++] = maxSampleInBlock; 
      maxSampleInBlock = 0;
      samplesReadInCurrentBlock = 0;
    }
  }
  
  if (samplesReadInCurrentBlock > 0 && peakIndex < peakBufferSize) {
      peakBuffer[peakIndex++] = maxSampleInBlock;
  }

  wavFile.close();
  Serial.print("Actual peaks extracted: "); Serial.println(peakIndex);
  return peakIndex; 
}


int16_t displayWaveform(int16_t* peakBuffer, int peakCount) {
  tft.fillRect(waveformX, waveformY, waveformWidth, waveformHeight, ILI9341_BLACK); 
  
  int16_t maxOverallPeak = 0; 
  int peaksToConsider = (peakCount < waveformWidth) ? peakCount : waveformWidth;

  for (int i = 0; i < peaksToConsider; i++) {
    if (abs(peakBuffer[i]) > maxOverallPeak) {
      maxOverallPeak = abs(peakBuffer[i]);
    }
  }

  if (maxOverallPeak == 0) { 
    return 0; 
  }

  float scaleFactor = (float)waveformHeight / maxOverallPeak;

  for (int x_offset = 0; x_offset < waveformWidth; x_offset++) {
    if (x_offset < peakCount) { 
      int16_t peakValue = peakBuffer[x_offset]; 
      int h = (int)(peakValue * scaleFactor); 
      
      if (h > 0) { 
        int y_center = waveformY + waveformHeight / 2;
        tft.drawFastVLine(waveformX + x_offset, y_center - h / 2, h, ILI9341_WHITE);
      }
    } else {
      break; 
    }
  }
  return maxOverallPeak; 
}

void updatePlaybackLine(int prevX_onScreen, int currentX_onScreen, int16_t* fullPeakData, int totalNumPeaksInWaveform, int16_t maxOverallPeakValue) {
    if (prevX_onScreen >= waveformX && prevX_onScreen < waveformX + waveformWidth) { 
        int peakDataIndex = prevX_onScreen - waveformX; 

        if (peakDataIndex >= 0 && peakDataIndex < totalNumPeaksInWaveform && peakDataIndex < waveformWidth) {
            if (maxOverallPeakValue > 0) { 
                float scaleFactor = (float)waveformHeight / maxOverallPeakValue;
                int16_t peakValue = fullPeakData[peakDataIndex]; 
                int h_orig = (int)(abs(peakValue * scaleFactor));

                if (h_orig > 0) { 
                    int y_center = waveformY + waveformHeight / 2;
                    tft.drawFastVLine(prevX_onScreen, y_center - h_orig / 2, h_orig, ILI9341_WHITE); 
                } else {
                    tft.drawFastVLine(prevX_onScreen, waveformY, waveformHeight, ILI9341_BLACK);
                }
            } else { 
                tft.drawFastVLine(prevX_onScreen, waveformY, waveformHeight, ILI9341_BLACK);
            }
        } else {
            tft.drawFastVLine(prevX_onScreen, waveformY, waveformHeight, ILI9341_BLACK);
        }
    }

    if (currentX_onScreen >= waveformX && currentX_onScreen < waveformX + waveformWidth) {
        tft.drawFastVLine(currentX_onScreen, waveformY, waveformHeight, ILI9341_BLUE); 
    }
}


void granularScreen(int16_t* currentPeaks, int currentNumPeaks, int16_t* outMaxPeakForDisplay) {
  tft.fillScreen(ILI9341_BLACK); 

  tft.setFont(LiberationMono_12); 
  
  // Button 1: LOOP (Original "PITCH" position and size)
  tft.fillRect(162, 210, 60, 20, ILI9341_WHITE); 
  tft.setTextColor(ILI9341_BLACK); // Black text on white button
  tft.setCursor(154 + (60 - 4*7)/2, 213); // Centering "LOOP" (4 chars * ~7px width)             
  tft.print("SCRUB");

  // Button 2: SCRUB (Original "FREEZE" position and size)
  tft.fillRect(250, 210, 60, 20, ILI9341_WHITE); 
  tft.setTextColor(ILI9341_BLACK); // Black text on white button
  tft.setCursor(247 + (60 - 5*7)/2, 213); // Centering "SCRUB" (5 chars * ~7px width)                    
  tft.print("LOOP");
  
  if (currentNumPeaks > 0 && currentPeaks != nullptr) {
    *outMaxPeakForDisplay = displayWaveform(currentPeaks, currentNumPeaks);
  } else {
    *outMaxPeakForDisplay = 0;
    tft.fillRect(waveformX, waveformY, waveformWidth, waveformHeight, ILI9341_BLACK);
  }
}


// --- Placeholder definitions for other screen functions ---
void waveformScreen(){ 
  tft.fillScreen(ILI9341_DARKCYAN); tft.setCursor(10,10); tft.setFont(LiberationMono_12); 
  tft.setTextColor(ILI9341_WHITE); tft.print("Synth Waveform Edit Screen"); 
}
void delayScreen(){ 
  tft.fillScreen(ILI9341_DARKGREEN); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Delay Settings"); 
}
void envelopeScreen(){ 
  tft.fillScreen(ILI9341_DARKGREY); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Envelope Settings"); 
}
void filterScreen(){ 
  tft.fillScreen(ILI9341_MAROON); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Filter Settings"); 
}
void levelsScreen(){ 
  tft.fillScreen(ILI9341_NAVY); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Levels Settings"); 
}
void octaveScreen(){ 
  tft.fillScreen(ILI9341_OLIVE); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Octave Settings"); 
}
void reverbScreen(){ 
  tft.fillScreen(ILI9341_PURPLE); tft.setCursor(10,10); tft.setFont(LiberationMono_12);
  tft.setTextColor(ILI9341_WHITE); tft.print("Reverb Settings"); 
}

void SIN(){ 
  tft.fillScreen(ILI9341_BLACK);
  float Ym = tft.height() / 2.0; 
  float Xi = 10; 
  float Xf = tft.width() - Xi; 
  float amplitude = tft.height() / 3.0; 
  uint16_t sineColor = ILI9341_BLUE; 

  for (long angle_deg = 0; angle_deg <= 360; angle_deg++) {
    float rad = angle_deg * PI / 180.0; 
    float current_x_pos = Xi + (angle_deg * (Xf - Xi) / 360.0);
    float current_y_pos = Ym - (amplitude * sin(3 * rad)); 
    
    tft.drawPixel((int)current_x_pos, (int)current_y_pos, sineColor);
    
    if (angle_deg % 20 == 0) { 
      delay(1); 
    }
  }
}

#endif // DISPLAY_H
