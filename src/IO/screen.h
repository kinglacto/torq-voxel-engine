#pragma once

#include<glad/glad.h>
#include<GLFW/glfw3.h>

class Screen {
public:
    static void frameBufferSizeCallback(GLFWwindow*, int width, int height);
    Screen(int width, int height);
    bool init();
    void setParameters();
    void setCursorDisabled(bool disabled);
    
    void newFrame();

    bool shouldClose();
    void setShouldClose(bool shouldClose);
    [[nodiscard]] int getWidth() const;
    [[nodiscard]] int getHeight() const;
    [[nodiscard]] float getAspectRatio() const;

    GLFWwindow* window;

private:
    int width;
    int height;
};
