#pragma once

#include "Audio.hpp"

#include <random>
#include <vector>

struct GLFWwindow;

// Туннель стоит на трассе в «пройденных метрах»: период 6000 м,
// длина 700 м, первый примерно на отметке 3500 м.
constexpr float kTunnelPeriod = 6000.0f;
constexpr float kTunnelPhase = 3500.0f;
constexpr float kTunnelLength = 700.0f;

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class GameState {
    Running,
    Paused,
    GameOver,
    Garage      // меню улучшений (открывается клавишей G на паузе или после аварии)
};

constexpr int kUpgradeCount = 4;
constexpr int kMaxUpgradeLevel = 5;

enum class TrafficType {
    Sedan,
    Sport,
    Truck,
    Van
};

enum class ChaseState {
    Idle,      // погони нет
    Warning,   // полиция заметила вас, сейчас начнётся погоня
    Chasing    // идёт погоня
};

struct TrafficVehicle {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;

    float width = 1.5f;
    float length = 3.4f;
    float height = 1.1f;

    float relativeSpeed = 0.0f;
    float drift = 0.0f;

    Color color{};

    TrafficType type = TrafficType::Sedan;

    bool scored = false;
    bool nearMissAwarded = false;
};

struct CoinPickup {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;

    float phase = 0.0f;

    bool collected = false;
};

class Game {
public:
    Game();
    ~Game();

    void handleInput(GLFWwindow* window, float dt);

    void update(float dt);

    void reset();

    GameState state() const {
        return state_;
    }

    float playerX() const {
        return playerX_;
    }

    float playerZ() const {
        return playerZ_;
    }

    float playerYaw() const {
        return playerYaw_;
    }

    float speed() const {
        return speed_;
    }

    float speedKmh() const {
        return speed_ * 7.2f;
    }

    float maxSpeed() const {
        return maxSpeed_;
    }

    float roadOffset() const {
        return roadOffset_;
    }

    float worldTime() const {
        return worldTime_;
    }

    // пройденный путь в метрах
    float distance() const {
        return distance_;
    }

    // 0 = сухо, 1 = сильный дождь; в туннеле всегда 0
    float rain() const;

    // 0..1, насколько камера внутри туннеля
    float tunnelInside() const;

    int score() const {
        return score_;
    }

    int highScore() const {
        return highScore_;
    }

    int coins() const {
        return coins_;
    }

    int combo() const {
        return combo_;
    }

    float health() const {
        return health_;
    }

    float boost() const {
        return boost_;
    }

    bool boosting() const {
        return boosting_;
    }

    int targetLane() const {
        return targetLane_;
    }

    const std::vector<TrafficVehicle>& traffic() const {
        return traffic_;
    }

    const std::vector<CoinPickup>& pickups() const {
        return pickups_;
    }

    // ---- Полицейская погоня ----
    ChaseState chaseState() const {
        return chaseState_;
    }

    // расстояние до полиции сзади, метры (больше 140 = оторвались)
    float policeGap() const {
        return policeGap_;
    }

    float policeX() const {
        return policeX_;
    }

    // true, если игра закончилась тем, что вас поймали
    bool busted() const {
        return busted_;
    }

    // сколько ещё секунд показывать надпись ESCAPED
    float escapeTimer() const {
        return escapeTimer_;
    }

    // ---- Гараж ----
    int bank() const {
        return bank_;
    }

    int upgradeLevel(int id) const;

    // цена следующего уровня, -1 если уже максимум
    int upgradeCost(int id) const;

    static const char* upgradeName(int id);

    static const char* upgradeInfo(int id);

    static float laneCenter(int lane);

private:
    bool pressedOnce(
        GLFWwindow* window,
        int key,
        bool& previous
    );

    void changeLane(int direction);

    void spawnTraffic();

    void spawnCoinLine();

    void updateTraffic(float dt);

    void updatePickups(float dt);

    void checkCollisions();

    void updateDifficulty(float dt);

    void updateChase(float dt);

    bool buyUpgrade(int id);

    void loadGarage();

    void saveGarage() const;

    void awardNearMiss(TrafficVehicle& vehicle);

    void updateHighScore();

    int loadHighScore() const;

    void saveHighScore(int value) const;

private:
    GameState state_ =
        GameState::Running;

    float playerX_ = 0.0f;

    float playerZ_ = 4.0f;

    float playerYaw_ = 0.0f;

    float lateralVelocity_ = 0.0f;

    int targetLane_ = 1;

    float speed_ = 28.0f;

    float baseCruiseSpeed_ = 28.0f;

    float maxSpeed_ = 58.0f;

    float throttle_ = 0.0f;

    float brake_ = 0.0f;

    float boost_ = 100.0f;

    bool boosting_ = false;

    float health_ = 100.0f;

    float invulnerabilityTimer_ = 0.0f;

    float worldTime_ = 0.0f;

    float roadOffset_ = 0.0f;

    float distance_ = 0.0f;

    float scoreValue_ = 0.0f;

    int score_ = 0;

    int highScore_ = 0;

    int coins_ = 0;

    int combo_ = 0;

    float comboTimer_ = 0.0f;

    float trafficTimer_ = 0.0f;

    float trafficInterval_ = 1.15f;

    float coinTimer_ = 0.0f;

    float coinInterval_ = 4.0f;

    float difficulty_ = 1.0f;

    ChaseState chaseState_ = ChaseState::Idle;

    float chaseTimer_ = 0.0f;

    float policeGap_ = 0.0f;

    float policeX_ = 0.0f;

    float escapeTimer_ = 0.0f;

    bool busted_ = false;

    // накопленные монеты и уровни улучшений: 0 двигатель, 1 шины, 2 броня, 3 магнит
    int bank_ = 0;

    int upgrades_[kUpgradeCount] = { 0, 0, 0, 0 };

    GameState garageReturn_ = GameState::Paused;

    bool garageWasDown_ = false;

    bool upgradeKeyWasDown_[kUpgradeCount] = { false, false, false, false };

    std::vector<TrafficVehicle>
        traffic_;

    std::vector<CoinPickup>
        pickups_;

    bool leftWasDown_ = false;

    bool rightWasDown_ = false;

    bool aWasDown_ = false;

    bool dWasDown_ = false;

    bool pauseWasDown_ = false;

    bool restartWasDown_ = false;

    std::mt19937 rng_;

    std::uniform_int_distribution<int>
        laneDist_;

    std::uniform_int_distribution<int>
        colorDist_;

    std::uniform_int_distribution<int>
        typeDist_;

    Audio audio_;
};