#pragma once

// Звук целиком синтезируется кодом, внешних аудиофайлов не нужно.
// Использует однофайловую библиотеку miniaudio (miniaudio.h рядом с исходниками).

struct AudioState;

class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Открывает звуковое устройство. Если не вышло, игра просто будет без звука.
    bool start();

    // Вызывать каждый кадр.
    // active = false на паузе и после аварии (звук плавно затихает).
    void update(float speedKmh, bool boosting, float rain, float tunnel, bool active);

    void playCrash();
    void playCoin();

private:
    AudioState* state_ = nullptr;
};