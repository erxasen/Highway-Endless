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

enum class GameMode {
    SinglePlayer,
    TwoPlayerBattle
};

enum class GameState {
    Running,
    Paused,
    GameOver,
    Garage,     // меню улучшений (открывается клавишей G на паузе, после аварии и в главном меню)
    Menu,       // главное меню при запуске игры
    Achievements, // список достижений (клавиша H на паузе, после аварии и в главном меню)
    Settings     // настройки звука и эффектов (клавиша O на паузе, после аварии и в главном меню)
};

constexpr int kUpgradeCount = 4;
constexpr int kMaxUpgradeLevel = 5;

// таблица лучших забегов
constexpr int kTopCount = 5;

// Покраска машины: 6 цветов, первый (BLUE) бесплатный
constexpr int kPaintCount = 6;

// машины игрока: SPORT (бесплатная), COUPE, SEDAN, VAN
constexpr int kCarCount = 6;

// достижения
constexpr int kAchCount = 8;

// трассы: HIGHWAY, DESERT, SNOW
constexpr int kTrackCount = 3;

// настройки: 0 общая громкость, 1 двигатель, 2 эффекты, 3 окружение (дождь, ветер, буст),
// 4 тряска камеры, 5 эффекты скорости; каждое значение 0..10
constexpr int kSettingCount = 6;
constexpr int kSettingMax = 10;

enum class TrafficType {
    Sedan,
    Sport,
    Truck,
    Van,
    Bus,
    Coupe
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

// дорожные работы: барьер с конусами, стоит в одной полосе
struct RoadBlock {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;

    bool hit = false;
    bool scored = false;
};

// масляное пятно: без урона, но машину заносит, пока вы на нём
struct OilSlick {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;
};

struct CoinPickup {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;

    float phase = 0.0f;

    bool collected = false;
};

// бонусы на дороге: щит гасит один удар, магнит притягивает монеты, нитро заполняет буст
enum class PowerType {
    Shield,
    Magnet,
    Nitro
};

struct PowerUp {
    int lane = 1;

    float x = 0.0f;
    float z = 0.0f;

    float phase = 0.0f;

    PowerType type = PowerType::Shield;

    bool collected = false;
};

class Game {
public:
    Game();
    ~Game();

    void handleInput(GLFWwindow* window, float dt);

    void update(float dt);

    void reset();

    GameMode gameMode() const { return gameMode_; }
    GameMode selectedGameMode() const { return selectedGameMode_; }
    bool multiplayerPending() const { return multiplayerPending_; }
    float player2X() const { return player2X_; }
    float player2Z() const { return player2Z_; }
    float player2Yaw() const { return player2Yaw_; }
    float player2SpeedKmh() const { return player2Speed_ * 7.2f; }
    float player2Distance() const { return player2Distance_; }
    float player2Boost() const { return player2Boost_; }
    float player2Health() const { return player2Health_; }

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


    bool timeWarpActive() const {
        return timeWarpTimer_ > 0.0f;
    }

    float timeWarpRemaining() const {
        return timeWarpTimer_;
    }

    float timeWarpCooldownRemaining() const {
        return timeWarpCooldown_;
    }

    float timeWarpDuration() const {
        return kTimeWarpDuration;
    }

    float timeWarpRechargeDuration() const {
        return kTimeWarpCooldown;
    }


    // отметка пройденного пути (метры), за которую дали бонус; для надписи на экране
    int milestoneMeters() const {
        return milestoneMeters_;
    }

    // сколько секунд ещё показывать надпись о бонусе за пройденный путь
    float milestoneTimer() const {
        return milestoneTimer_;
    }

    // ---- Задание на забег: выполнил, получил монеты в гараж ----
    const char* missionName() const;

    int missionProgress() const;

    int missionTarget() const;

    bool missionDone() const {
        return missionDone_;
    }

    // сколько секунд ещё показывать «MISSION COMPLETE»
    float missionPopupTimer() const {
        return missionPopupTimer_;
    }

    // игрок сейчас тормозит (для стоп-сигналов)
    bool braking() const {
        return brake_ > 0.5f && state_ == GameState::Running;
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

    const std::vector<RoadBlock>& roadBlocks() const {
        return roadBlocks_;
    }

    const std::vector<OilSlick>& oilSlicks() const {
        return oilSlicks_;
    }

    // true, пока машину заносит после масляного пятна
    bool oilActive() const {
        return oilTimer_ > 0.0f;
    }

    // ---- Бонусы на дороге ----
    const std::vector<PowerUp>& powerUps() const {
        return powerUps_;
    }

    // сколько секунд ещё действует щит (0 = нет)
    float shieldTimer() const {
        return shieldTimer_;
    }

    // сколько секунд ещё действует магнит (0 = нет)
    float magnetTimer() const {
        return magnetTimer_;
    }

    // надпись о только что подобранном бонусе
    float powerPopupTimer() const {
        return powerPopupTimer_;
    }

    PowerType powerPopupType() const {
        return powerPopupType_;
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
    int garageTab() const { return garageTab_; }

    int bank() const {
        return bank_;
    }

    int upgradeLevel(int id) const;

    // цена следующего уровня, -1 если уже максимум
    int upgradeCost(int id) const;

    static const char* upgradeName(int id);

    static const char* upgradeInfo(int id);

    // ---- Таблица рекордов ----
    int topScore(int index) const {
        if (index < 0 || index >= kTopCount)
        {
            return 0;
        }

        return topScores_[index];
    }

    // ---- Машины игрока ----
    int selectedCar() const {
        return selectedCar_;
    }

    // на какой машине стоит курсор в гараже (она же показана в превью)
    int carCursor() const {
        return carCursor_;
    }

    bool carUnlocked(int id) const;

    static const char* carName(int id);

    static int carCost(int id);

    // с какого уровня игрока машину можно купить
    static int carLevelReq(int id);

    // множитель максимальной скорости, сцепления и получаемого урона
    static float carSpeed(int id);

    static float carGrip(int id);

    static float carArmor(int id);

    // ---- Трассы ----
    int track() const {
        return selectedTrack_;
    }

    bool trackUnlocked(int id) const;

    static const char* trackName(int id);

    static int trackLevelReq(int id);

    // множитель очков на трассе
    static float trackScoreMul(int id);

    // ---- Достижения ----
    static const char* achName(int id);

    static const char* achInfo(int id);

    static int achTarget(int id);

    static int achReward(int id);

    int achProgress(int id) const;

    bool achUnlocked(int id) const;

    int achUnlockedCount() const;

    // надпись о только что полученном достижении
    float achPopupTimer() const {
        return achPopupTimer_;
    }

    int achPopupId() const {
        return achPopupId_;
    }

    // ---- Уровень игрока ----
    int level() const;

    // опыт внутри текущего уровня и сколько нужно до следующего
    int xpInLevel() const;

    int xpForNextLevel() const;

    // опыт и новые уровни за последний забег (для экрана аварии)
    int lastRunXp() const {
        return lastRunXp_;
    }

    int lastRunLevelUps() const {
        return lastLevelUps_;
    }

    // ---- Покраска ----
    // выбранный цвет машины игрока
    int selectedPaint() const {
        return selectedPaint_;
    }

    // на каком цвете стоит курсор в гараже
    int paintCursor() const {
        return paintCursor_;
    }

    bool paintUnlocked(int id) const;

    static int paintCost(int id);

    static const char* paintName(int id);

    static Color paintColor(int id);

    // ---- Настройки ----
    int settingValue(int id) const;

    // на какой строке стоит курсор в настройках
    int settingsCursor() const {
        return settingsCursor_;
    }

    static const char* settingName(int id);

    static const char* settingInfo(int id);

    // множители 0..1 для графики
    float shakeScale() const {
        return static_cast<float>(settings_[4]) / static_cast<float>(kSettingMax);
    }

    float speedFxScale() const {
        return static_cast<float>(settings_[5]) / static_cast<float>(kSettingMax);
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

    void updateRoadBlocks(float dt);

    void updateOilSlicks(float dt);

    void updatePowerUps(float dt);

    void updateMission(float dt);

    void checkCollisions();

    void updateDifficulty(float dt);

    void updateChase(float dt);

    bool buyUpgrade(int id);

    // выбрать цвет; если он закрыт и хватает монет, покупает
    bool applyPaint(int id);

    void recordRun();

    void levelState(int& lvl, int& rem) const;

    bool applyCar(int id);

    void checkAchievements();

    void loadStats();

    void saveStats() const;

    void loadTop();

    void saveTop() const;

    void loadGarage();

    void saveGarage() const;

    void loadSettings();

    void saveSettings() const;

    void applySettings();

    void changeSetting(int id, int delta);

    void awardNearMiss(TrafficVehicle& vehicle);

    void updateHighScore();

    int loadHighScore() const;

    void saveHighScore(int value) const;

private:
    GameMode gameMode_ = GameMode::SinglePlayer;
    GameMode selectedGameMode_ = GameMode::SinglePlayer;
    bool multiplayerPending_ = false;
    float player2X_ = 3.35f;
    float player2Z_ = 4.0f;
    float player2Yaw_ = 0.0f;
    float player2Speed_ = 28.0f;
    float player2Distance_ = 0.0f;
    float player2Boost_ = 100.0f;
    float player2Health_ = 100.0f;
    float player2Invulnerability_ = 0.0f;
    int player2Lane_ = 2;
    bool player2Throttle_ = false;
    bool player2Brake_ = false;
    bool player2Nitro_ = false;
    unsigned int trafficSpawnSequence_ = 0;
    bool player2LeftWasDown_ = false;
    bool player2RightWasDown_ = false;
    bool mode1WasDown_ = false;
    bool mode2WasDown_ = false;
    bool menuMouseWasDown_ = false;
    bool escapeWasDown_ = false;

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


    float timeWarpTimer_ = 0.0f;
    float timeWarpCooldown_ = 0.0f;
    bool timeWarpKeyWasDown_ = false;

    static constexpr float kTimeWarpDuration = 4.0f;
    static constexpr float kTimeWarpCooldown = 12.0f;
    static constexpr float kTimeWarpFactor = 0.05f;


    bool boosting_ = false;

    float health_ = 100.0f;

    float invulnerabilityTimer_ = 0.0f;

    float worldTime_ = 0.0f;

    float roadOffset_ = 0.0f;

    float distance_ = 0.0f;

    // бонус монетами за каждые 2000 м пути
    float nextMilestone_ = 2000.0f;

    int milestoneMeters_ = 0;

    float milestoneTimer_ = 0.0f;

    float scoreValue_ = 0.0f;

    int score_ = 0;

    int highScore_ = 0;

    int topScores_[kTopCount] = { 0, 0, 0, 0, 0 };

    bool runRecorded_ = false;

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

    // весь накопленный опыт; уровень считается из него
    int totalXp_ = 0;

    int lastRunXp_ = 0;

    int lastLevelUps_ = 0;

    // выбранная машина и маска купленных (бит 0 = SPORT, всегда куплена)
    int selectedCar_ = 0;

    unsigned int carMask_ = 1u;

    int carCursor_ = 0;
    int garageTab_ = 0;

    // выбранная трасса (открывается по уровню игрока)
    int selectedTrack_ = 0;

    bool trackKeyWasDown_ = false;

    // статистика за всё время (файл stats.txt) и достижения
    int totalDistance_ = 0;

    int totalCoins_ = 0;

    int totalEscapes_ = 0;

    int bestCombo_ = 0;

    int bestRunDist_ = 0;

    int bestSpeed_ = 0;

    unsigned int achMask_ = 0u;

    float runMaxSpeed_ = 0.0f;

    float achPopupTimer_ = 0.0f;

    int achPopupId_ = 0;

    GameState achReturn_ = GameState::Menu;

    bool achKeyWasDown_ = false;

    bool carPrevWasDown_ = false;

    bool carNextWasDown_ = false;

    bool carApplyWasDown_ = false;

    int upgrades_[kUpgradeCount] = { 0, 0, 0, 0 };

    // покраска: выбранный цвет и маска купленных (бит 0 = BLUE, всегда куплен)
    int selectedPaint_ = 0;

    unsigned int paintMask_ = 1u;

    int paintCursor_ = 0;

    GameState garageReturn_ = GameState::Paused;

    bool garageWasDown_ = false;

    bool upgradeKeyWasDown_[kUpgradeCount] = { false, false, false, false };

    bool paintLeftWasDown_ = false;

    bool paintRightWasDown_ = false;

    bool paintApplyWasDown_ = false;

    int settings_[kSettingCount] = { 10, 10, 10, 10, 10, 10 };

    int settingsCursor_ = 0;

    GameState settingsReturn_ = GameState::Menu;

    bool settingsKeyWasDown_ = false;

    bool setUpWasDown_ = false;

    bool setDownWasDown_ = false;

    bool setLeftWasDown_ = false;

    bool setRightWasDown_ = false;

    std::vector<TrafficVehicle>
        traffic_;

    std::vector<CoinPickup>
        pickups_;

    std::vector<RoadBlock>
        roadBlocks_;

    float roadBlockTimer_ = 18.0f;

    std::vector<OilSlick>
        oilSlicks_;

    float oilSpawnTimer_ = 25.0f;

    float oilTimer_ = 0.0f;

    std::vector<PowerUp>
        powerUps_;

    float powerSpawnTimer_ = 14.0f;

    float shieldTimer_ = 0.0f;

    float magnetTimer_ = 0.0f;

    float powerPopupTimer_ = 0.0f;

    PowerType powerPopupType_ = PowerType::Shield;

    // задание: 0 монеты, 1 комбо, 2 побег от полиции, 3 дистанция
    int missionType_ = 0;

    bool missionDone_ = false;

    float missionPopupTimer_ = 0.0f;

    int maxCombo_ = 0;

    int escapes_ = 0;

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