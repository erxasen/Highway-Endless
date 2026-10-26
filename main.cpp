#include <GL/glew.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>

#include "Game.hpp"
#include "Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace {

    constexpr int kInitialWidth = 1280;
    constexpr int kInitialHeight = 720;

    Renderer* gRenderer = nullptr;

    void framebufferSizeCallback(GLFWwindow*, int width, int height) {
        if (gRenderer) {
            gRenderer->resize(width, height);
        }
    }

}

int main() {
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(
        kInitialWidth,
        kInitialHeight,
        "Endless Highway - Arcade Edition",
        nullptr,
        nullptr
    );

    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const GLenum glewStatus = glewInit();

    if (glewStatus != GLEW_OK) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glEnable(GL_MULTISAMPLE);

    Game game;
    Renderer renderer;

    gRenderer = &renderer;

    renderer.initialize();
    renderer.resize(kInitialWidth, kInitialHeight);

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    auto previous = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        const auto now = std::chrono::high_resolution_clock::now();

        const float dt = std::min(
            0.05f,
            std::chrono::duration<float>(now - previous).count()
        );

        previous = now;

        glfwPollEvents();

        game.handleInput(window, dt);
        game.update(dt);

        renderer.render(game);

        glfwSwapBuffers(window);
    }

    gRenderer = nullptr;

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}