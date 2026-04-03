#include "eventHandler.h"
#include "Util/logger.h"

void EventHandler::Init(GLFWwindow *window)
{
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    mouseLocked = true;

    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, EventHandler::OnKeyPress);
    glfwSetCursorPosCallback(window, EventHandler::OnMouseMove);
    glfwSetCursorEnterCallback(window, EventHandler::OnMouseEnterOrLeave);
    glfwSetWindowSizeCallback(window, EventHandler::OnResize);
    glfwSetFramebufferSizeCallback(window, EventHandler::OnResize);
    glfwSetMouseButtonCallback(window, EventHandler::OnMousePress);
    glfwSetScrollCallback(window, EventHandler::OnMouseScroll);
}

void EventHandler::SetEventCallback(void ( *callback)(Event event, void *data), void *data)
{
    m_EventCallback = callback;
    m_CallbackData = data;
}

void EventHandler::RecordFrame()
{
    mousePosLastFrame = mousePos;
}

void EventHandler::OnKeyPress(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void) scancode; (void) mods;
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_UNKNOWN) {
        WARN("Unknown key pressed");
        return;
    }

    if (action == GLFW_PRESS) {
        handler->keysDown[key] = true;

        // Unlock mouse
        if (key == GLFW_KEY_ESCAPE && handler->mouseLocked) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            handler->mouseLocked = false;
        }

        handler->ForwardEvent((Event) {
            .kind = EVENT_KEY_PRESS,
            .key = static_cast<uint16_t>(key)
        });
    } else if (action == GLFW_RELEASE) {
        handler->keysDown[key] = false;
        handler->ForwardEvent((Event) {
            .kind = EVENT_KEY_RELEASE,
            .key = static_cast<uint16_t>(key)
        });
    }
}

void EventHandler::OnResize(GLFWwindow *window, int width, int height)
{
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    handler->ForwardEvent((Event) {
        .kind = EVENT_WINDOW_RESIZE,
        .windowWidth = static_cast<uint16_t>(width),
        .windowHeight = static_cast<uint16_t>(height),
    });
}

void EventHandler::OnMouseMove(GLFWwindow *window, double xPos, double yPos)
{
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    handler->mousePos = glm::vec2(xPos, yPos);
    handler->ForwardEvent((Event) {
        .kind = EVENT_MOUSE_MOVE,
        .mouseX = static_cast<float>(xPos),
        .mouseY = static_cast<float>(yPos)
    });
}

void EventHandler::OnMouseEnterOrLeave(GLFWwindow *window, int entered)
{
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    handler->ForwardEvent((Event) {
        .kind = (entered) ? EVENT_MOUSE_ENTER : EVENT_MOUSE_LEAVE
    });
}

void EventHandler::OnMousePress(GLFWwindow *window, int button, int action, int mods)
{
    (void) mods;
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    // Lock mouse
    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && !handler->mouseLocked) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        handler->mouseLocked = true;
    }

    handler->ForwardEvent((Event) {
        .kind = (action == GLFW_PRESS) ? EVENT_MOUSE_PRESS : EVENT_MOUSE_RELEASE,
        .mouseX = handler->mousePos.x,
        .mouseY = handler->mousePos.y,
        .mouseButton = static_cast<uint8_t>(button),
    });
}

void EventHandler::OnMouseScroll(GLFWwindow *window, double xOffset, double yOffset)
{
    EventHandler *handler = static_cast<EventHandler *>(glfwGetWindowUserPointer(window));

    handler->ForwardEvent((Event) {
        .kind = EVENT_MOUSE_SCROLL,
        .mouseX = handler->mousePos.x,
        .mouseY = handler->mousePos.y,
        .scrollX = static_cast<float>(xOffset),
        .scrollY = static_cast<float>(yOffset)
    });
}

void EventHandler::ForwardEvent(Event event)
{
    m_EventCallback(event, m_CallbackData);
}
