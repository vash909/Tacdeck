#pragma once
#include <stddef.h>
#include <cstring>

// ================================================================
// Morse / CW utilities — text to morse string conversion
// ================================================================
class CWUtils {
public:
    // Convert text string to morse representation
    // Dots: '.'  Dashes: '-'  Letter space: ' '  Word space: '/'
    // Returns number of chars written (excl. null terminator)
    static size_t textToMorse(const char* text, char* out, size_t sz);

    // Compute dit length in ms from WPM
    static uint32_t wpmToDitMs(int wpm);

    // Character to morse pattern (null = not encodable)
    static const char* charToMorse(char c);

private:
    static const char* const MORSE_TABLE[];
    static constexpr int TABLE_SIZE = 54;  // A-Z + 0-9 + common punct
};
