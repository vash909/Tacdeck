#include "Storage.h"
#include <Arduino.h>

Preferences Storage::_prefs;
bool        Storage::_open = false;

bool Storage::begin() {
    _open = _prefs.begin(NVS_NAMESPACE, false);
    if (!_open) Serial.println("[NVS] Failed to open namespace");
    else        Serial.println("[NVS] Ready");
    return _open;
}

void Storage::end() {
    if (_open) { _prefs.end(); _open = false; }
}

bool Storage::getString(const char* key, char* out, size_t sz, const char* def) {
    if (!_open || !key || !out) return false;
    String s = _prefs.getString(key, def ? def : "");
    strncpy(out, s.c_str(), sz - 1);
    out[sz - 1] = '\0';
    return true;
}

bool Storage::setString(const char* key, const char* value) {
    if (!_open || !key || !value) return false;
    return _prefs.putString(key, value) > 0;
}

int32_t Storage::getInt(const char* key, int32_t def) {
    if (!_open || !key) return def;
    return _prefs.getInt(key, def);
}

bool Storage::setInt(const char* key, int32_t value) {
    if (!_open || !key) return false;
    return _prefs.putInt(key, value) == sizeof(int32_t);
}

float Storage::getFloat(const char* key, float def) {
    if (!_open || !key) return def;
    return _prefs.getFloat(key, def);
}

bool Storage::setFloat(const char* key, float value) {
    if (!_open || !key) return false;
    return _prefs.putFloat(key, value) == sizeof(float);
}

bool Storage::getBool(const char* key, bool def) {
    if (!_open || !key) return def;
    return _prefs.getBool(key, def);
}

bool Storage::setBool(const char* key, bool value) {
    if (!_open || !key) return false;
    return _prefs.putBool(key, value) == sizeof(bool);
}

bool Storage::clear() {
    if (!_open) return false;
    return _prefs.clear();
}
