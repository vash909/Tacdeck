#pragma once
#include <stdint.h>

// ================================================================
// Base class for all application screens
// ================================================================
class Screen {
public:
    virtual ~Screen() = default;

    // Called once when the screen becomes active
    virtual void onEnter() {}

    // Called once when the screen is about to be replaced
    virtual void onExit()  {}

    // Called every frame — update state + draw
    virtual void update()  = 0;

    // Input handlers
    virtual void onKey(char key)                     {}
    virtual void onTrackball(int dx, int dy, bool click) {}

    // Screen identifier (for back-navigation)
    virtual const char* name() const = 0;

    // True if this screen wants to handle ESC itself
    // (false = UIManager will pop the screen on ESC)
    virtual bool handlesEsc() const { return false; }

    // Request full redraw on next update()
    void invalidate() { _dirty = true; }

protected:
    bool _dirty = true;   // force full redraw when true
};
