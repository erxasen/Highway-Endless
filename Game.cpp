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
    constexpr int kUpgradeCosts[kMaxUpgradeLevel] = { 50, 100, 175, 275, 400 };

    // Покраска: цена каждого цвета (BLUE бесплатный)
    constexpr int kPaintCosts[kPaintCount] = { 0, 75, 75, 100, 125, 150 };

    // Машины игрока: SPORT, COUPE, SEDAN, VAN
    constexpr int kCarCosts[kCarCount] = { 0, 8000, 20000, 45000, 90000, 180000 };
    constexpr int kCarLevels[kCarCount] = { 1, 2, 3, 5, 8, 12 };
    constexpr float kCarSpeed[kCarCount] = { 1.00f, 1.13f, 1.27f, 1.40f, 1.57f, 1.75f };
    constexpr float kCarGrip[kCarCount] = { 1.00f, 1.04f, 1.20f, 0.84f, 1.12f, 1.07f };
    constexpr float kCarArmor[kCarCount] = { 1.00f, 1.08f, 1.12f, 0.73f, 1.28f, 1.20f };   // множитель урона

    // Трассы: HIGHWAY, DESERT, SNOW
    constexpr int kTrackLevels[kTrackCount] = { 1, 3, 6 };
    constexpr float kTrackScoreMul[kTrackCount] = { 1.00f, 1.15f, 1.30f };

    // Достижения: цель и награда монетами
    constexpr int kAchTargets[kAchCount] = { 20000, 200, 5, 15, 5000, 300, 5, 3 };
    constexpr int kAchRewards[kAchCount] = { 30, 30, 50, 40, 60, 40, 50, 80 };

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

    const std::array<Color, kPaintCount> kPaintColors = { {
        {0.04f, 0.42f, 1.00f, 1.0f},   // BLUE
        {0.92f, 0.10f, 0.12f, 1.0f},   // RED
        {0.10f, 0.72f, 0.30f, 1.0f},   // GREEN
        {0.95f, 0.70f, 0.10f, 1.0f},   // GOLD
        {0.50f, 0.22f, 0.85f, 1.0f},   // PURPLE
        {0.08f, 0.08f, 0.10f, 1.0f}    // BLACK
    } };

    float approach(float current, float target, float speed, float dt) {
        const float t = std::clamp(speed * dt, 0.0f, 1.0f);
        return current + (target - current) * t;
    }

    bool overlaps(float aCenter, float aSize, float bCenter, float bSize) {
        return std::abs(aCenter - bCenter) < (aSize + bSize) * 0.5f;
    }

    // сколько опыта нужно, чтобы с уровня level перейти на следующий
    int xpNeeded(int level) {
        return 200 + 120 * (level - 1);
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
    typeDist_(0, 5)
{
    loadGarage();
    loadStats();
    loadTop();
    loadSettings();
    audio_.start();
    applySettings();
    reset();

    // игра стартует с главного меню; Enter или R начинает забег
    state_ = GameState::Menu;
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

bool Game::paintUnlocked(int id) const
{
    if (id < 0 || id >= kPaintCount)
    {
        return false;
    }

    return (paintMask_ & (1u << id)) != 0u;
}

int Game::paintCost(int id)
{
    if (id < 0 || id >= kPaintCount)
    {
        return 0;
    }

    return kPaintCosts[id];
}

const char* Game::paintName(int id)
{
    switch (id)
    {
    case 0: return "BLUE";
    case 1: return "RED";
    case 2: return "GREEN";
    case 3: return "GOLD";
    case 4: return "PURPLE";
    case 5: return "BLACK";
    default: return "";
    }
}

Color Game::paintColor(int id)
{
    if (id < 0 || id >= kPaintCount)
    {
        return kPaintColors[0];
    }

    return kPaintColors[id];
}

bool Game::applyPaint(int id)
{
    if (id < 0 || id >= kPaintCount)
    {
        return false;
    }

    if (!paintUnlocked(id))
    {
        const int cost = kPaintCosts[id];

        if (bank_ < cost)
        {
            return false;
        }

        bank_ -= cost;
        paintMask_ |= (1u << id);
    }

    selectedPaint_ = id;

    saveGarage();

    return true;
}

void Game::loadGarage()
{
    std::ifstream file("garage.txt");

    int bank = 0;
    int levels[kUpgradeCount] = { 0, 0, 0, 0 };
    int paint = 0;
    int mask = 1;
    int xp = 0;
    int carSel = 0;
    int carBits = 1;
    int trackSel = 0;

    if (file >> bank)
    {
        for (int i = 0; i < kUpgradeCount; ++i)
        {
            file >> levels[i];
        }

        // старые файлы без покраски: остаётся BLUE
        if (!(file >> paint >> mask))
        {
            paint = 0;
            mask = 1;
        }
        else
        {
            // старые файлы без опыта: начинаем с нуля
            file >> xp;

            // старые файлы без машин: остаётся SPORT
            if (!(file >> carSel >> carBits))
            {
                carSel = 0;
                carBits = 1;
            }
            else
            {
                file >> trackSel;
            }
        }
    }

    totalXp_ = std::max(0, xp);

    carMask_ = static_cast<unsigned int>(carBits) & ((1u << kCarCount) - 1u);
    carMask_ |= 1u;

    selectedCar_ = std::clamp(carSel, 0, kCarCount - 1);

    if (!carUnlocked(selectedCar_))
    {
        selectedCar_ = 0;
    }

    carCursor_ = selectedCar_;

    selectedTrack_ = std::clamp(trackSel, 0, kTrackCount - 1);

    if (!trackUnlocked(selectedTrack_))
    {
        selectedTrack_ = 0;
    }

    bank_ = std::max(0, bank);

    for (int i = 0; i < kUpgradeCount; ++i)
    {
        upgrades_[i] = std::clamp(levels[i], 0, kMaxUpgradeLevel);
    }

    paintMask_ = static_cast<unsigned int>(mask) & ((1u << kPaintCount) - 1u);
    paintMask_ |= 1u;

    selectedPaint_ = std::clamp(paint, 0, kPaintCount - 1);

    if (!paintUnlocked(selectedPaint_))
    {
        selectedPaint_ = 0;
    }

    paintCursor_ = selectedPaint_;
}

void Game::saveGarage() const
{
    std::ofstream file("garage.txt", std::ios::trunc);

    file << bank_;

    for (int i = 0; i < kUpgradeCount; ++i)
    {
        file << ' ' << upgrades_[i];
    }

    file << ' ' << selectedPaint_ << ' ' << paintMask_ << ' ' << totalXp_
        << ' ' << selectedCar_ << ' ' << carMask_ << ' ' << selectedTrack_;
}

int Game::settingValue(int id) const
{
    if (id < 0 || id >= kSettingCount)
    {
        return 0;
    }

    return settings_[id];
}

const char* Game::settingName(int id)
{
    switch (id)
    {
    case 0: return "MASTER VOLUME";
    case 1: return "ENGINE";
    case 2: return "EFFECTS";
    case 3: return "AMBIENT";
    case 4: return "SCREEN SHAKE";
    case 5: return "SPEED EFFECTS";
    default: return "";
    }
}

const char* Game::settingInfo(int id)
{
    switch (id)
    {
    case 0: return "ALL SOUNDS";
    case 1: return "YOUR CAR MOTOR";
    case 2: return "COINS CRASH SIREN TIRES";
    case 3: return "RAIN WIND BOOST";
    case 4: return "CAMERA SHAKE ON HITS";
    case 5: return "SPEED LINES AND DARK EDGES";
    default: return "";
    }
}

void Game::applySettings()
{
    const float k = 1.0f / static_cast<float>(kSettingMax);

    audio_.setVolumes(
        static_cast<float>(settings_[0]) * k,
        static_cast<float>(settings_[1]) * k,
        static_cast<float>(settings_[2]) * k,
        static_cast<float>(settings_[3]) * k
    );
}

void Game::changeSetting(int id, int delta)
{
    if (id < 0 || id >= kSettingCount)
    {
        return;
    }

    const int value = std::clamp(settings_[id] + delta, 0, kSettingMax);

    if (value == settings_[id])
    {
        return;
    }

    settings_[id] = value;

    applySettings();
    saveSettings();

    // на общей громкости и эффектах сразу слышно пример
    if (id == 0 || id == 2)
    {
        audio_.playCoin();
    }
}

void Game::loadSettings()
{
    std::ifstream file("settings.txt");

    for (int i = 0; i < kSettingCount; ++i)
    {
        int value = kSettingMax;

        if (file >> value)
        {
            settings_[i] = std::clamp(value, 0, kSettingMax);
        }
        else
        {
            settings_[i] = kSettingMax;
        }
    }
}

void Game::saveSettings() const
{
    std::ofstream file("settings.txt", std::ios::trunc);

    for (int i = 0; i < kSettingCount; ++i)
    {
        file << settings_[i] << ' ';
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
    float weather = weatherRain(worldTime_);

    if (selectedTrack_ == 1)
    {
        // пустыня: дождя нет
        weather = 0.0f;
    }
    else if (selectedTrack_ == 2)
    {
        // снежная трасса: всегда немного скользко
        weather = std::max(weather, 0.35f);
    }

    return weather * (1.0f - tunnelInside());
}

void Game::reset()
{
    state_ = GameState::Running;

    trafficSpawnSequence_ = 0;
    player2Lane_ = 2;
    player2X_ = laneCenter(2);
    player2Z_ = 4.0f;
    player2Yaw_ = 0.0f;
    player2Speed_ = 28.0f;
    player2Distance_ = 0.0f;
    player2Boost_ = 100.0f;
    player2Health_ = 100.0f;
    player2Invulnerability_ = 0.0f;
    player2LeftWasDown_ = false;
    player2RightWasDown_ = false;
    player2Throttle_ = false;
    player2Brake_ = false;
    player2Nitro_ = false;
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


    timeWarpTimer_ = 0.0f;
    timeWarpCooldown_ = 0.0f;
    timeWarpKeyWasDown_ = false;


    health_ = 100.0f;
    invulnerabilityTimer_ = 0.0f;

    worldTime_ = 0.0f;
    roadOffset_ = 0.0f;
    distance_ = 0.0f;
    nextMilestone_ = 2000.0f;
    milestoneMeters_ = 0;
    milestoneTimer_ = 0.0f;

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
    roadBlocks_.clear();
    roadBlockTimer_ = 18.0f;
    oilSlicks_.clear();
    oilSpawnTimer_ = 25.0f;
    oilTimer_ = 0.0f;

    powerUps_.clear();
    powerSpawnTimer_ = 14.0f;
    shieldTimer_ = 0.0f;
    magnetTimer_ = 0.0f;
    powerPopupTimer_ = 0.0f;

    // новое задание на каждый забег
    {
        std::uniform_int_distribution<int> missionDist(0, 3);
        missionType_ = missionDist(rng_);
    }

    missionDone_ = false;
    missionPopupTimer_ = 0.0f;
    maxCombo_ = 0;
    escapes_ = 0;

    chaseState_ = ChaseState::Idle;
    chaseTimer_ = kFirstChaseDelay;
    policeGap_ = 0.0f;
    policeX_ = playerX_;
    escapeTimer_ = 0.0f;
    busted_ = false;
    runRecorded_ = false;
    lastRunXp_ = 0;
    lastLevelUps_ = 0;
    runMaxSpeed_ = 0.0f;
    achPopupTimer_ = 0.0f;
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

    const bool timeWarpPressed =
        pressedOnce(window, GLFW_KEY_Q, timeWarpKeyWasDown_);

    if (state_ == GameState::Running &&
        gameMode_ == GameMode::SinglePlayer &&
        timeWarpPressed &&
        timeWarpTimer_ <= 0.0f &&
        timeWarpCooldown_ <= 0.0f)
    {
        timeWarpTimer_ = kTimeWarpDuration;
        timeWarpCooldown_ =
            kTimeWarpDuration + kTimeWarpCooldown;
    }


    const bool chooseSingle = pressedOnce(window, GLFW_KEY_1, mode1WasDown_);
    const bool chooseBattle = pressedOnce(window, GLFW_KEY_2, mode2WasDown_);
    if (state_ == GameState::Menu)
    {
        if (chooseSingle) { selectedGameMode_ = GameMode::SinglePlayer; multiplayerPending_ = false; }
        if (chooseBattle) { selectedGameMode_ = GameMode::TwoPlayerBattle; multiplayerPending_ = false; }
    }

    // Main home screen: buttons and hit areas share a single responsive layout.
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool mouseClicked = mouseDown && !menuMouseWasDown_;
    menuMouseWasDown_ = mouseDown;
    if (state_ == GameState::Menu && mouseClicked)
    {
        double cursorX = 0.0, cursorY = 0.0;
        int winW = 1, winH = 1, fbW = 1, fbH = 1;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const float x = static_cast<float>(cursorX * fbW / std::max(1, winW));
        const float y = static_cast<float>(cursorY * fbH / std::max(1, winH));
        const float scale = std::min(static_cast<float>(fbW) / 1600.0f,
                                     static_cast<float>(fbH) / 900.0f);
        const float panelX = 62.0f * scale;
        const float panelY = 165.0f * scale;
        const float buttonX = panelX + 24.0f * scale;
        const float buttonW = 490.0f * scale;
        const auto hit = [&](float yOffset, float height) {
            return x >= buttonX && x <= buttonX + buttonW &&
                   y >= panelY + yOffset * scale && y <= panelY + (yOffset + height) * scale;
        };
        if (hit(105.0f, 75.0f))
        {
            selectedGameMode_ = GameMode::SinglePlayer;
            gameMode_ = GameMode::SinglePlayer;
            multiplayerPending_ = false;
            reset();
        }
        else if (hit(194.0f, 75.0f))
        {
            selectedGameMode_ = GameMode::TwoPlayerBattle;
            gameMode_ = GameMode::TwoPlayerBattle;
            multiplayerPending_ = false;
            reset();
        }
        else if (hit(296.0f, 59.0f))
        {
            garageReturn_ = GameState::Menu;
            state_ = GameState::Garage;
            paintCursor_ = selectedPaint_;
            carCursor_ = selectedCar_;
            garageTab_ = 0;
        }
        else if (hit(366.0f, 59.0f))
        {
            state_ = GameState::Achievements;
        }
        else if (hit(436.0f, 59.0f))
        {
            settingsReturn_ = GameState::Menu;
            state_ = GameState::Settings;
        }
        else if (hit(506.0f, 59.0f))
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    // Clickable garage tabs, car list, navigation and purchase/equip.
    if (state_ == GameState::Garage && mouseClicked)
    {
        double mx = 0.0, my = 0.0;
        int ww = 1, wh = 1, fw = 1, fh = 1;
        glfwGetCursorPos(window, &mx, &my);
        glfwGetWindowSize(window, &ww, &wh);
        glfwGetFramebufferSize(window, &fw, &fh);
        const float x = static_cast<float>(mx * fw / std::max(1, ww));
        const float y = static_cast<float>(my * fh / std::max(1, wh));
        const float panelW = std::min(590.0f, fw * 0.44f);
        const float panelX = 20.0f;
        const float panelY = 95.0f;
        const auto hit = [&](float bx, float by, float bw, float bh) {
            return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
        };
        if (hit(panelX + 16, panelY + 72, (panelW - 42) * 0.5f, 48))
            garageTab_ = 0;
        else if (hit(panelX + 26 + (panelW - 42) * 0.5f, panelY + 72,
                     (panelW - 42) * 0.5f, 48))
            garageTab_ = 1;
        else if (garageTab_ == 0)
        {
            const float rowY = panelY + 137.0f;
            const float rowH = std::min(67.0f, (fh - rowY - 160.0f) / 6.0f);
            for (int i = 0; i < kCarCount; ++i)
                if (hit(panelX + 18, rowY + i * rowH, panelW - 36, rowH - 5))
                    carCursor_ = i;
            const float actionY = static_cast<float>(fh) - 88.0f;
            if (hit(panelX + 18, actionY, 100.0f, 58.0f))
                carCursor_ = (carCursor_ + kCarCount - 1) % kCarCount;
            else if (hit(panelX + panelW - 118, actionY, 100.0f, 58.0f))
                carCursor_ = (carCursor_ + 1) % kCarCount;
            else if (hit(panelX + 130, actionY, panelW - 260, 58.0f))
                applyCar(carCursor_);
        }
    }

    // Universal BACK: mouse button in every sub-menu or Escape.
    const bool subMenu = state_ == GameState::Garage ||
                         state_ == GameState::Settings ||
                         state_ == GameState::Achievements;
    bool backClicked = false;
    if (subMenu && mouseClicked)
    {
        double mx = 0.0, my = 0.0;
        int ww = 1, wh = 1, fw = 1, fh = 1;
        glfwGetCursorPos(window, &mx, &my);
        glfwGetWindowSize(window, &ww, &wh);
        glfwGetFramebufferSize(window, &fw, &fh);
        const float x = static_cast<float>(mx * fw / std::max(1, ww));
        const float y = static_cast<float>(my * fh / std::max(1, wh));
        // Consistent upper-right BACK button in every submenu.
        backClicked = x >= fw - 205.0f && x <= fw - 25.0f &&
                      y >= 24.0f && y <= 78.0f;
    }
    const bool escapePressed = pressedOnce(window, GLFW_KEY_ESCAPE, escapeWasDown_);
    if (backClicked || (escapePressed && subMenu))
    {
        if (state_ == GameState::Garage) state_ = garageReturn_;
        else if (state_ == GameState::Achievements) state_ = achReturn_;
        else if (state_ == GameState::Settings)
        {
            state_ = settingsReturn_;
            saveSettings();
        }
    }
    else if (escapePressed && state_ == GameState::Menu)
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    else if (escapePressed && state_ == GameState::Running)
    {
        state_ = GameState::Paused;
    }
    else if (escapePressed && state_ == GameState::Paused)
    {
        state_ = GameState::Running;
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

    // R в гараже не сбрасывает забег, чтобы не нажать случайно
    if (pressedOnce(window, GLFW_KEY_R, restartWasDown_))
    {
        if (state_ != GameState::Garage && state_ != GameState::Achievements && state_ != GameState::Settings)
        {
            reset();
        }
    }

    // гараж: G открывает на паузе и после аварии, повторно закрывает
    if (pressedOnce(window, GLFW_KEY_G, garageWasDown_))
    {
        if (state_ == GameState::Paused || state_ == GameState::GameOver || state_ == GameState::Menu)
        {
            garageReturn_ = state_;
            state_ = GameState::Garage;
            paintCursor_ = selectedPaint_;
            carCursor_ = selectedCar_;
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

    // трасса: T переключает на следующую открытую (в меню и после аварии)
    if (pressedOnce(window, GLFW_KEY_T, trackKeyWasDown_))
    {
        if (state_ == GameState::Menu || state_ == GameState::GameOver)
        {
            for (int step = 1; step <= kTrackCount; ++step)
            {
                const int next = (selectedTrack_ + step) % kTrackCount;

                if (trackUnlocked(next))
                {
                    selectedTrack_ = next;
                    break;
                }
            }

            saveGarage();
        }
    }

    // достижения: H открывает на паузе, после аварии и в главном меню, повторно закрывает
    if (pressedOnce(window, GLFW_KEY_H, achKeyWasDown_))
    {
        if (state_ == GameState::Paused || state_ == GameState::GameOver || state_ == GameState::Menu)
        {
            achReturn_ = state_;
            state_ = GameState::Achievements;
        }
        else if (state_ == GameState::Achievements)
        {
            state_ = achReturn_;
        }
    }

    // настройки: O открывает на паузе, после аварии и в главном меню, повторно закрывает;
    // стрелки вверх и вниз выбирают строку, влево и вправо меняют значение
    {
        const bool open = pressedOnce(window, GLFW_KEY_O, settingsKeyWasDown_);
        const bool up = pressedOnce(window, GLFW_KEY_UP, setUpWasDown_);
        const bool down = pressedOnce(window, GLFW_KEY_DOWN, setDownWasDown_);
        const bool left = pressedOnce(window, GLFW_KEY_LEFT, setLeftWasDown_);
        const bool right = pressedOnce(window, GLFW_KEY_RIGHT, setRightWasDown_);

        if (open)
        {
            if (state_ == GameState::Paused || state_ == GameState::GameOver || state_ == GameState::Menu)
            {
                settingsReturn_ = state_;
                state_ = GameState::Settings;
            }
            else if (state_ == GameState::Settings)
            {
                state_ = settingsReturn_;
                saveSettings();
            }
        }
        else if (state_ == GameState::Settings)
        {
            if (up)
            {
                settingsCursor_ = (settingsCursor_ + kSettingCount - 1) % kSettingCount;
            }

            if (down)
            {
                settingsCursor_ = (settingsCursor_ + 1) % kSettingCount;
            }

            if (left)
            {
                changeSetting(settingsCursor_, -1);
            }

            if (right)
            {
                changeSetting(settingsCursor_, 1);
            }
        }
    }

    // выбор машины в гараже: Q и E листают, пробел покупает или выбирает
    {
        const bool carPrev = pressedOnce(window, GLFW_KEY_Q, carPrevWasDown_);
        const bool carNext = pressedOnce(window, GLFW_KEY_E, carNextWasDown_);
        const bool carApply = pressedOnce(window, GLFW_KEY_SPACE, carApplyWasDown_);

        if (state_ == GameState::Garage)
        {
            if (carPrev)
            {
                carCursor_ = (carCursor_ + kCarCount - 1) % kCarCount;
            }

            if (carNext)
            {
                carCursor_ = (carCursor_ + 1) % kCarCount;
            }

            if (carApply)
            {
                applyCar(carCursor_);
            }
        }
    }

    // покраска: стрелки двигают курсор, Enter выбирает или покупает цвет
    {
        const bool paintLeft = pressedOnce(window, GLFW_KEY_LEFT, paintLeftWasDown_);
        const bool paintRight = pressedOnce(window, GLFW_KEY_RIGHT, paintRightWasDown_);
        const bool paintApply = pressedOnce(window, GLFW_KEY_ENTER, paintApplyWasDown_);

        if (state_ == GameState::Garage)
        {
            if (paintLeft)
            {
                paintCursor_ = (paintCursor_ + kPaintCount - 1) % kPaintCount;
            }

            if (paintRight)
            {
                paintCursor_ = (paintCursor_ + 1) % kPaintCount;
            }

            if (paintApply)
            {
                applyPaint(paintCursor_);
            }
        }
        else if (state_ == GameState::Menu && paintApply)
        {
            if (selectedGameMode_ == GameMode::SinglePlayer)
            {
                gameMode_ = GameMode::SinglePlayer;
                multiplayerPending_ = false;
                reset();
            }
            else
            {
                gameMode_ = GameMode::TwoPlayerBattle;
                multiplayerPending_ = false;
                reset();
            }
        }
    }

    if (state_ != GameState::Running)
    {
        return;
    }

    if (gameMode_ == GameMode::TwoPlayerBattle)
    {
        if (pressedOnce(window, GLFW_KEY_LEFT, player2LeftWasDown_))
            player2Lane_ = std::max(0, player2Lane_ - 1);
        if (pressedOnce(window, GLFW_KEY_RIGHT, player2RightWasDown_))
            player2Lane_ = std::min(2, player2Lane_ + 1);
        player2Throttle_ = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;
        player2Brake_ = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
        player2Nitro_ = glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_KP_0) == GLFW_PRESS;
    }

    const bool leftPressed =
        (gameMode_ == GameMode::SinglePlayer && pressedOnce(window, GLFW_KEY_LEFT, leftWasDown_)) ||
        pressedOnce(window, GLFW_KEY_A, aWasDown_);

    const bool rightPressed =
        (gameMode_ == GameMode::SinglePlayer && pressedOnce(window, GLFW_KEY_RIGHT, rightWasDown_)) ||
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
            (gameMode_ == GameMode::SinglePlayer && glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS))
        ? 1.0f
        : 0.0f;

    brake_ =
        (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
            (gameMode_ == GameMode::SinglePlayer && glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS))
        ? 1.0f
        : 0.0f;

    const bool boostPressed =
        glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        (gameMode_ == GameMode::SinglePlayer && glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

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
    // забег закончился: один раз заносим результат в таблицу
    if (state_ == GameState::GameOver && !runRecorded_)
    {
        recordRun();
    }

    if (state_ != GameState::Running)
    {
        // пауза или авария: звук плавно затихает
        if (state_ == GameState::Settings)
        {
            // в настройках играет двигатель (на строке «окружение» ещё и дождь), чтобы слышать изменения
            audio_.update(120.0f, false, settingsCursor_ == 3 ? 0.6f : 0.0f, 0.0f, true);
        }
        else
        {
            audio_.update(speedKmh(), false, 0.0f, 0.0f, false);
        }

        audio_.setSiren(0.0f);
        audio_.setSkid(0.0f);
        return;
    }

    // защита от скачков: после подвисания окна машины не должны
    // пролетать сквозь игрока
    dt = std::min(dt, 0.05f);


    if (timeWarpTimer_ > 0.0f)
    {
        timeWarpTimer_ = std::max(
            0.0f,
            timeWarpTimer_ - dt
        );
    }

    if (timeWarpCooldown_ > 0.0f)
    {
        timeWarpCooldown_ = std::max(
            0.0f,
            timeWarpCooldown_ - dt
        );
    }


    if (gameMode_ == GameMode::TwoPlayerBattle)
    {
        player2Invulnerability_ = std::max(0.0f, player2Invulnerability_ - dt);
        const bool boost2 = player2Nitro_ && player2Boost_ > 0.5f && player2Speed_ > 20.0f;
        player2Boost_ = std::clamp(player2Boost_ + (boost2 ? -31.0f : 9.0f) * dt, 0.0f, 100.0f);
        const float engine2 = static_cast<float>(upgrades_[0]);
        const float wet2 = rain() * (1.0f - 0.15f * static_cast<float>(upgrades_[1]));
        const float targetCruise2 = baseCruiseSpeed_ + difficulty_ * 2.1f;
        float target2 = targetCruise2 + (player2Throttle_ ? 12.0f + engine2 : 0.0f)
            + (boost2 ? 18.0f : 0.0f)
            - (player2Brake_ ? 22.0f * (1.0f - 0.4f * wet2) : 0.0f);
        const float max2 = (maxSpeed_ + (boost2 ? 15.0f : 0.0f) +
            1.5f * engine2) * carSpeed(selectedCar_);
        target2 = std::clamp(target2, 14.0f, max2);
        const float response2 = target2 > player2Speed_
            ? 2.2f * (1.0f + 0.08f * engine2) * (1.0f - 0.15f * wet2)
            : 4.8f * (1.0f - 0.40f * wet2);
        player2Speed_ = approach(player2Speed_, target2, response2, dt);
        const float previousX = player2X_;
        player2X_ = approach(player2X_, laneCenter(player2Lane_), 8.5f * carGrip(selectedCar_), dt);
        player2Yaw_ = approach(player2Yaw_, std::clamp(-(player2X_ - previousX) / std::max(dt, 0.0001f) * 1.35f, -12.0f, 12.0f), 8.0f, dt);
        player2Distance_ += player2Speed_ * dt;
    }

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
    // на масляном пятне дорога как в сильный ливень, плюс машину водит из стороны в сторону
    const float slick = std::max(rainLevel, oilTimer_ > 0.0f ? 1.0f : 0.0f);

    const float wet = slick * (1.0f - 0.15f * tiresLevel);

    const float oilWobble = oilTimer_ > 0.0f ? std::sin(worldTime_ * 8.0f) * 5.0f : 0.0f;

    float targetSpeed = targetCruise + throttle_ * (12.0f + engineLevel) + boostExtra;

    // на мокрой дороге тормоза слабее
    targetSpeed -= brake_ * 22.0f * (1.0f - 0.4f * wet);

    const float currentMaxSpeed =
        (maxSpeed_ + (boosting_ ? 15.0f : 0.0f) + 1.5f * engineLevel) * carSpeed(selectedCar_);

    targetSpeed = std::clamp(targetSpeed, 14.0f, currentMaxSpeed);

    const float response =
        targetSpeed > speed_
        ? 2.2f * (1.0f + 0.08f * engineLevel) * (1.0f - 0.15f * wet)
        : 4.8f * (1.0f - 0.40f * wet);

    speed_ = approach(speed_, targetSpeed, response, dt);

    const float targetX = laneCenter(targetLane_);

    const float oldX = playerX_;

    // на мокрой дороге машина медленнее перестраивается
    playerX_ = approach(playerX_, targetX, 8.5f * (1.0f + 0.06f * tiresLevel) * carGrip(selectedCar_) * (1.0f - 0.45f * wet), dt);

    lateralVelocity_ = (playerX_ - oldX) / std::max(dt, 0.0001f);

    playerYaw_ = approach(
        playerYaw_,
        std::clamp(-lateralVelocity_ * 1.35f * (1.0f + 0.8f * wet) + oilWobble, -12.0f, 12.0f),
        8.0f,
        dt
    );

    roadOffset_ += speed_ * dt;

    distance_ += speed_ * dt;
    if (gameMode_ == GameMode::TwoPlayerBattle)
        player2Z_ = playerZ_ + distance_ - player2Distance_;

    // бонус монетами за каждые 2000 м пути
    if (milestoneTimer_ > 0.0f)
    {
        milestoneTimer_ -= dt;
    }

    if (distance_ >= nextMilestone_)
    {
        milestoneMeters_ = static_cast<int>(nextMilestone_);
        milestoneTimer_ = 2.5f;
        nextMilestone_ += 2000.0f;

        bank_ += 8;
        audio_.playCoin();
    }

    audio_.update(speedKmh(), boosting_, rainLevel, tunnelInside(), true);

    while (roadOffset_ >= kRoadMarkerSpacing)
    {
        roadOffset_ -= kRoadMarkerSpacing;
    }

    scoreValue_ +=
        speed_ * dt *
        (7.2f + static_cast<float>(combo_) * 0.55f) *
        trackScoreMul(selectedTrack_);

    score_ = static_cast<int>(scoreValue_) + coins_ * 120;

    updateHighScore();

    trafficTimer_ += dt;

    if (trafficTimer_ >= trafficInterval_)
    {
        spawnTraffic();
        if (gameMode_ == GameMode::TwoPlayerBattle && std::abs(player2Z_ - playerZ_) > 50.0f)
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

    updatePowerUps(dt);

    checkCollisions();

    if (state_ == GameState::Running)
    {
        updateRoadBlocks(dt);
    }

    if (state_ == GameState::Running)
    {
        updateOilSlicks(dt);
    }

    if (state_ == GameState::Running)
    {
        updateChase(dt);
    }

    if (state_ == GameState::Running)
    {
        updateMission(dt);
    }

    runMaxSpeed_ = std::max(runMaxSpeed_, speedKmh());

    if (achPopupTimer_ > 0.0f)
    {
        achPopupTimer_ -= dt;
    }

    checkAchievements();

    // сирена полиции: громче, когда полиция ближе
    {
        float siren = 0.0f;

        if (state_ == GameState::Running)
        {
            if (chaseState_ == ChaseState::Warning)
            {
                siren = 0.4f;
            }
            else if (chaseState_ == ChaseState::Chasing)
            {
                siren = std::clamp(1.1f - policeGap_ / kChaseEscapeGap, 0.25f, 1.0f);
            }
        }

        audio_.setSiren(siren);
    }

    // скрип шин: торможение на скорости, масляное пятно, резкая смена полосы
    {
        const bool skidding =
            state_ == GameState::Running &&
            ((brake_ > 0.5f && speedKmh() > 120.0f) || oilTimer_ > 0.0f || std::abs(playerYaw_) > 7.5f);

        audio_.setSkid(skidding ? 1.0f : 0.0f);
    }
    if (gameMode_ == GameMode::TwoPlayerBattle && health_ <= 0.0f && player2Health_ <= 0.0f)
        state_ = GameState::GameOver;

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
            ++escapes_;
            scoreValue_ += kEscapeBonus;
            bank_ += 10;
            boost_ = std::min(100.0f, boost_ + 40.0f);
            audio_.playAchievement();
        }
        else if (policeGap_ <= 0.0f && gameMode_ != GameMode::TwoPlayerBattle)
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
    // Spawn ahead of the leading player in Player 1's coordinate frame.
    const bool battle = gameMode_ == GameMode::TwoPlayerBattle;
    const bool spawnForP2 = battle && (trafficSpawnSequence_++ % 2 == 1);
    vehicle.z = (spawnForP2 ? player2Z_ : playerZ_) - 150.0f;

    vehicle.color = kTrafficColors[colorDist_(rng_)];

    const int type = typeDist_(rng_);

    vehicle.type = static_cast<TrafficType>(type);

    // relativeSpeed отрицательный: машина едет вперёд со своей скоростью
    // (в среднем 6-11 м/с). Положительное значение означало бы езду задним
    // ходом относительно земли. Минимальная скорость игрока 14 м/с, а самая
    // быстрая машина едет не быстрее 13 м/с, поэтому машины не уезжают вперёд насовсем.
    std::uniform_real_distribution<float> forwardSpeedDist(6.0f, 11.0f);

    vehicle.relativeSpeed = -forwardSpeedDist(rng_);

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
        vehicle.relativeSpeed -= 2.0f;   // спорткар едет быстрее остальных
        break;

    case TrafficType::Truck:
        vehicle.width = 2.2f;
        vehicle.length = 6.1f;
        vehicle.height = 2.25f;
        vehicle.relativeSpeed += 3.0f;   // грузовик медленнее
        break;

    case TrafficType::Van:
        vehicle.width = 1.9f;
        vehicle.length = 4.6f;
        vehicle.height = 1.8f;
        vehicle.relativeSpeed += 1.0f;
        break;

    case TrafficType::Bus:
        vehicle.width = 2.3f;
        vehicle.length = 8.0f;
        vehicle.height = 2.6f;
        vehicle.relativeSpeed += 4.0f;   // автобус самый медленный
        break;

    case TrafficType::Coupe:
        vehicle.width = 1.58f;
        vehicle.length = 3.55f;
        vehicle.height = 0.94f;
        vehicle.relativeSpeed -= 1.0f;   // купе чуть быстрее седана
        break;
    }

    std::uniform_real_distribution<float> driftDist(-0.08f, 0.08f);

    vehicle.drift = driftDist(rng_);

    if (traffic_.size() >= 110u)
    {
        auto farthest = traffic_.end();
        float worst = -1.0f;
        for (auto it = traffic_.begin(); it != traffic_.end(); ++it)
        {
            const float nearest = gameMode_ == GameMode::TwoPlayerBattle
                ? std::min(std::abs(it->z - playerZ_), std::abs(it->z - player2Z_))
                : std::abs(it->z - playerZ_);
            if (nearest > worst) { worst = nearest; farthest = it; }
        }
        if (farthest != traffic_.end()) traffic_.erase(farthest);
    }
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
    const bool warpActive = timeWarpTimer_ > 0.0f;

    const float trafficDt =
        warpActive ? dt * kTimeWarpFactor : dt;

    for (auto& vehicle : traffic_)
    {
        vehicle.z +=
            (speed_ + vehicle.relativeSpeed) * trafficDt;

        const float desiredX = laneCenter(vehicle.lane);

        vehicle.x = approach(
            vehicle.x,
            desiredX,
            2.5f,
            trafficDt
        );

        vehicle.x +=
            std::sin(
                worldTime_ * 0.8f +
                vehicle.z * 0.03f
            ) * vehicle.drift;

        if (!vehicle.scored &&
            vehicle.z > playerZ_ + 4.0f)
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
            [this](const TrafficVehicle& vehicle)
            {
                const float trailingZ = gameMode_ == GameMode::TwoPlayerBattle
                    ? std::max(playerZ_, player2Z_) : playerZ_;
                return vehicle.z > trailingZ + 45.0f;
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

            // бонус «магнит» с дороги временно захватывает и соседние полосы
            const float magnetBonusX = magnetTimer_ > 0.0f ? 2.6f : 0.0f;
            const float magnetBonusZ = magnetTimer_ > 0.0f ? 1.5f : 0.0f;

            const bool xHit = std::abs(playerX_ - coin.x) < 0.95f + 0.35f * magnet + magnetBonusX;

            const bool zHit = std::abs(playerZ_ - coin.z) < 1.5f + 0.25f * magnet + magnetBonusZ;

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

        audio_.playNearMiss();

        scoreValue_ +=
            180.0f + static_cast<float>(combo_) * 40.0f;
    }
}

void Game::checkCollisions()
{
    if (gameMode_ == GameMode::TwoPlayerBattle && player2Health_ > 0.0f)
    {
        if (player2Invulnerability_ <= 0.0f)
        {
            for (auto& vehicle : traffic_)
            {
                const float oldP2Z = player2Z_ - (speed_ - player2Speed_) * 0.05f;
                const float oldTrafficZ = vehicle.z - (speed_ + vehicle.relativeSpeed) * 0.05f;
                const float halfDepth = (kPlayerLength * 0.72f + vehicle.length * 0.70f) * 0.5f;
                const float previousGap = oldP2Z - oldTrafficZ;
                const float currentGap = player2Z_ - vehicle.z;
                const bool depthHit = std::abs(currentGap) < halfDepth ||
                    (previousGap * currentGap <= 0.0f && std::abs(previousGap) < 9.0f);
                const float widthFactor = (vehicle.type == TrafficType::Truck || vehicle.type == TrafficType::Bus) ? 0.76f : 0.72f;
                if (overlaps(player2X_, kPlayerWidth * 0.76f, vehicle.x, vehicle.width * widthFactor) && depthHit)
                {
                    float damage = vehicle.type == TrafficType::Truck ? 55.0f :
                        vehicle.type == TrafficType::Bus ? 50.0f :
                        vehicle.type == TrafficType::Van ? 42.0f : 34.0f;
                    damage *= 1.0f - 0.10f * static_cast<float>(upgrades_[2]);
                    player2Health_ = std::max(0.0f, player2Health_ - damage * carArmor(selectedCar_));
                    player2Speed_ *= 0.54f;
                    player2Boost_ = std::max(0.0f, player2Boost_ - 28.0f);
                    player2Invulnerability_ = 1.15f;
                    vehicle.z = player2Z_ + 12.0f;
                    audio_.playCrash();
                    break;
                }
            }
        }
        // Player 2 must collide with road blocks independently of Player 1.
        if (player2Invulnerability_ <= 0.0f)
        {
            for (auto& block : roadBlocks_)
            {
                if (block.hit) continue;
                if (overlaps(player2X_, kPlayerWidth * 0.76f, block.x, 2.6f) &&
                    overlaps(player2Z_, kPlayerLength * 0.72f, block.z, 1.4f))
                {
                    block.hit = true;
                    player2Health_ = std::max(0.0f, player2Health_ - 28.0f *
                        (1.0f - 0.10f * static_cast<float>(upgrades_[2])) * carArmor(selectedCar_));
                    player2Speed_ *= 0.54f;
                    player2Boost_ = std::max(0.0f, player2Boost_ - 28.0f);
                    player2Invulnerability_ = 1.15f;
                    audio_.playCrash();
                    break;
                }
            }
        }
        if (player2Invulnerability_ <= 0.0f &&
            overlaps(playerX_, kPlayerWidth, player2X_, kPlayerWidth) &&
            overlaps(playerZ_, kPlayerLength, player2Z_, kPlayerLength))
        {
            player2Health_ = std::max(0.0f, player2Health_ - 20.0f);
            health_ = std::max(0.0f, health_ - 20.0f);
            player2Speed_ *= 0.7f;
            speed_ *= 0.7f;
            player2Invulnerability_ = 1.15f;
            invulnerabilityTimer_ = 1.15f;
            audio_.playCrash();
        }
    }

    if (invulnerabilityTimer_ > 0.0f)
    {
        return;
    }

    for (auto& vehicle : traffic_)
    {
        const float widthFactor =
            (vehicle.type == TrafficType::Truck || vehicle.type == TrafficType::Bus) ? 0.76f : 0.72f;

        const float lengthFactor =
            (vehicle.type == TrafficType::Truck || vehicle.type == TrafficType::Bus) ? 0.78f : 0.70f;

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
            // щит гасит удар целиком: урона и потери скорости нет
            if (shieldTimer_ > 0.0f)
            {
                shieldTimer_ = 0.0f;

                invulnerabilityTimer_ = 1.15f;

                vehicle.z += 7.0f;

                audio_.playCrash();

                break;
            }

            float damage = 34.0f;

            if (vehicle.type == TrafficType::Truck)
            {
                damage = 55.0f;
            }
            else if (vehicle.type == TrafficType::Van)
            {
                damage = 42.0f;
            }
            else if (vehicle.type == TrafficType::Bus)
            {
                damage = 50.0f;
            }

            audio_.playCrash();

            // улучшение «броня»: до половины урона на пятом уровне
            damage *= 1.0f - 0.10f * static_cast<float>(upgrades_[2]);

            // прочность кузова выбранной машины
            damage *= carArmor(selectedCar_);

            health_ = std::max(0.0f, health_ - damage);

            speed_ *= 0.54f;

            boost_ = std::max(0.0f, boost_ - 28.0f);

            combo_ = 0;

            comboTimer_ = 0.0f;

            invulnerabilityTimer_ = 1.15f;

            vehicle.z += 7.0f;

            if (health_ <= 0.0f && (gameMode_ != GameMode::TwoPlayerBattle || player2Health_ <= 0.0f))
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

void Game::updateRoadBlocks(float dt)
{
    // новые дорожные работы: раз в 10-18 секунд, одна полоса из трёх
    roadBlockTimer_ -= dt;

    if (roadBlockTimer_ <= 0.0f)
    {
        std::uniform_real_distribution<float> delayDist(10.0f, 18.0f);
        roadBlockTimer_ = delayDist(rng_);

        int lane = laneDist_(rng_);

        // не ставим барьер в полосу, где в дальней зоне уже едет машина
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            bool busy = false;

            for (const auto& vehicle : traffic_)
            {
                if (vehicle.lane == lane && vehicle.z < -90.0f)
                {
                    busy = true;
                    break;
                }
            }

            if (!busy)
            {
                break;
            }

            lane = (lane + 1) % 3;
        }

        RoadBlock block;

        block.lane = lane;
        block.x = laneCenter(lane);
        block.z = (gameMode_ == GameMode::TwoPlayerBattle
            ? std::min(playerZ_, player2Z_) : playerZ_) - 150.0f;

        roadBlocks_.push_back(block);
    }

    for (auto& block : roadBlocks_)
    {
        block.z += speed_ * dt;

        if (!block.scored && block.z > playerZ_ + 4.0f)
        {
            block.scored = true;

            if (!block.hit)
            {
                scoreValue_ += 80.0f;
            }
        }

        if (block.hit || invulnerabilityTimer_ > 0.0f)
        {
            continue;
        }

        const bool sameLaneSpace =
            overlaps(playerX_, kPlayerWidth * 0.76f, block.x, 2.6f);

        const bool sameDepthSpace =
            overlaps(playerZ_, kPlayerLength * 0.72f, block.z, 1.4f);

        if (sameLaneSpace && sameDepthSpace)
        {
            block.hit = true;

            audio_.playCrash();

            if (shieldTimer_ > 0.0f)
            {
                shieldTimer_ = 0.0f;

                invulnerabilityTimer_ = 1.15f;

                continue;
            }

            // броня снижает урон так же, как при аварии с машиной
            const float damage = 28.0f * (1.0f - 0.10f * static_cast<float>(upgrades_[2])) * carArmor(selectedCar_);

            health_ = std::max(0.0f, health_ - damage);

            speed_ *= 0.54f;

            boost_ = std::max(0.0f, boost_ - 28.0f);

            combo_ = 0;

            comboTimer_ = 0.0f;

            invulnerabilityTimer_ = 1.15f;

            if (health_ <= 0.0f && (gameMode_ != GameMode::TwoPlayerBattle || player2Health_ <= 0.0f))
            {
                state_ = GameState::GameOver;

                updateHighScore();

                saveHighScore(highScore_);

                saveGarage();

                break;
            }
        }
    }

    roadBlocks_.erase(
        std::remove_if(
            roadBlocks_.begin(),
            roadBlocks_.end(),
            [](const RoadBlock& block)
            {
                return block.z > kRemoveZ;
            }
        ),
        roadBlocks_.end()
    );
}

void Game::updateOilSlicks(float dt)
{
    if (oilTimer_ > 0.0f)
    {
        oilTimer_ -= dt;
    }

    // новое пятно раз в 12-22 секунды, в случайной полосе
    oilSpawnTimer_ -= dt;

    if (oilSpawnTimer_ <= 0.0f)
    {
        std::uniform_real_distribution<float> delayDist(12.0f, 22.0f);
        oilSpawnTimer_ = delayDist(rng_);

        OilSlick slick;

        slick.lane = laneDist_(rng_);
        slick.x = laneCenter(slick.lane);
        slick.z = kSpawnZ;

        oilSlicks_.push_back(slick);
    }

    for (auto& slick : oilSlicks_)
    {
        slick.z += speed_ * dt;

        const bool sameLaneSpace =
            overlaps(playerX_, kPlayerWidth * 0.76f, slick.x, 2.4f);

        const bool sameDepthSpace =
            overlaps(playerZ_, kPlayerLength * 0.72f, slick.z, 5.0f);

        if (sameLaneSpace && sameDepthSpace)
        {
            // пока колёса на пятне, занос держится и ещё немного после него
            oilTimer_ = 1.4f;
        }
    }

    oilSlicks_.erase(
        std::remove_if(
            oilSlicks_.begin(),
            oilSlicks_.end(),
            [](const OilSlick& slick)
            {
                return slick.z > kRemoveZ;
            }
        ),
        oilSlicks_.end()
    );
}

void Game::updatePowerUps(float dt)
{
    if (shieldTimer_ > 0.0f)
    {
        shieldTimer_ = std::max(0.0f, shieldTimer_ - dt);
    }

    if (magnetTimer_ > 0.0f)
    {
        magnetTimer_ = std::max(0.0f, magnetTimer_ - dt);
    }

    if (powerPopupTimer_ > 0.0f)
    {
        powerPopupTimer_ -= dt;
    }

    // новый бонус раз в 14-24 секунды, в случайной полосе
    powerSpawnTimer_ -= dt;

    if (powerSpawnTimer_ <= 0.0f)
    {
        std::uniform_real_distribution<float> delayDist(14.0f, 24.0f);
        std::uniform_int_distribution<int> kindDist(0, 2);

        powerSpawnTimer_ = delayDist(rng_);

        PowerUp power;

        power.lane = laneDist_(rng_);
        power.x = laneCenter(power.lane);
        power.z = kSpawnZ;
        power.type = static_cast<PowerType>(kindDist(rng_));

        powerUps_.push_back(power);
    }

    for (auto& power : powerUps_)
    {
        power.z += speed_ * dt;

        power.phase += dt * 3.0f;

        if (power.collected)
        {
            continue;
        }

        const bool xHit = std::abs(playerX_ - power.x) < 1.15f;

        const bool zHit = std::abs(playerZ_ - power.z) < 1.8f;

        if (xHit && zHit)
        {
            power.collected = true;

            switch (power.type)
            {
            case PowerType::Shield:
                shieldTimer_ = 12.0f;
                break;

            case PowerType::Magnet:
                magnetTimer_ = 10.0f;
                break;

            case PowerType::Nitro:
                boost_ = 100.0f;
                break;
            }

            powerPopupType_ = power.type;
            powerPopupTimer_ = 2.0f;

            scoreValue_ += 150.0f;

            audio_.playPower(static_cast<int>(power.type));
        }
    }

    powerUps_.erase(
        std::remove_if(
            powerUps_.begin(),
            powerUps_.end(),
            [](const PowerUp& power)
            {
                return power.collected || power.z > kRemoveZ;
            }
        ),
        powerUps_.end()
    );
}

const char* Game::missionName() const
{
    switch (missionType_)
    {
    case 0: return "COLLECT 15 COINS";
    case 1: return "REACH COMBO X10";
    case 2: return "ESCAPE POLICE";
    case 3: return "DRIVE 3000 M";
    default: return "";
    }
}

int Game::missionProgress() const
{
    switch (missionType_)
    {
    case 0: return coins_;
    case 1: return maxCombo_;
    case 2: return escapes_;
    case 3: return static_cast<int>(distance_);
    default: return 0;
    }
}

int Game::missionTarget() const
{
    switch (missionType_)
    {
    case 0: return 15;
    case 1: return 10;
    case 2: return 1;
    case 3: return 3000;
    default: return 1;
    }
}

void Game::updateMission(float dt)
{
    maxCombo_ = std::max(maxCombo_, combo_);

    if (missionPopupTimer_ > 0.0f)
    {
        missionPopupTimer_ -= dt;
    }

    if (!missionDone_ && missionProgress() >= missionTarget())
    {
        missionDone_ = true;
        missionPopupTimer_ = 3.0f;

        bank_ += 15;
        audio_.playAchievement();
    }
}

bool Game::trackUnlocked(int id) const
{
    if (id < 0 || id >= kTrackCount)
    {
        return false;
    }

    return level() >= kTrackLevels[id];
}

const char* Game::trackName(int id)
{
    switch (id)
    {
    case 0: return "HIGHWAY";
    case 1: return "DESERT";
    case 2: return "SNOW";
    default: return "";
    }
}

int Game::trackLevelReq(int id)
{
    return (id >= 0 && id < kTrackCount) ? kTrackLevels[id] : 1;
}

float Game::trackScoreMul(int id)
{
    return (id >= 0 && id < kTrackCount) ? kTrackScoreMul[id] : 1.0f;
}

bool Game::carUnlocked(int id) const
{
    if (id < 0 || id >= kCarCount)
    {
        return false;
    }

    return (carMask_ & (1u << id)) != 0u;
}

const char* Game::carName(int id)
{
    switch (id)
    {
    case 0: return "STREET FOX";
    case 1: return "CRIMSON GT";
    case 2: return "VORTEX R";
    case 3: return "PHANTOM V8";
    case 4: return "APEX X";
    case 5: return "HYPERION Z";
    default: return "";
    }
}

int Game::carCost(int id)
{
    return (id >= 0 && id < kCarCount) ? kCarCosts[id] : 0;
}

int Game::carLevelReq(int id)
{
    return (id >= 0 && id < kCarCount) ? kCarLevels[id] : 1;
}

float Game::carSpeed(int id)
{
    return (id >= 0 && id < kCarCount) ? kCarSpeed[id] : 1.0f;
}

float Game::carGrip(int id)
{
    return (id >= 0 && id < kCarCount) ? kCarGrip[id] : 1.0f;
}

float Game::carArmor(int id)
{
    return (id >= 0 && id < kCarCount) ? kCarArmor[id] : 1.0f;
}

// выбрать машину; закрытую покупает, если хватает уровня и монет
bool Game::applyCar(int id)
{
    if (id < 0 || id >= kCarCount)
    {
        return false;
    }

    if (!carUnlocked(id))
    {
        if (level() < kCarLevels[id] || bank_ < kCarCosts[id])
        {
            return false;
        }

        bank_ -= kCarCosts[id];
        carMask_ |= (1u << id);
    }

    selectedCar_ = id;

    saveGarage();

    checkAchievements();

    return true;
}

const char* Game::achName(int id)
{
    switch (id)
    {
    case 0: return "ROAD TRIP";
    case 1: return "COIN HUNTER";
    case 2: return "ESCAPE ARTIST";
    case 3: return "COMBO KING";
    case 4: return "SURVIVOR";
    case 5: return "SPEED DEMON";
    case 6: return "RISING STAR";
    case 7: return "COLLECTOR";
    default: return "";
    }
}

const char* Game::achInfo(int id)
{
    switch (id)
    {
    case 0: return "DRIVE 20 KM IN TOTAL";
    case 1: return "COLLECT 200 COINS IN TOTAL";
    case 2: return "ESCAPE POLICE 5 TIMES";
    case 3: return "REACH COMBO X15";
    case 4: return "DRIVE 5 KM IN ONE RUN";
    case 5: return "REACH 300 KM H";
    case 6: return "REACH LEVEL 5";
    case 7: return "OWN 3 CARS";
    default: return "";
    }
}

int Game::achTarget(int id)
{
    return (id >= 0 && id < kAchCount) ? kAchTargets[id] : 1;
}

int Game::achReward(int id)
{
    return (id >= 0 && id < kAchCount) ? kAchRewards[id] : 0;
}

// прогресс включает текущий забег, пока он ещё не записан в статистику
int Game::achProgress(int id) const
{
    const bool live = !runRecorded_;

    switch (id)
    {
    case 0: return totalDistance_ + (live ? static_cast<int>(distance_) : 0);
    case 1: return totalCoins_ + (live ? coins_ : 0);
    case 2: return totalEscapes_ + (live ? escapes_ : 0);
    case 3: return std::max(bestCombo_, live ? maxCombo_ : 0);
    case 4: return std::max(bestRunDist_, live ? static_cast<int>(distance_) : 0);
    case 5: return std::max(bestSpeed_, live ? static_cast<int>(runMaxSpeed_) : 0);
    case 6: return level();
    case 7:
    {
        int owned = 0;

        for (int i = 0; i < kCarCount; ++i)
        {
            if (carUnlocked(i))
            {
                ++owned;
            }
        }

        return owned;
    }
    default: return 0;
    }
}

bool Game::achUnlocked(int id) const
{
    if (id < 0 || id >= kAchCount)
    {
        return false;
    }

    return (achMask_ & (1u << id)) != 0u;
}

int Game::achUnlockedCount() const
{
    int count = 0;

    for (int i = 0; i < kAchCount; ++i)
    {
        if (achUnlocked(i))
        {
            ++count;
        }
    }

    return count;
}

void Game::checkAchievements()
{
    for (int i = 0; i < kAchCount; ++i)
    {
        if (achUnlocked(i) || achProgress(i) < kAchTargets[i])
        {
            continue;
        }

        achMask_ |= (1u << i);

        bank_ += kAchRewards[i];

        achPopupId_ = i;
        achPopupTimer_ = 3.5f;

        audio_.playAchievement();

        saveStats();
        saveGarage();
    }
}

void Game::loadStats()
{
    std::ifstream file("stats.txt");

    int dist = 0;
    int coins = 0;
    int escapes = 0;
    int combo = 0;
    int runDist = 0;
    int speed = 0;
    int mask = 0;

    file >> dist >> coins >> escapes >> combo >> runDist >> speed >> mask;

    totalDistance_ = std::max(0, dist);
    totalCoins_ = std::max(0, coins);
    totalEscapes_ = std::max(0, escapes);
    bestCombo_ = std::max(0, combo);
    bestRunDist_ = std::max(0, runDist);
    bestSpeed_ = std::max(0, speed);

    achMask_ = static_cast<unsigned int>(std::max(0, mask)) & ((1u << kAchCount) - 1u);
}

void Game::saveStats() const
{
    std::ofstream file("stats.txt", std::ios::trunc);

    file << totalDistance_ << ' ' << totalCoins_ << ' ' << totalEscapes_ << ' '
        << bestCombo_ << ' ' << bestRunDist_ << ' ' << bestSpeed_ << ' ' << achMask_;
}

void Game::levelState(int& lvl, int& rem) const
{
    lvl = 1;
    rem = std::max(0, totalXp_);

    while (rem >= xpNeeded(lvl))
    {
        rem -= xpNeeded(lvl);
        ++lvl;
    }
}

int Game::level() const
{
    int lvl = 1;
    int rem = 0;

    levelState(lvl, rem);

    return lvl;
}

int Game::xpInLevel() const
{
    int lvl = 1;
    int rem = 0;

    levelState(lvl, rem);

    return rem;
}

int Game::xpForNextLevel() const
{
    return xpNeeded(level());
}

void Game::recordRun()
{
    runRecorded_ = true;

    // опыт за забег: очки, монеты, побеги от полиции и задание
    {
        // статистика за всё время
        totalDistance_ += static_cast<int>(distance_);
        // Distance and skill bonuses are awarded once per completed run.
        bank_ += std::min(250, static_cast<int>(distance_ / 30.0f));
        bank_ += std::min(90, maxCombo_ * 6);
        bank_ += escapes_ * 25;
        if (missionDone_) bank_ += 60;
        totalCoins_ += coins_;
        totalEscapes_ += escapes_;
        bestCombo_ = std::max(bestCombo_, maxCombo_);
        bestRunDist_ = std::max(bestRunDist_, static_cast<int>(distance_));
        bestSpeed_ = std::max(bestSpeed_, static_cast<int>(runMaxSpeed_));

        const int before = level();

        lastRunXp_ = score_ / 25 + coins_ * 3 + escapes_ * 40 + (missionDone_ ? 60 : 0);

        totalXp_ += lastRunXp_;

        const int after = level();

        lastLevelUps_ = after - before;

        // награда за каждый новый уровень: 10 монет за номер уровня
        for (int l = before + 1; l <= after; ++l)
        {
            bank_ += 10 * l;
        }

        checkAchievements();

        saveStats();

        saveGarage();
    }

    if (score_ <= 0)
    {
        return;
    }

    int best[kTopCount + 1];

    for (int i = 0; i < kTopCount; ++i)
    {
        best[i] = topScores_[i];
    }

    best[kTopCount] = score_;

    std::sort(best, best + kTopCount + 1, [](int a, int b) { return a > b; });

    for (int i = 0; i < kTopCount; ++i)
    {
        topScores_[i] = best[i];
    }

    saveTop();
}

void Game::loadTop()
{
    std::ifstream file("scores.txt");

    for (int i = 0; i < kTopCount; ++i)
    {
        int value = 0;

        if (file >> value)
        {
            topScores_[i] = std::max(0, value);
        }
        else
        {
            topScores_[i] = 0;
        }
    }

    std::sort(topScores_, topScores_ + kTopCount, [](int a, int b) { return a > b; });
}

void Game::saveTop() const
{
    std::ofstream file("scores.txt", std::ios::trunc);

    for (int i = 0; i < kTopCount; ++i)
    {
        file << topScores_[i] << ' ';
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