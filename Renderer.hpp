#pragma once

#include "Game.hpp"

class Renderer {
public:
    bool initialize();

    void resize(
        int width,
        int height
    );

    void render(
        const Game& game
    );

private:
    void renderWorld(
        const Game& game
    );

    void renderHud(
        const Game& game
    );

    int width_ =
        1280;

    int height_ =
        720;
};