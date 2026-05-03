#include "eventManager.h"
#include "Util/logger.h"

void EventManager::Init(GLFWwindow *window)
{
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    mouseLocked = false;

    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, EventManager::OnKeyPress);
    glfwSetCursorPosCallback(window, EventManager::OnMouseMove);
    glfwSetCursorEnterCallback(window, EventManager::OnMouseEnterOrLeave);
    glfwSetWindowSizeCallback(window, EventManager::OnResize);
    glfwSetFramebufferSizeCallback(window, EventManager::OnResize);
    glfwSetMouseButtonCallback(window, EventManager::OnMousePress);
    glfwSetScrollCallback(window, EventManager::OnMouseScroll);

    mousePos = glm::vec2(0.0);
    mousePosLastFrame = glm::vec2(0.0);
}

void EventManager::SetEventCallback(void ( *callback)(Event event, void *data), void *data)
{
    m_EventCallback = callback;
    m_CallbackData = data;
}

void EventManager::Update()
{
    mousePosLastFrame = mousePos;
}

void EventManager::OnKeyPress(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void) scancode; (void) mods;
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_UNKNOWN) {
        WARN("Unknown key pressed");
        return;
    }

    if (action == GLFW_PRESS) {
        manager->keysDown[key] = true;

        // Unlock mouse
        if (key == GLFW_KEY_ESCAPE && manager->mouseLocked) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            manager->mouseLocked = false;
        }

        manager->ForwardEvent((Event) {
            .kind = EVENT_KEY_PRESS,
            .key = static_cast<uint16_t>(key)
        });
    } else if (action == GLFW_RELEASE) {
        manager->keysDown[key] = false;
        manager->ForwardEvent((Event) {
            .kind = EVENT_KEY_RELEASE,
            .key = static_cast<uint16_t>(key)
        });
    }
}

void EventManager::OnResize(GLFWwindow *window, int width, int height)
{
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    manager->ForwardEvent((Event) {
        .kind = EVENT_WINDOW_RESIZE,
        .windowWidth = static_cast<uint16_t>(width),
        .windowHeight = static_cast<uint16_t>(height),
    });
}

void EventManager::OnMouseMove(GLFWwindow *window, double xPos, double yPos)
{
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    float xFloat = static_cast<float>(xPos);
    float yFloat = static_cast<float>(yPos);
    manager->mousePos = glm::vec2(xFloat, yFloat);
    manager->ForwardEvent((Event) {
        .kind = EVENT_MOUSE_MOVE,
        .mouseX = xFloat,
        .mouseY = yFloat
    });
}

void EventManager::OnMouseEnterOrLeave(GLFWwindow *window, int entered)
{
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    manager->ForwardEvent((Event) {
        .kind = (entered) ? EVENT_MOUSE_ENTER : EVENT_MOUSE_LEAVE
    });
}

void EventManager::OnMousePress(GLFWwindow *window, int button, int action, int mods)
{
    (void) mods;
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    // Lock mouse
    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && !manager->mouseLocked) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        manager->mouseLocked = true;
    }

    manager->ForwardEvent((Event) {
        .kind = (action == GLFW_PRESS) ? EVENT_MOUSE_PRESS : EVENT_MOUSE_RELEASE,
        .mouseX = manager->mousePos.x,
        .mouseY = manager->mousePos.y,
        .mouseButton = static_cast<uint8_t>(button),
    });
}

void EventManager::OnMouseScroll(GLFWwindow *window, double xOffset, double yOffset)
{
    EventManager *manager = static_cast<EventManager *>(glfwGetWindowUserPointer(window));

    manager->ForwardEvent((Event) {
        .kind = EVENT_MOUSE_SCROLL,
        .mouseX = manager->mousePos.x,
        .mouseY = manager->mousePos.y,
        .scrollX = static_cast<float>(xOffset),
        .scrollY = static_cast<float>(yOffset)
    });
}

void EventManager::ForwardEvent(Event event)
{
    m_EventCallback(event, m_CallbackData);
}
