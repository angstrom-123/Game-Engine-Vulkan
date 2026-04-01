#pragma once

#include "glm/ext/vector_float2.hpp"
#include <GLFW/glfw3.h>

#include "event.h"

class EventHandler {
public:
    void Init(GLFWwindow *window);
    void SetEventCallback(void ( *callback)(Event event, void *data), void *data);
    void RecordFrame();
    
public:
    bool keysDown[GLFW_KEY_LAST];
    glm::vec2 mousePos;
    glm::vec2 mousePosLastFrame;
    bool mouseLocked;

private:
    static void OnKeyPress(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void OnResize(GLFWwindow *window, int width, int height);
    static void OnMouseMove(GLFWwindow *window, double xPos, double yPos);
    static void OnMouseEnterOrLeave(GLFWwindow *window, int entered);
    static void OnMousePress(GLFWwindow *window, int button, int action, int mods);
    static void OnMouseScroll(GLFWwindow *window, double xOffset, double yOffset);
    void ForwardEvent(Event event);

private:
    void ( *m_EventCallback)(Event event, void *data);
    void *m_CallbackData;
};
