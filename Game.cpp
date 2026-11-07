#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>

#include "Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>

namespace {

    constexpr float kLaneWidth = 3.35f;

    // 72 делится на шаги 12, 24 и 36 (разметка, деревья, фонари),
    // поэтому сброс roadOffset_ не вызывает скачков.
    constexpr float kRoadMarkerSpacing = 72.0f;

    constexpr float kPlayerWidth = 1.55f;

    constexpr float kPlayerLength = 3.35f;

    constexpr float kSpawnZ = -150.0f;

    constexpr float kRemoveZ = 24.0f;

    // Цена улучшения: из уровня N в уровень N+1 стоит kUpgradeCosts[N] монет
    constexpr int kUpgradeCosts[kMaxUpgradeLevel] = { 10, 20, 35, 55, 80 };

    // Полицейская погоня (метры позади игрока)
    constexpr float kFirstChaseDelay = 35.0f;   // через сколько секунд первая погоня
    constexpr float kChaseStartGap = 70.0f;     // с какого расстояния начинает полиция
    constexpr float kChaseEscapeGap = 140.0f;   // оторвались, если расстояние больше
    constexpr float kPoliceSpeedBonus = 6.0f;   // насколько полиция быстрее обычного хода
    constexpr float kEscapeBonus = 1500.0f;     // очки за побег

    const std::array<Color, 9> kTrafficColors = { {
        {0.92f, 0.13f, 0.14f, 1.0f},
        {0.95f, 0.56f, 0.10f, 1.0f},
        {0.10f, 0.48f, 0.94f, 1.0f},
        {0.14f, 0.72f, 0.30f, 1.0f},
        {0.88f, 0.88f, 0.92f, 1.0f},
        {0.34f, 0.20f, 0.72f, 1.0f},
        {0.10f, 0.10f, 0.12f, 1.0f},
        {0.18f, 0.72f, 0.75f, 1.0f},
        {0.78f, 0.12f, 0.52f, 1.0f}
    } };

    float approach(float current, float target, float speed, float dt) {
        const float t = std::clamp(speed * dt, 0.0f, 1.0f);
        return current + (target - current) * t;
    }

    bool overlaps(float aCenter, float aSize, float bCenter, float bSize) {
        return std::abs(aCenter - bCenter) < (aSize + bSize) * 0.5f;
    }

    // Единственная формула погоды: её использует и Game (управление),
    // и Renderer (через Game::rain()).
    float weatherRain(float t) {
        const float v = std::sin(t * 0.021f - 1.0f);
        return std::clamp((v - 0.2f) / 0.5f, 0.0f, 1.0f);
    }

}

Game::Game()
    :
    highScore_(loadHighScore()),
    rng_(static_cast<unsigned int>(std::time(nullptr))),
    laneDist_(0, 2),
    colorDist_(0, static_cast<int>(kTrafficColors.size() - 1)),
    typeDist_(0, 3)
{
    loadGarage();
    audio_.start();
    reset();
}

Game::~Game()
{
    saveHighScore(highScore_);
    saveGarage();
}

int Game::upgradeLevel(int id) const
{
    if (id < 0 || id >= kUpgradeCount)
    {
        return 0;
    }

    return upgrades_[id];
}

int Game::upgradeCost(int id) const
{
    if (id < 0 || id >= kUpgradeCount)
    {
        return -1;
    }

    const int level = upgrades_[id];

    if (level >= kMaxUpgradeLevel)
    {
        return -1;
    }

    return kUpgradeCosts[level];
}

const char* Game::upgradeName(int id)
{
    switch (id)
    {
    case 0: return "ENGINE";
    case 1: return "TIRES";
    case 2: return "ARMOR";
    case 3: return "MAGNET";
    default: return "";
    }
}

const char* Game::upgradeInfo(int id)
{
    switch (id)
    {
    case 0: return "SPEED AND ACCELERATION";
    case 1: return "GRIP IN RAIN AND LANES";
    case 2: return "LESS CRASH DAMAGE";
    case 3: return "COIN PICKUP RANGE";
    default: return "";
    }
}

bool Game::buyUpgrade(int id)
{
    const int cost = upgradeCost(id);

    if (cost < 0 || bank_ < cost)
    {
        return false;
    }

    bank_ -= cost;
    ++upgrades_[id];

    saveGarage();

    return true;
}

void Game::loadGarage()
{
    std::ifstream file("garage.txt");

    int bank = 0;
    int levels[kUpgradeCount] = { 0, 0, 0, 0 };

    if (file >> bank)
    {
        for (int i = 0; i < kUpgradeCount; ++i)
        {
            file >> levels[i];
        }
    }

    bank_ = std::max(0, bank);

    for (int i = 0; i < kUpgradeCount; ++i)
    {
        upgrades_[i] = std::clamp(levels[i], 0, kMaxUpgradeLevel);
    }
}

void Game::saveGarage() const
{
    std::ofstream file("garage.txt", std::ios::trunc);

    file << bank_;

    for (int i = 0; i < kUpgradeCount; ++i)
    {
        file << ' ' << upgrades_[i];
    }
}

float Game::tunnelInside() const
{
    const float k = std::floor((distance_ - kTunnelPhase) / kTunnelPeriod);
    const float a = k * kTunnelPeriod + kTunnelPhase;
    const float b = a + kTunnelLength;

    if (distance_ < a || distance_ > b)
    {
        return 0.0f;
    }

    return std::clamp(std::min(distance_ - a, b - distance_) / 20.0f, 0.0f, 1.0f);
}

float Game::rain() const
{
    return weatherRain(worldTime_) * (1.0f - tunnelInside());
}

void Game::reset()
{
    state_ = GameState::Running;

    playerX_ = laneCenter(1);
    playerZ_ = 4.0f;
    playerYaw_ = 0.0f;
    lateralVelocity_ = 0.0f;
    targetLane_ = 1;

    speed_ = 28.0f;
    throttle_ = 0.0f;
    brake_ = 0.0f;

    boost_ = 100.0f;
    boosting_ = false;

    health_ = 100.0f;
    invulnerabilityTimer_ = 0.0f;

    worldTime_ = 0.0f;
    roadOffset_ = 0.0f;
    distance_ = 0.0f;

    scoreValue_ = 0.0f;
    score_ = 0;
    coins_ = 0;
    combo_ = 0;
    comboTimer_ = 0.0f;

    trafficTimer_ = 0.35f;
    trafficInterval_ = 1.15f;
    coinTimer_ = 1.2f;
    coinInterval_ = 4.0f;
    difficulty_ = 1.0f;

    traffic_.clear();
    pickups_.clear();

    chaseState_ = ChaseState::Idle;
    chaseTimer_ = kFirstChaseDelay;
    policeGap_ = 0.0f;
    policeX_ = playerX_;
    escapeTimer_ = 0.0f;
    busted_ = false;
}

float Game::laneCenter(int lane)
{
    return (static_cast<float>(lane) - 1.0f) * kLaneWidth;
}

bool Game::pressedOnce(GLFWwindow* window, int key, bool& previous)
{
    const bool down = glfwGetKey(window, key) == GLFW_PRESS;
    const bool result = down && !previous;
    previous = down;
    return result;
}

void Game::handleInput(GLFWwindow* window, float dt)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (pressedOnce(window, GLFW_KEY_P, pauseWasDown_))
    {
        if (state_ == GameState::Running)
        {
            state_ = GameState::Paused;
        }
        else if (state_ == GameState::Paused)
        {
            state_ = GameState::Running;
        }
    }

    if (pressedOnce(window, GLFW_KEY_R, restartWasDown_))
    {
        reset();
    }

    // гараж: G открывает на паузе и после аварии, повторно закрывает
    if (pressedOnce(window, GLFW_KEY_G, garageWasDown_))
    {
        if (state_ == GameState::Paused || state_ == GameState::GameOver)
        {
            garageReturn_ = state_;
            state_ = GameState::Garage;
        }
        else if (state_ == GameState::Garage)
        {
            state_ = garageReturn_;
        }
    }

    // клавиши 1-4 покупают улучшения (состояние клавиш отслеживаем всегда)
    const int upgradeKeys[kUpgradeCount] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4
    };

    for (int i = 0; i < kUpgradeCount; ++i)
    {
        const bool pressed = pressedOnce(window, upgradeKeys[i], upgradeKeyWasDown_[i]);

        if (pressed && state_ == GameState::Garage)
        {
            buyUpgrade(i);
        }
    }

    if (state_ != GameState::Running)
    {
        return;
    }

    const bool leftPressed =
        pressedOnce(window, GLFW_KEY_LEFT, leftWasDown_) ||
        pressedOnce(window, GLFW_KEY_A, aWasDown_);

    const bool rightPressed =
        pressedOnce(window, GLFW_KEY_RIGHT, rightWasDown_) ||
        pressedOnce(window, GLFW_KEY_D, dWasDown_);

    if (leftPressed && !rightPressed)
    {
        changeLane(-1);
    }

    if (rightPressed && !leftPressed)
    {
        changeLane(1);
    }

    throttle_ =
        (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        ? 1.0f
        : 0.0f;

    brake_ =
        (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        ? 1.0f
        : 0.0f;

    const bool boostPressed =
        glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    boosting_ =
        boostPressed &&
        boost_ > 0.5f &&
        speed_ > 20.0f;

    if (boosting_)
    {
        boost_ = std::max(0.0f, boost_ - 31.0f * dt);
    }
    else
    {
        boost_ = std::min(100.0f, boost_ + 9.0f * dt);
    }
}

void Game::changeLane(int direction)
{
    targetLane_ = std::clamp(targetLane_ + direction, 0, 2);
}

void Game::update(float dt)
{
    if (state_ != GameState::Running)
    {
        // пауза или авария: звук плавно затихает
        audio_.update(speedKmh(), false, 0.0f, 0.0f, false);
        return;
    }

    // защита от скачков: после подвисания окна машины не должны
    // пролетать сквозь игрока
    dt = std::min(dt, 0.05f);

    worldTime_ += dt;

    // 0 = сухо, 1 = сильный дождь (скользкая дорога); в туннеле сухо
    const float rainLevel = rain();

    if (invulnerabilityTimer_ > 0.0f)
    {
        invulnerabilityTimer_ -= dt;
    }

    if (comboTimer_ > 0.0f)
    {
        comboTimer_ -= dt;

        if (comboTimer_ <= 0.0f)
        {
            combo_ = 0;
        }
    }

    updateDifficulty(dt);

    const float targetCruise = baseCruiseSpeed_ + difficulty_ * 2.1f;

    const float boostExtra = boosting_ ? 18.0f : 0.0f;

    // улучшения: 0 двигатель, 1 шины
    const float engineLevel = static_cast<float>(upgrades_[0]);
    const float tiresLevel = static_cast<float>(upgrades_[1]);

    // хорошие шины уменьшают потерю сцепления на мокрой дороге
    const float wet = rainLevel * (1.0f - 0.15f * tiresLevel);

    float targetSpeed = targetCruise + throttle_ * (12.0f + engineLevel) + boostExtra;

    // на мокрой дороге тормоза слабее
    targetSpeed -= brake_ * 22.0f * (1.0f - 0.4f * wet);

    const float currentMaxSpeed =
        maxSpeed_ + (boosting_ ? 15.0f : 0.0f) + 1.5f * engineLevel;

    targetSpeed = std::clamp(targetSpeed, 14.0f, currentMaxSpeed);

    const float response =
        targetSpeed > speed_
        ? 2.2f * (1.0f + 0.08f * engineLevel) * (1.0f - 0.15f * wet)
        : 4.8f * (1.0f - 0.40f * wet);

    speed_ = approach(speed_, targetSpeed, response, dt);

    const float targetX = laneCenter(targetLane_);

    const float oldX = playerX_;

    // на мокрой дороге машина медленнее перестраивается
    playerX_ = approach(playerX_, targetX, 8.5f * (1.0f + 0.06f * tiresLevel) * (1.0f - 0.45f * wet), dt);

    lateralVelocity_ = (playerX_ - oldX) / std::max(dt, 0.0001f);

    playerYaw_ = approach(
        playerYaw_,
        std::clamp(-lateralVelocity_ * 1.35f * (1.0f + 0.8f * wet), -12.0f, 12.0f),
        8.0f,
        dt
    );

    roadOffset_ += speed_ * dt;

    distance_ += speed_ * dt;

    audio_.update(speedKmh(), boosting_, rainLevel, tunnelInside(), true);

    while (roadOffset_ >= kRoadMarkerSpacing)
    {
        roadOffset_ -= kRoadMarkerSpacing;
    }

    scoreValue_ +=
        speed_ * dt *
        (7.2f + static_cast<float>(combo_) * 0.55f);

    score_ = static_cast<int>(scoreValue_) + coins_ * 120;

    updateHighScore();

    trafficTimer_ += dt;

    if (trafficTimer_ >= trafficInterval_)
    {
        spawnTraffic();
        trafficTimer_ = 0.0f;
    }

    coinTimer_ += dt;

    if (coinTimer_ >= coinInterval_)
    {
        spawnCoinLine();
        coinTimer_ = 0.0f;
    }

    updateTraffic(dt);

    updatePickups(dt);

    checkCollisions();

    if (state_ == GameState::Running)
    {
        updateChase(dt);
    }
}

void Game::updateChase(float dt)
{
    if (escapeTimer_ > 0.0f)
    {
        escapeTimer_ -= dt;
    }

    if (chaseState_ != ChaseState::Idle)
    {
        // полицейская машина держится в вашей полосе
        policeX_ = approach(policeX_, playerX_, 1.5f, dt);
    }

    switch (chaseState_)
    {
    case ChaseState::Idle:
        chaseTimer_ -= dt;

        if (chaseTimer_ <= 0.0f)
        {
            chaseState_ = ChaseState::Warning;
            chaseTimer_ = 2.5f;
            policeGap_ = kChaseStartGap;
            policeX_ = playerX_;
        }
        break;

    case ChaseState::Warning:
        chaseTimer_ -= dt;

        if (chaseTimer_ <= 0.0f)
        {
            chaseState_ = ChaseState::Chasing;
        }
        break;

    case ChaseState::Chasing:
    {
        // полиция едет чуть быстрее обычного хода: без газа (W) и после
        // аварий расстояние сокращается, с газом и бустом растёт
        const float cruise = baseCruiseSpeed_ + difficulty_ * 2.1f;
        const float policeSpeed = std::min(cruise + kPoliceSpeedBonus, maxSpeed_ - 4.0f);

        policeGap_ += (speed_ - policeSpeed) * dt;

        // за погоню идут дополнительные очки
        scoreValue_ += speed_ * dt * 3.6f;

        if (policeGap_ >= kChaseEscapeGap)
        {
            // оторвались
            chaseState_ = ChaseState::Idle;

            std::uniform_real_distribution<float> delayDist(45.0f, 75.0f);
            chaseTimer_ = delayDist(rng_);

            escapeTimer_ = 3.0f;
            scoreValue_ += kEscapeBonus;
            bank_ += 10;
            boost_ = std::min(100.0f, boost_ + 40.0f);
        }
        else if (policeGap_ <= 0.0f)
        {
            // поймали
            busted_ = true;
            health_ = 0.0f;
            state_ = GameState::GameOver;

            audio_.playCrash();

            updateHighScore();

            saveHighScore(highScore_);

            saveGarage();
        }
        break;
    }
    }
}

void Game::updateDifficulty(float)
{
    difficulty_ = std::min(8.0f, 1.0f + worldTime_ / 32.0f);

    trafficInterval_ = std::max(0.42f, 1.18f - difficulty_ * 0.085f);

    coinInterval_ = std::max(2.5f, 4.3f - difficulty_ * 0.12f);
}

void Game::spawnTraffic()
{
    int lane = laneDist_(rng_);

    for (int attempt = 0; attempt < 6; ++attempt)
    {
        bool blocked = false;

        for (const auto& vehicle : traffic_)
        {
            if (vehicle.lane == lane && vehicle.z < -105.0f)
            {
                blocked = true;
                break;
            }
        }

        if (!blocked)
        {
            break;
        }

        lane = (lane + 1) % 3;
    }

    TrafficVehicle vehicle;

    vehicle.lane = lane;
    vehicle.x = laneCenter(lane);
    vehicle.z = kSpawnZ;

    vehicle.color = kTrafficColors[colorDist_(rng_)];

    const int type = typeDist_(rng_);

    vehicle.type = static_cast<TrafficType>(type);

    std::uniform_real_distribution<float> speedOffsetDist(-4.0f, 8.0f);

    vehicle.relativeSpeed = speedOffsetDist(rng_);

    switch (vehicle.type)
    {
    case TrafficType::Sedan:
        vehicle.width = 1.48f;
        vehicle.length = 3.35f;
        vehicle.height = 1.05f;
        break;

    case TrafficType::Sport:
        vehicle.width = 1.62f;
        vehicle.length = 3.7f;
        vehicle.height = 0.88f;
        vehicle.relativeSpeed += 4.0f;
        break;

    case TrafficType::Truck:
        vehicle.width = 2.2f;
        vehicle.length = 6.1f;
        vehicle.height = 2.25f;
        vehicle.relativeSpeed -= 4.0f;
        break;

    case TrafficType::Van:
        vehicle.width = 1.9f;
        vehicle.length = 4.6f;
        vehicle.height = 1.8f;
        vehicle.relativeSpeed -= 1.0f;
        break;
    }

    std::uniform_real_distribution<float> driftDist(-0.08f, 0.08f);

    vehicle.drift = driftDist(rng_);

    traffic_.push_back(vehicle);
}

void Game::spawnCoinLine()
{
    int lane = laneDist_(rng_);

    std::uniform_int_distribution<int> countDist(3, 6);

    const int count = countDist(rng_);

    for (int i = 0; i < count; ++i)
    {
        CoinPickup coin;

        coin.lane = lane;
        coin.x = laneCenter(lane);
        coin.z = -85.0f - static_cast<float>(i) * 8.0f;
        coin.phase = static_cast<float>(i) * 0.7f;

        pickups_.push_back(coin);
    }
}

void Game::updateTraffic(float dt)
{
    for (auto& vehicle : traffic_)
    {
        vehicle.z += (speed_ + vehicle.relativeSpeed) * dt;

        const float desiredX = laneCenter(vehicle.lane);

        vehicle.x = approach(vehicle.x, desiredX, 2.5f, dt);

        vehicle.x +=
            std::sin(worldTime_ * 0.8f + vehicle.z * 0.03f) *
            vehicle.drift;

        if (!vehicle.scored && vehicle.z > playerZ_ + 4.0f)
        {
            vehicle.scored = true;
            scoreValue_ += 60.0f;
        }

        awardNearMiss(vehicle);
    }

    traffic_.erase(
        std::remove_if(
            traffic_.begin(),
            traffic_.end(),
            [](const TrafficVehicle& vehicle)
            {
                return vehicle.z > kRemoveZ;
            }
        ),
        traffic_.end()
    );
}

void Game::updatePickups(float dt)
{
    for (auto& coin : pickups_)
    {
        coin.z += speed_ * dt;

        coin.phase += dt * 4.0f;

        if (!coin.collected)
        {
            // улучшение «магнит» расширяет зону подбора монет
            const float magnet = static_cast<float>(upgrades_[3]);

            const bool xHit = std::abs(playerX_ - coin.x) < 0.95f + 0.35f * magnet;

            const bool zHit = std::abs(playerZ_ - coin.z) < 1.5f + 0.25f * magnet;

            if (xHit && zHit)
            {
                coin.collected = true;

                audio_.playCoin();

                ++coins_;

                ++bank_;

                boost_ = std::min(100.0f, boost_ + 12.0f);

                combo_ = std::min(combo_ + 1, 15);

                comboTimer_ = 3.2f;

                scoreValue_ +=
                    120.0f + static_cast<float>(combo_) * 25.0f;
            }
        }
    }

    pickups_.erase(
        std::remove_if(
            pickups_.begin(),
            pickups_.end(),
            [](const CoinPickup& coin)
            {
                return coin.collected || coin.z > kRemoveZ;
            }
        ),
        pickups_.end()
    );
}

void Game::awardNearMiss(TrafficVehicle& vehicle)
{
    if (vehicle.nearMissAwarded)
    {
        return;
    }

    const float dz = std::abs(vehicle.z - playerZ_);

    const float dx = std::abs(vehicle.x - playerX_);

    const float dangerousWidth =
        (vehicle.width + kPlayerWidth) * 0.5f + 0.9f;

    const float collisionWidth =
        (vehicle.width + kPlayerWidth) * 0.5f * 0.72f;

    if (dz < 1.4f && dx < dangerousWidth && dx > collisionWidth)
    {
        vehicle.nearMissAwarded = true;

        combo_ = std::min(combo_ + 1, 15);

        comboTimer_ = 3.4f;

        boost_ = std::min(100.0f, boost_ + 8.0f);

        scoreValue_ +=
            180.0f + static_cast<float>(combo_) * 40.0f;
    }
}

void Game::checkCollisions()
{
    if (invulnerabilityTimer_ > 0.0f)
    {
        return;
    }

    for (auto& vehicle : traffic_)
    {
        const float widthFactor =
            vehicle.type == TrafficType::Truck ? 0.76f : 0.72f;

        const float lengthFactor =
            vehicle.type == TrafficType::Truck ? 0.78f : 0.70f;

        const bool sameLaneSpace =
            overlaps(
                playerX_,
                kPlayerWidth * 0.76f,
                vehicle.x,
                vehicle.width * widthFactor
            );

        const bool sameDepthSpace =
            overlaps(
                playerZ_,
                kPlayerLength * 0.72f,
                vehicle.z,
                vehicle.length * lengthFactor
            );

        if (sameLaneSpace && sameDepthSpace)
        {
            float damage = 34.0f;

            if (vehicle.type == TrafficType::Truck)
            {
                damage = 55.0f;
            }
            else if (vehicle.type == TrafficType::Van)
            {
                damage = 42.0f;
            }

            audio_.playCrash();

            // улучшение «броня»: до половины урона на пятом уровне
            damage *= 1.0f - 0.10f * static_cast<float>(upgrades_[2]);

            health_ = std::max(0.0f, health_ - damage);

            speed_ *= 0.54f;

            boost_ = std::max(0.0f, boost_ - 28.0f);

            combo_ = 0;

            comboTimer_ = 0.0f;

            invulnerabilityTimer_ = 1.15f;

            vehicle.z += 7.0f;

            if (health_ <= 0.0f)
            {
                state_ = GameState::GameOver;

                updateHighScore();

                saveHighScore(highScore_);

                saveGarage();
            }

            break;
        }
    }
}

void Game::updateHighScore()
{
    if (score_ > highScore_)
    {
        highScore_ = score_;
    }
}

int Game::loadHighScore() const
{
    std::ifstream file("highscore.txt");

    int value = 0;

    if (file >> value)
    {
        return std::max(0, value);
    }

    return 0;
}

void Game::saveHighScore(int value) const
{
    std::ofstream file("highscore.txt", std::ios::trunc);

    file << value;
}