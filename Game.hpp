#pragma once

#include <random>
#include <vector>

struct GLFWwindow;

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class GameState {
    Running,
    Paused,
    GameOver
};

enum class TrafficType {
    Sedan,
    Sport,
    Truck,
    Van
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
};