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
    EventKind kind;
    uint16_t key;
    float mouseX;
    float mouseY;
    float scrollX;
    float scrollY;
    uint16_t windowWidth;
    uint16_t windowHeight;
    uint8_t mouseButton;
};

