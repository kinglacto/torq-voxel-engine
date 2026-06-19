#include "mouse.h"

double mouse::x = 0;
double mouse::y = 0;

double mouse::lastX = 0;
double mouse::lastY = 0;
bool mouse::initialized = false;

double mouse::dx = 0;
double mouse::dy = 0;

double mouse::scrollDX = 0;
double mouse::scrollDY = 0;

std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouse::buttons{};
std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouse::buttonsChanged{};

void mouse::cursorPosCallBack(GLFWwindow* window, double _x, double _y) {
    (void) window;

    if (!initialized) {
        x = _x;
        y = _y;
        lastX = _x;
        lastY = _y;
        initialized = true;
        return;
    }

    x = _x;
    y = _y;

    dx += x - lastX;
    dy += lastY - y;
    lastX = x;
    lastY = y;
}

void mouse::mouseButtonCallBack(GLFWwindow* window, int button, int action, int mods) {
    (void) window;
    (void) mods;

    if (!isValidButton(button)) {
        return;
    }

    buttons[button] = action != GLFW_RELEASE;
    buttonsChanged[button] = action != GLFW_REPEAT;
}

void mouse::mouseWheelCallBack(GLFWwindow* window, double _dx, double _dy) {
    (void) window;

    scrollDX += _dx;
    scrollDY += _dy;
}

double mouse::getMouseX() {
    return x;
}

double mouse::getMouseY() {
    return y;
}

double mouse::getDX() {
    double _dx = dx;
    dx = 0;
    return _dx;
}

double mouse::getDY() {
    double _dy = dy;
    dy = 0;
    return _dy;
}

double mouse::getScrollDX() {
    double dx = scrollDX;
    scrollDX = 0;
    return dx;
}

double mouse::getScrollDY() {
    double dy = scrollDY;
    scrollDY = 0;
    return dy;
}

bool mouse::button(int button) {
    if (!isValidButton(button)) {
        return false;
    }

    return buttons[button];
}

bool mouse::buttonChanged(int button) {
    if (!isValidButton(button)) {
        return false;
    }

    bool ret = buttonsChanged[button];
    buttonsChanged[button] = false;
    return ret;
}

bool mouse::buttonWentUp(int button) {
    if (!isValidButton(button)) {
        return false;
    }

    return !buttons[button] && buttonChanged(button);
}
bool mouse::buttonWentDown(int button) {
    if (!isValidButton(button)) {
        return false;
    }

    return buttons[button] && buttonChanged(button);
}

void mouse::resetPosition(GLFWwindow* window) {
    if (!window) {
        return;
    }

    glfwGetCursorPos(window, &x, &y);
    lastX = x;
    lastY = y;
    initialized = true;
    resetDeltas();
}

void mouse::resetDeltas() {
    dx = 0;
    dy = 0;
    scrollDX = 0;
    scrollDY = 0;
}

bool mouse::isValidButton(const int button) {
    return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST;
}
