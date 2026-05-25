#include "CWUtils.h"
#include <ctype.h>

// A-Z (indices 0-25), 0-9 (indices 26-35), punctuation (36+)
const char* const CWUtils::MORSE_TABLE[] = {
    ".-",   "-...", "-.-.", "-..",  ".",     "..-.",  "--.",   "....", // A-H
    "..",   ".---", "-.-",  ".-..", "--",    "-.",    "---",   ".--.", // I-P
    "--.-", ".-.",  "...",  "-",    "..-",   "...-",  ".--",   "-..-", // Q-X
    "-.--", "--..",                                                     // Y-Z
    "-----",".----","..---","...--","....-",".....","-....","--...","---..", "----.", // 0-9
    ".-.-.-",   // .
    "--..--",   // ,
    "..--..",   // ?
    "-.-.--",   // !
    "---...",   // :
    "-....-",   // -
    ".----.",   // '
    "-.--.",    // (
    "-.--.-",   // )
    ".-..-.",   // "
    ".-.-.--",  // +  (not standard, using AR)
    "-...-",    // =
    "-..-.",    // /
    "..--.",    // _
    ".-.-.",    // AR (end of message)
    "...-.-",   // SK (end of contact)
    "-.-.-",    // KA (start)
    ".-...",    // AS (wait)
};

const char* CWUtils::charToMorse(char c) {
    c = toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return MORSE_TABLE[c - 'A'];
    if (c >= '0' && c <= '9') return MORSE_TABLE[26 + (c - '0')];
    switch (c) {
        case '.': return MORSE_TABLE[36];
        case ',': return MORSE_TABLE[37];
        case '?': return MORSE_TABLE[38];
        case '!': return MORSE_TABLE[39];
        case ':': return MORSE_TABLE[40];
        case '-': return MORSE_TABLE[41];
        case '\'':return MORSE_TABLE[42];
        case '(': return MORSE_TABLE[43];
        case ')': return MORSE_TABLE[44];
        case '"': return MORSE_TABLE[45];
        case '+': return MORSE_TABLE[46];
        case '=': return MORSE_TABLE[47];
        case '/': return MORSE_TABLE[48];
        default:  return nullptr;
    }
}

size_t CWUtils::textToMorse(const char* text, char* out, size_t sz) {
    if (!text || !out || sz < 2) return 0;
    size_t written = 0;

    for (size_t i = 0; text[i] && written < sz - 1; i++) {
        char c = text[i];

        if (c == ' ') {
            if (written > 0 && out[written - 1] != '/') {
                out[written++] = '/';
            }
            continue;
        }

        const char* m = charToMorse(c);
        if (!m) continue;

        // Add letter separator before each letter (except first)
        if (written > 0 && out[written - 1] != '/') {
            if (written < sz - 1) out[written++] = ' ';
        }

        for (size_t j = 0; m[j] && written < sz - 1; j++) {
            out[written++] = m[j];
        }
    }

    out[written] = '\0';
    return written;
}

uint32_t CWUtils::wpmToDitMs(int wpm) {
    // PARIS standard: one word = 50 dits, so dit = 1200/wpm ms
    if (wpm <= 0) wpm = 1;
    return 1200 / wpm;
}
