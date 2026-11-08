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

    // бонус с дороги: 0 щит, 1 магнит, 2 нитро (как PowerType в Game.hpp)
    void playPower(int kind);

    // короткая фанфара: достижение, побег от полиции, задание
    void playAchievement();

    // свист воздуха при опасном проезде рядом с машиной
    void playNearMiss();

    // сирена полиции: 0 выключена, 1 самая громкая; вызывать каждый кадр
    void setSiren(float level);

    // скрип шин: 0 нет, 1 есть; вызывать каждый кадр
    void setSkid(float level);

    // громкости 0..1: общая, двигатель, эффекты (монеты, авария, сирена, шины), окружение (дождь, ветер, буст)
    void setVolumes(float master, float engine, float effects, float ambient);

private:
    AudioState* state_ = nullptr;
};