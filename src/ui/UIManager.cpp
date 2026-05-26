#include "UIManager.h"
#include "../hardware/Display.h"
#include "StatusBar.h"
#include "Screen.h"
#include <Arduino.h>

void UIManager::begin(Display* disp, StatusBar* sb) {
    _disp = disp;
    _sb   = sb;
}

void UIManager::push(Screen* screen) {
    if (!screen) return;
    if (!_stack.empty()) _stack.back()->onExit();
    _stack.push_back(screen);
    screen->onEnter();
    screen->invalidate();
}

void UIManager::pop() {
    if (_stack.size() <= 1) return;   // never pop the last screen
    Screen* top = _stack.back();
    top->onExit();
    delete top;
    _stack.pop_back();
    _stack.back()->onEnter();
    _stack.back()->invalidate();
}

void UIManager::replace(Screen* screen) {
    _clearStack();
    push(screen);
}

void UIManager::update() {
    // NOTE: status bar is updated at 1 Hz directly in main.cpp — do NOT call
    // _sb->update() here (was being called at 20 fps causing unnecessary
    // partial redraws and visible flicker in the time/RSSI regions).
    if (!_stack.empty()) _stack.back()->update();
}

void UIManager::handleKey(char key) {
    if (_stack.empty()) return;

    Screen* top = _stack.back();

    // Global ESC handling
    if (key == 0x1B && !top->handlesEsc()) {
        pop();
        return;
    }
    top->onKey(key);
}

void UIManager::handleTrackball(int dx, int dy, bool click) {
    if (!_stack.empty())
        _stack.back()->onTrackball(dx, dy, click);
}

void UIManager::_clearStack() {
    for (auto* s : _stack) {
        s->onExit();
        delete s;
    }
    _stack.clear();
}
