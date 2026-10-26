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

    constexpr float kRoadMarkerSpacing = 12.0f;

    constexpr float kPlayerWidth = 1.55f;

    constexpr float kPlayerLength = 3.35f;

    constexpr float kSpawnZ = -150.0f;

    constexpr float kRemoveZ = 24.0f;

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

    float approach(
        float current,
        float target,
        float speed,
        float dt
    ) {
        const float t =
            std::clamp(
                speed * dt,
                0.0f,
                1.0f
            );

        return current +
            (target - current) * t;
    }

    bool overlaps(
        float aCenter,
        float aSize,
        float bCenter,
        float bSize
    ) {
        return std::abs(
            aCenter - bCenter
        ) < (aSize + bSize) * 0.5f;
    }

}

Game::Game()
    :
    highScore_(loadHighScore()),
    rng_(
        static_cast<unsigned int>(
            std::time(nullptr)
            )
    ),
    laneDist_(0, 2),
    colorDist_(
        0,
        static_cast<int>(
            kTrafficColors.size() - 1
            )
    ),
    typeDist_(0, 3)
{
    reset();
}

Game::~Game()
{
    saveHighScore(
        highScore_
    );
}

void Game::reset()
{
    state_ =
        GameState::Running;

    playerX_ =
        laneCenter(1);

    playerZ_ =
        4.0f;

    playerYaw_ =
        0.0f;

    lateralVelocity_ =
        0.0f;

    targetLane_ =
        1;

    speed_ =
        28.0f;

    throttle_ =
        0.0f;

    brake_ =
        0.0f;

    boost_ =
        100.0f;

    boosting_ =
        false;

    health_ =
        100.0f;

    invulnerabilityTimer_ =
        0.0f;

    worldTime_ =
        0.0f;

    roadOffset_ =
        0.0f;

    scoreValue_ =
        0.0f;

    score_ =
        0;

    coins_ =
        0;

    combo_ =
        0;

    comboTimer_ =
        0.0f;

    trafficTimer_ =
        0.35f;

    trafficInterval_ =
        1.15f;

    coinTimer_ =
        1.2f;

    coinInterval_ =
        4.0f;

    difficulty_ =
        1.0f;

    traffic_.clear();

    pickups_.clear();
}

float Game::laneCenter(
    int lane
)
{
    return (
        static_cast<float>(lane) - 1.0f
        ) * kLaneWidth;
}

bool Game::pressedOnce(
    GLFWwindow* window,
    int key,
    bool& previous
)
{
    const bool down =
        glfwGetKey(
            window,
            key
        ) == GLFW_PRESS;

    const bool result =
        down && !previous;

    previous =
        down;

    return result;
}

void Game::handleInput(
    GLFWwindow* window,
    float dt
)
{
    if (
        glfwGetKey(
            window,
            GLFW_KEY_ESCAPE
        ) == GLFW_PRESS
        )
    {
        glfwSetWindowShouldClose(
            window,
            GLFW_TRUE
        );
    }

    if (
        pressedOnce(
            window,
            GLFW_KEY_P,
            pauseWasDown_
        )
        )
    {
        if (
            state_ ==
            GameState::Running
            )
        {
            state_ =
                GameState::Paused;
        }
        else if (
            state_ ==
            GameState::Paused
            )
        {
            state_ =
                GameState::Running;
        }
    }

    if (
        pressedOnce(
            window,
            GLFW_KEY_R,
            restartWasDown_
        )
        )
    {
        reset();
    }

    if (
        state_ !=
        GameState::Running
        )
    {
        return;
    }

    const bool leftPressed =
        pressedOnce(
            window,
            GLFW_KEY_LEFT,
            leftWasDown_
        ) ||
        pressedOnce(
            window,
            GLFW_KEY_A,
            aWasDown_
        );

    const bool rightPressed =
        pressedOnce(
            window,
            GLFW_KEY_RIGHT,
            rightWasDown_
        ) ||
        pressedOnce(
            window,
            GLFW_KEY_D,
            dWasDown_
        );

    if (
        leftPressed &&
        !rightPressed
        )
    {
        changeLane(-1);
    }

    if (
        rightPressed &&
        !leftPressed
        )
    {
        changeLane(1);
    }

    throttle_ =
        (
            glfwGetKey(
                window,
                GLFW_KEY_W
            ) == GLFW_PRESS ||
            glfwGetKey(
                window,
                GLFW_KEY_UP
            ) == GLFW_PRESS
            )
        ? 1.0f
        : 0.0f;

    brake_ =
        (
            glfwGetKey(
                window,
                GLFW_KEY_S
            ) == GLFW_PRESS ||
            glfwGetKey(
                window,
                GLFW_KEY_DOWN
            ) == GLFW_PRESS
            )
        ? 1.0f
        : 0.0f;

    const bool boostPressed =
        glfwGetKey(
            window,
            GLFW_KEY_LEFT_SHIFT
        ) == GLFW_PRESS ||
        glfwGetKey(
            window,
            GLFW_KEY_RIGHT_SHIFT
        ) == GLFW_PRESS;

    boosting_ =
        boostPressed &&
        boost_ > 0.5f &&
        speed_ > 20.0f;

    if (boosting_)
    {
        boost_ =
            std::max(
                0.0f,
                boost_ -
                31.0f * dt
            );
    }
    else
    {
        boost_ =
            std::min(
                100.0f,
                boost_ +
                9.0f * dt
            );
    }
}

void Game::changeLane(
    int direction
)
{
    targetLane_ =
        std::clamp(
            targetLane_ +
            direction,
            0,
            2
        );
}

void Game::update(
    float dt
)
{
    if (
        state_ !=
        GameState::Running
        )
    {
        return;
    }

    worldTime_ += dt;

    if (
        invulnerabilityTimer_ >
        0.0f
        )
    {
        invulnerabilityTimer_ -=
            dt;
    }

    if (
        comboTimer_ >
        0.0f
        )
    {
        comboTimer_ -=
            dt;

        if (
            comboTimer_ <=
            0.0f
            )
        {
            combo_ =
                0;
        }
    }

    updateDifficulty(dt);

    const float targetCruise =
        baseCruiseSpeed_ +
        difficulty_ * 2.1f;

    const float boostExtra =
        boosting_
        ? 18.0f
        : 0.0f;

    float targetSpeed =
        targetCruise +
        throttle_ * 12.0f +
        boostExtra;

    targetSpeed -=
        brake_ * 22.0f;

    const float currentMaxSpeed =
        maxSpeed_ +
        (
            boosting_
            ? 15.0f
            : 0.0f
            );

    targetSpeed =
        std::clamp(
            targetSpeed,
            14.0f,
            currentMaxSpeed
        );

    const float response =
        targetSpeed > speed_
        ? 2.2f
        : 4.8f;

    speed_ =
        approach(
            speed_,
            targetSpeed,
            response,
            dt
        );

    const float targetX =
        laneCenter(
            targetLane_
        );

    const float oldX =
        playerX_;

    playerX_ =
        approach(
            playerX_,
            targetX,
            8.5f,
            dt
        );

    lateralVelocity_ =
        (
            playerX_ -
            oldX
            ) /
        std::max(
            dt,
            0.0001f
        );

    playerYaw_ =
        approach(
            playerYaw_,
            std::clamp(
                -lateralVelocity_ *
                1.35f,
                -12.0f,
                12.0f
            ),
            8.0f,
            dt
        );

    roadOffset_ +=
        speed_ * dt;

    while (
        roadOffset_ >=
        kRoadMarkerSpacing
        )
    {
        roadOffset_ -=
            kRoadMarkerSpacing;
    }

    scoreValue_ +=
        speed_ *
        dt *
        (
            7.2f +
            static_cast<float>(
                combo_
                ) * 0.55f
            );

    score_ =
        static_cast<int>(
            scoreValue_
            ) +
        coins_ * 120;

    updateHighScore();

    trafficTimer_ +=
        dt;

    if (
        trafficTimer_ >=
        trafficInterval_
        )
    {
        spawnTraffic();

        trafficTimer_ =
            0.0f;
    }

    coinTimer_ +=
        dt;

    if (
        coinTimer_ >=
        coinInterval_
        )
    {
        spawnCoinLine();

        coinTimer_ =
            0.0f;
    }

    updateTraffic(dt);

    updatePickups(dt);

    checkCollisions();
}

void Game::updateDifficulty(
    float
)
{
    difficulty_ =
        std::min(
            8.0f,
            1.0f +
            worldTime_ /
            32.0f
        );

    trafficInterval_ =
        std::max(
            0.42f,
            1.18f -
            difficulty_ *
            0.085f
        );

    coinInterval_ =
        std::max(
            2.5f,
            4.3f -
            difficulty_ *
            0.12f
        );
}

void Game::spawnTraffic()
{
    int lane =
        laneDist_(rng_);

    for (
        int attempt = 0;
        attempt < 6;
        ++attempt
        )
    {
        bool blocked =
            false;

        for (
            const auto& vehicle :
            traffic_
            )
        {
            if (
                vehicle.lane ==
                lane &&
                vehicle.z <
                -105.0f
                )
            {
                blocked =
                    true;

                break;
            }
        }

        if (!blocked)
        {
            break;
        }

        lane =
            (lane + 1) % 3;
    }

    TrafficVehicle vehicle;

    vehicle.lane =
        lane;

    vehicle.x =
        laneCenter(lane);

    vehicle.z =
        kSpawnZ;

    vehicle.color =
        kTrafficColors[
            colorDist_(rng_)
        ];

    const int type =
        typeDist_(rng_);

    vehicle.type =
        static_cast<
        TrafficType
        >(type);

    std::uniform_real_distribution<float>
        speedOffsetDist(
            -4.0f,
            8.0f
        );

    vehicle.relativeSpeed =
        speedOffsetDist(rng_);

    switch (
        vehicle.type
        )
    {
    case TrafficType::Sedan:
        vehicle.width =
            1.48f;

        vehicle.length =
            3.35f;

        vehicle.height =
            1.05f;

        break;

    case TrafficType::Sport:
        vehicle.width =
            1.62f;

        vehicle.length =
            3.7f;

        vehicle.height =
            0.88f;

        vehicle.relativeSpeed +=
            4.0f;

        break;

    case TrafficType::Truck:
        vehicle.width =
            2.2f;

        vehicle.length =
            6.1f;

        vehicle.height =
            2.25f;

        vehicle.relativeSpeed -=
            4.0f;

        break;

    case TrafficType::Van:
        vehicle.width =
            1.9f;

        vehicle.length =
            4.6f;

        vehicle.height =
            1.8f;

        vehicle.relativeSpeed -=
            1.0f;

        break;
    }

    std::uniform_real_distribution<float>
        driftDist(
            -0.08f,
            0.08f
        );

    vehicle.drift =
        driftDist(rng_);

    traffic_.push_back(
        vehicle
    );
}

void Game::spawnCoinLine()
{
    int lane =
        laneDist_(rng_);

    std::uniform_int_distribution<int>
        countDist(
            3,
            6
        );

    const int count =
        countDist(rng_);

    for (
        int i = 0;
        i < count;
        ++i
        )
    {
        CoinPickup coin;

        coin.lane =
            lane;

        coin.x =
            laneCenter(lane);

        coin.z =
            -85.0f -
            static_cast<float>(
                i
                ) * 8.0f;

        coin.phase =
            static_cast<float>(
                i
                ) * 0.7f;

        pickups_.push_back(
            coin
        );
    }
}

void Game::updateTraffic(
    float dt
)
{
    for (
        auto& vehicle :
        traffic_
        )
    {
        vehicle.z +=
            (
                speed_ +
                vehicle.relativeSpeed
                ) * dt;

        const float desiredX =
            laneCenter(
                vehicle.lane
            );

        vehicle.x =
            approach(
                vehicle.x,
                desiredX,
                2.5f,
                dt
            );

        vehicle.x +=
            std::sin(
                worldTime_ *
                0.8f +
                vehicle.z *
                0.03f
            ) *
            vehicle.drift;

        if (
            !vehicle.scored &&
            vehicle.z >
            playerZ_ +
            4.0f
            )
        {
            vehicle.scored =
                true;

            scoreValue_ +=
                60.0f;
        }

        awardNearMiss(
            vehicle
        );
    }

    traffic_.erase(
        std::remove_if(
            traffic_.begin(),
            traffic_.end(),
            [](
                const TrafficVehicle&
                vehicle
                )
            {
                return
                    vehicle.z >
                    kRemoveZ;
            }
        ),
        traffic_.end()
    );
}

void Game::updatePickups(
    float dt
)
{
    for (
        auto& coin :
        pickups_
        )
    {
        coin.z +=
            speed_ * dt;

        coin.phase +=
            dt * 4.0f;

        if (
            !coin.collected
            )
        {
            const bool xHit =
                std::abs(
                    playerX_ -
                    coin.x
                ) < 0.95f;

            const bool zHit =
                std::abs(
                    playerZ_ -
                    coin.z
                ) < 1.5f;

            if (
                xHit &&
                zHit
                )
            {
                coin.collected =
                    true;

                ++coins_;

                boost_ =
                    std::min(
                        100.0f,
                        boost_ +
                        12.0f
                    );

                combo_ =
                    std::min(
                        combo_ + 1,
                        15
                    );

                comboTimer_ =
                    3.2f;

                scoreValue_ +=
                    120.0f +
                    static_cast<float>(
                        combo_
                        ) *
                    25.0f;
            }
        }
    }

    pickups_.erase(
        std::remove_if(
            pickups_.begin(),
            pickups_.end(),
            [](
                const CoinPickup&
                coin
                )
            {
                return
                    coin.collected ||
                    coin.z >
                    kRemoveZ;
            }
        ),
        pickups_.end()
    );
}

void Game::awardNearMiss(
    TrafficVehicle&
    vehicle
)
{
    if (
        vehicle.nearMissAwarded
        )
    {
        return;
    }

    const float dz =
        std::abs(
            vehicle.z -
            playerZ_
        );

    const float dx =
        std::abs(
            vehicle.x -
            playerX_
        );

    const float dangerousWidth =
        (
            vehicle.width +
            kPlayerWidth
            ) * 0.5f +
        0.9f;

    const float collisionWidth =
        (
            vehicle.width +
            kPlayerWidth
            ) * 0.5f *
        0.72f;

    if (
        dz < 1.4f &&
        dx < dangerousWidth &&
        dx > collisionWidth
        )
    {
        vehicle.nearMissAwarded =
            true;

        combo_ =
            std::min(
                combo_ + 1,
                15
            );

        comboTimer_ =
            3.4f;

        boost_ =
            std::min(
                100.0f,
                boost_ +
                8.0f
            );

        scoreValue_ +=
            180.0f +
            static_cast<float>(
                combo_
                ) *
            40.0f;
    }
}

void Game::checkCollisions()
{
    if (
        invulnerabilityTimer_ >
        0.0f
        )
    {
        return;
    }

    for (
        auto& vehicle :
        traffic_
        )
    {
        const float widthFactor =
            vehicle.type ==
            TrafficType::Truck
            ? 0.76f
            : 0.72f;

        const float lengthFactor =
            vehicle.type ==
            TrafficType::Truck
            ? 0.78f
            : 0.70f;

        const bool sameLaneSpace =
            overlaps(
                playerX_,
                kPlayerWidth *
                0.76f,
                vehicle.x,
                vehicle.width *
                widthFactor
            );

        const bool sameDepthSpace =
            overlaps(
                playerZ_,
                kPlayerLength *
                0.72f,
                vehicle.z,
                vehicle.length *
                lengthFactor
            );

        if (
            sameLaneSpace &&
            sameDepthSpace
            )
        {
            float damage =
                34.0f;

            if (
                vehicle.type ==
                TrafficType::Truck
                )
            {
                damage =
                    55.0f;
            }
            else if (
                vehicle.type ==
                TrafficType::Van
                )
            {
                damage =
                    42.0f;
            }

            health_ =
                std::max(
                    0.0f,
                    health_ -
                    damage
                );

            speed_ *=
                0.54f;

            boost_ =
                std::max(
                    0.0f,
                    boost_ -
                    28.0f
                );

            combo_ =
                0;

            comboTimer_ =
                0.0f;

            invulnerabilityTimer_ =
                1.15f;

            vehicle.z +=
                7.0f;

            if (
                health_ <=
                0.0f
                )
            {
                state_ =
                    GameState::GameOver;

                updateHighScore();

                saveHighScore(
                    highScore_
                );
            }

            break;
        }
    }
}

void Game::updateHighScore()
{
    if (
        score_ >
        highScore_
        )
    {
        highScore_ =
            score_;
    }
}

int Game::loadHighScore() const
{
    std::ifstream file(
        "highscore.txt"
    );

    int value =
        0;

    if (
        file >>
        value
        )
    {
        return
            std::max(
                0,
                value
            );
    }

    return 0;
}

void Game::saveHighScore(
    int value
) const
{
    std::ofstream file(
        "highscore.txt",
        std::ios::trunc
    );

    file <<
        value;
}