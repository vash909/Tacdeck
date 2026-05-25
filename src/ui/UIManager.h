#pragma once
#include <vector>
#include "Screen.h"

class Display;
class StatusBar;

// ================================================================
// UIManager — screen stack navigation
// ================================================================
class UIManager {
public:
    UIManager() = default;

    void begin(Display* disp, StatusBar* sb);

    // Push a new screen (takes ownership)
    void push(Screen* screen);

    // Pop current screen and return to previous
    void pop();

    // Replace entire stack with a single screen
    void replace(Screen* screen);

    // Called every loop
    void update();

    // Input dispatch
    void handleKey(char key);
    void handleTrackball(int dx, int dy, bool click);

    Screen* current() const {
        return _stack.empty() ? nullptr : _stack.back();
    }

    size_t depth() const { return _stack.size(); }

private:
    Display*             _disp = nullptr;
    StatusBar*           _sb   = nullptr;
    std::vector<Screen*> _stack;

    void _clearStack();
};
