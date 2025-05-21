// sdFiles.h
//
// handle audio files on micro sd
//

#ifndef SDFILES_H
#define SDFILES_H

#include <string.h> // For strlen and strstr

// Forward declaration for tft if used in this file (e.g. for error messages)
// #include "display.h" // Or include display.h if tft is needed here for printFileList status

extern File file; // If 'file' is a global variable used across multiple files.
                  // If it's only used within functions here, it can be local.
                  // Based on original code, it seems unused as a global.
                  // Let's declare it locally or remove if not used.
                  // For now, I'll assume it might be intended for future use or was used before.


// Function to check if the filename ends with .WAV or .RAW (case-insensitive for extension)
// Changed filename to const char*
bool isFnMusic(const char* filename) {
  int8_t len = strlen(filename);
  if (len < 4) return false; // Not long enough for ".WAV"

  // Check for .WAV or .wav
  if ( (filename[len-4] == '.' && (filename[len-3] == 'W' || filename[len-3] == 'w') && (filename[len-2] == 'A' || filename[len-2] == 'a') && (filename[len-1] == 'V' || filename[len-1] == 'v')) ) {
    return true;
  }
  // Check for .RAW or .raw
  if ( (filename[len-4] == '.' && (filename[len-3] == 'R' || filename[len-3] == 'r') && (filename[len-2] == 'A' || filename[len-2] == 'a') && (filename[len-1] == 'W' || filename[len-1] == 'w')) ) {
    return true;
  }
  return false;
}

// Function to get the list of music files from the SD card
char** getFileList(int &fileCount) {
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open SD root directory!");
    fileCount = 0;
    return nullptr;
  }
  if (!root.isDirectory()) {
    Serial.println("SD root is not a directory!");
    root.close();
    fileCount = 0;
    return nullptr;
  }

  // First pass: count WAV/RAW files
  fileCount = 0;
  File entry;
  root.rewindDirectory(); // Make sure we start from the beginning

  while (true) {
    entry = root.openNextFile();
    if (!entry) { // No more files
      break;
    }
    if (!entry.isDirectory() && isFnMusic(entry.name())) {
      fileCount++;
    }
    entry.close();
  }
  Serial.print("Number of music files found: ");
  Serial.println(fileCount);


  if (fileCount == 0) {
    root.close();
    return nullptr;
  }

  // Allocate memory for the file list
  char** fileList = new char*[fileCount];
  if (!fileList) {
    Serial.println("Memory allocation failed for fileList!");
    root.close();
    fileCount = 0; // Ensure fileCount reflects the failure
    return nullptr;
  }

  // Second pass: populate the file list
  root.rewindDirectory(); // Reset directory iterator
  int i = 0;
  while (i < fileCount) { // Iterate only up to the counted files
    entry = root.openNextFile();
    if (!entry) {
      Serial.println("Error: Ran out of entries before filling fileList (should not happen).");
      break; // Should not happen if counting was correct
    }
    if (!entry.isDirectory() && isFnMusic(entry.name())) {
      fileList[i] = new char[strlen(entry.name()) + 1];
      if (!fileList[i]) {
         Serial.print("Memory allocation failed for fileList["); Serial.print(i); Serial.println("]!");
         // Clean up previously allocated strings if one fails
         for (int j = 0; j < i; j++) delete[] fileList[j];
         delete[] fileList;
         fileCount = 0;
         entry.close();
         root.close();
         return nullptr;
      }
      strcpy(fileList[i], entry.name());
      i++;
    }
    entry.close();
  }
  root.close();

  // If 'i' is less than fileCount here, it means not all expected files were populated.
  // This could indicate an issue, but for now, we'll proceed with what was read.
  if (i < fileCount) {
    Serial.print("Warning: Populated "); Serial.print(i); Serial.print(" files, but counted "); Serial.println(fileCount);
    // Optionally, you could resize fileList or handle this as an error.
    // For simplicity, we'll assume the list up to 'i' is valid.
    fileCount = i; // Update fileCount to actual number of files added
  }

  return fileList;
}

// Function to print the file list to the TFT display
// Make sure display.h is included where tft is used, or tft is passed as a parameter.
// For now, assuming tft is globally accessible via display.h inclusion in the .ino
void printFileList(char** fileList, int fileNum) {
  // This function uses 'tft' which is defined in SYNTHIA_DEV_GRANULAR.ino
  // and declared extern in display.h. Ensure display.h is included in SYNTHIA_DEV_GRANULAR.ino
  // before sdFiles.h if sdFiles.h needs to call this with a global tft.
  // Or, pass tft as an argument: void printFileList(ILI9341_t3& display, char** fileList, int fileNum)

  tft.fillScreen(ILI9341_BLACK); // Clear the screen
  tft.setFont(LiberationMono_12); // Ensure this font is loaded/available
  tft.setTextColor(ILI9341_WHITE);
  
  if (fileList == nullptr || fileNum == 0) {
    tft.setCursor(10, 20);
    tft.println("No files found.");
    return;
  }

  for (int i = 0; i < fileNum; i++) {
    tft.setCursor(5, (i * 20) + 2); // Adjust Y position for each file
    tft.print(i + 1); // File number (1-indexed)
    tft.setCursor(30, (i * 20) + 2); // X position for filename
    if (fileList[i] != nullptr) {
        tft.println(fileList[i]);
    } else {
        tft.println("Error: null filename");
    }
  }
}

#endif // SDFILES_H
