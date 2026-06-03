#pragma once

#include <cstdint>

enum class EventKind : uint32_t {
    INVALID,
    MOUSE_MOVE,
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_SCROLL,
    MOUSE_ENTER,
    MOUSE_LEAVE,
    KEY_PRESS,
    KEY_RELEASE,
    WINDOW_RESIZE,
    MAX_ENUM
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

