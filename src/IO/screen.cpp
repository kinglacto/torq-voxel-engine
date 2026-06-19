#include "screen.h"
#include "keyboard.h"
#include "mouse.h"

void Screen::frameBufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);

    auto* screen = static_cast<Screen*>(glfwGetWindowUserPointer(window));
    if (!screen) {
        return;
    }

    screen->width = width;
    screen->height = height;
}

Screen::Screen(int width, int height): window(nullptr), width(width), height(height) {
};

bool Screen::init() {
    window = glfwCreateWindow(width, height, "OpenGL", nullptr, nullptr);
    if (!window) {
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwGetFramebufferSize(window, &width, &height);
    return true;
}

void Screen::setParameters() {
    glViewport(0, 0, width, height);
    glfwSetFramebufferSizeCallback(window, frameBufferSizeCallback);
    glfwSetKeyCallback(window, keyboard::keyCallback);
    glfwSetCursorPosCallback(window, mouse::cursorPosCallBack);
    glfwSetMouseButtonCallback(window, mouse::mouseButtonCallBack);
    glfwSetScrollCallback(window, mouse::mouseWheelCallBack);
    setCursorDisabled(false);
    mouse::resetPosition(window);
}

void Screen::setCursorDisabled(bool disabled) {
    glfwSetInputMode(window, GLFW_CURSOR, disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    if (disabled && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    } else if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
}

void Screen::newFrame() {
    glfwSwapBuffers(window);
    glfwPollEvents();
}

bool Screen::shouldClose() {
    return glfwWindowShouldClose(window);
}

void Screen::setShouldClose(const bool shouldClose) {
    glfwSetWindowShouldClose(window, shouldClose);
}

int Screen::getWidth() const {
    return width;
}

int Screen::getHeight() const {
    return height;
}

float Screen::getAspectRatio() const {
    if (height == 0) {
        return 1.0f;
    }

    return static_cast<float>(width) / static_cast<float>(height);
}
