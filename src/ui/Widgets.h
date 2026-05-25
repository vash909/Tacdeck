#pragma once
#include <LovyanGFX.hpp>
#include "Theme.h"
#include <functional>

// Key codes (subset; full set in hardware/Keyboard.h)
#ifndef KEY_BACKSPACE
#  define KEY_BACKSPACE 0x08
#endif
#ifndef KEY_ENTER
#  define KEY_ENTER     0x0D
#endif
#ifndef KEY_ESC
#  define KEY_ESC       0x1B
#endif

// ================================================================
// Lightweight widget toolkit built directly on LovyanGFX
// ================================================================

// ---- Header bar (below status bar) ----
inline void drawHeader(lgfx::LovyanGFX* gfx,
                        const char* title, uint16_t accentCol,
                        int y = 24, int h = 20) {
    gfx->fillRect(0, y, 320, h, COL_BG_HEADER);
    gfx->drawFastHLine(0, y + h - 1, 320, accentCol);
    gfx->setTextColor(accentCol, COL_BG_HEADER);
    gfx->setTextSize(FONT_SMALL);
    gfx->setCursor(6, y + 3);
    gfx->print(title);
}

// ---- Hint bar at bottom ----
inline void drawHints(lgfx::LovyanGFX* gfx,
                       const char* left, const char* mid = nullptr,
                       const char* right = nullptr) {
    constexpr int Y = 216;
    gfx->fillRect(0, Y, 320, 24, COL_BG_HEADER);
    gfx->drawFastHLine(0, Y, 320, COL_DIVIDER);
    gfx->setTextSize(FONT_TINY);

    if (left) {
        gfx->setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
        gfx->setCursor(4, Y + 8);
        gfx->print(left);
    }
    if (mid) {
        gfx->setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
        int tw = strlen(mid) * 6;
        gfx->setCursor((320 - tw) / 2, Y + 8);
        gfx->print(mid);
    }
    if (right) {
        gfx->setTextColor(COL_TEXT_DIM, COL_BG_HEADER);
        int tw = strlen(right) * 6;
        gfx->setCursor(320 - tw - 4, Y + 8);
        gfx->print(right);
    }
}

// ---- RSSI bar ----
inline void drawRSSIBar(lgfx::LovyanGFX* gfx,
                         int x, int y, int w, int h,
                         float rssi) {
    gfx->drawRect(x, y, w, h, COL_BORDER);
    int filled = rssiToWidth(rssi, w - 2);
    uint16_t col = rssiToColor(rssi);
    gfx->fillRect(x + 1, y + 1, filled, h - 2, col);
    gfx->fillRect(x + 1 + filled, y + 1, w - 2 - filled, h - 2, COL_BG);
}

// ---- Label + value pair ----
inline void drawKV(lgfx::LovyanGFX* gfx,
                    int x, int y, const char* key, const char* val,
                    uint16_t keyCol = COL_TEXT_DIM,
                    uint16_t valCol = COL_TEXT) {
    gfx->setTextSize(FONT_TINY);
    gfx->setTextColor(keyCol, COL_BG);
    gfx->setCursor(x, y);
    gfx->print(key);
    gfx->setTextColor(valCol, COL_BG);
    gfx->print(val);
}

// ---- Scrollable text log (last N lines) ----
struct TextLog {
    static constexpr int MAX_LINES = 12;
    static constexpr int LINE_LEN  = 54;

    char  lines[MAX_LINES][LINE_LEN];
    int   count = 0;
    int   scroll = 0;   // 0 = bottom

    void addLine(const char* text) {
        if (count < MAX_LINES) {
            strncpy(lines[count++], text, LINE_LEN - 1);
            lines[count - 1][LINE_LEN - 1] = '\0';
        } else {
            memmove(lines[0], lines[1], sizeof(lines) - LINE_LEN);
            strncpy(lines[MAX_LINES - 1], text, LINE_LEN - 1);
        }
    }

    void draw(lgfx::LovyanGFX* gfx, int x, int y, int w, int h,
              uint16_t textCol = COL_TEXT, uint16_t bg = COL_BG) {
        int lineH = 10;
        int visLines = h / lineH;
        int start = count > visLines ? count - visLines : 0;
        gfx->fillRect(x, y, w, h, bg);
        gfx->setTextSize(FONT_TINY);
        gfx->setTextColor(textCol, bg);
        for (int i = 0; i < visLines && (start + i) < count; i++) {
            gfx->setCursor(x + 2, y + i * lineH);
            gfx->print(lines[start + i]);
        }
    }
};

// ---- Input box ----
struct InputBox {
    char    buf[64];
    int     cursor = 0;
    int     maxLen = 32;
    bool    active = false;

    void clear()            { memset(buf, 0, sizeof(buf)); cursor = 0; }
    void input(char c) {
        if (c == KEY_BACKSPACE || c == 0x7F) {
            if (cursor > 0) { buf[--cursor] = '\0'; }
            return;
        }
        if (c >= 0x20 && c < 0x7F && cursor < maxLen - 1) {
            buf[cursor++] = c;
            buf[cursor]   = '\0';
        }
    }
    void draw(lgfx::LovyanGFX* gfx, int x, int y, int w,
              uint16_t accentCol = COL_CYAN) {
        gfx->fillRect(x, y, w, 14, COL_BG_INPUT);
        gfx->drawRect(x, y, w, 14, active ? accentCol : COL_BORDER);
        gfx->setTextSize(FONT_TINY);
        gfx->setTextColor(COL_TEXT, COL_BG_INPUT);
        gfx->setCursor(x + 3, y + 3);
        gfx->print(buf);
        // blinking cursor
        if (active && (millis() / 500) % 2 == 0) {
            int cx = x + 3 + cursor * 6;
            gfx->fillRect(cx, y + 2, 1, 10, accentCol);
        }
    }
};

// ---- Simple button ----
inline void drawButton(lgfx::LovyanGFX* gfx, int x, int y, int w, int h,
                        const char* label, bool selected,
                        uint16_t accentCol = COL_CYAN) {
    uint16_t bg  = selected ? accentCol : COL_BG_PANEL;
    uint16_t fg  = selected ? COL_BG    : accentCol;
    gfx->fillRoundRect(x, y, w, h, 3, bg);
    gfx->drawRoundRect(x, y, w, h, 3, accentCol);
    gfx->setTextSize(FONT_TINY);
    gfx->setTextColor(fg, bg);
    int tw = strlen(label) * 6;
    gfx->setCursor(x + (w - tw) / 2, y + (h - 8) / 2);
    gfx->print(label);
}

// ---- Progress bar ----
inline void drawProgressBar(lgfx::LovyanGFX* gfx,
                              int x, int y, int w, int h,
                              float pct,           // 0.0 - 1.0
                              uint16_t col = COL_GREEN) {
    gfx->drawRect(x, y, w, h, COL_BORDER);
    int filled = (int)(pct * (w - 2));
    if (filled < 0)    filled = 0;
    if (filled > w -2) filled = w - 2;
    gfx->fillRect(x + 1, y + 1, filled, h - 2, col);
    gfx->fillRect(x + 1 + filled, y + 1, w - 2 - filled, h - 2, COL_BG);
}

