#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <cstring>
#include "config.h"

// ================================================================
// NVS-backed persistent storage (ESP32 Preferences)
// ================================================================
class Storage {
public:
    static bool begin();
    static void end();

    static bool   getString(const char* key, char* out, size_t sz,
                             const char* def = "");
    static bool   setString(const char* key, const char* value);

    static int32_t getInt(const char* key, int32_t def = 0);
    static bool    setInt(const char* key, int32_t value);

    static float   getFloat(const char* key, float def = 0.f);
    static bool    setFloat(const char* key, float value);

    static bool    getBool(const char* key, bool def = false);
    static bool    setBool(const char* key, bool value);

    static bool    clear();

private:
    static Preferences _prefs;
    static bool        _open;
};
