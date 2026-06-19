#include "keyboard.h"

std::array<bool, GLFW_KEY_LAST + 1> keyboard::keys{};
std::array<bool, GLFW_KEY_LAST + 1> keyboard::keysChanged{};

void keyboard::keyCallback(GLFWwindow* window, const int key, int scancode, const int action, int mods) {
    (void) window;
    (void) scancode;
    (void) mods;

    if (!isValidKey(key)) {
        return;
    }

    keys[key] = action != GLFW_RELEASE;
    keysChanged[key] = action != GLFW_REPEAT;
}

bool keyboard::key(const int key) {
    if (!isValidKey(key)) {
        return false;
    }

    return keys[key];
}

bool keyboard::keyChanged(const int key) {
    if (!isValidKey(key)) {
        return false;
    }

	const bool ret = keysChanged[key];
    keysChanged[key] = false;
    return ret;
}

bool keyboard::keyWentUp(const int key) {
    if (!isValidKey(key)) {
        return false;
    }

    return !keys[key] && keyChanged(key);
}

bool keyboard::keyWentDown(const int key) {
    if (!isValidKey(key)) {
        return false;
    }

    return keys[key] && keyChanged(key);
}

bool keyboard::isValidKey(const int key) {
    return key >= 0 && key <= GLFW_KEY_LAST;
}
