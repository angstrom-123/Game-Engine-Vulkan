#pragma once

#include <cstdint>

enum EventKind {
    EVENT_INVALID,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_PRESS,
    EVENT_MOUSE_RELEASE,
    EVENT_MOUSE_SCROLL,
    EVENT_MOUSE_ENTER,
    EVENT_MOUSE_LEAVE,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_WINDOW_RESIZE,
    EVENT_MAX_ENUM
};

struct Event {
    EventKind kind;          // Valid for all event types
    uint16_t key;            // Valid for key press and key release events
    float mouseX;            // Valid for mouse move, mouse press, and mouse scroll events
    float mouseY;            // ^^^
    float scrollX;           // Valid for mouse scroll events
    float scrollY;           // ^^^
    uint16_t windowWidth;    // Valid for window resize events
    uint16_t windowHeight;   // ^^^
    uint8_t mouseButton;     // Valid for mouse press events
};

