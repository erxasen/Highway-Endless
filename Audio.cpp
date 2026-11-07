#include "Audio.hpp"

// Если miniaudio.h лежит рядом, работает настоящий звук.
// Если файла нет, собирается пустая заглушка и игра идёт без звука.
#if defined(__has_include)
#if __has_include("miniaudio.h")
#define GAME_HAS_MINIAUDIO 1
#endif
#endif

#ifdef GAME_HAS_MINIAUDIO

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

struct AudioState {
    ma_device device{};
    bool started = false;
    float sampleRate = 44100.0f;

    // параметры из игрового потока (читаются в звуковом потоке)
    std::atomic<float> targetHz{ 45.0f };
    std::atomic<float> targetEngine{ 0.0f };
    std::atomic<float> targetRain{ 0.0f };
    std::atomic<float> targetWind{ 0.0f };
    std::atomic<float> targetBoost{ 0.0f };
    std::atomic<float> targetTunnel{ 0.0f };
    std::atomic<float> targetMaster{ 0.0f };
    std::atomic<int> crashTrigger{ 0 };
    std::atomic<int> coinTrigger{ 0 };

    // состояние звукового потока
    float curHz = 45.0f;
    float curEngine = 0.0f;
    float curRain = 0.0f;
    float curWind = 0.0f;
    float curBoost = 0.0f;
    float curTunnel = 0.0f;
    float curMaster = 0.0f;

    float phase = 0.0f;
    float phase2 = 0.0f;
    float phaseSub = 0.0f;
    float engLP = 0.0f;

    float rainLP = 0.0f;
    float windLP = 0.0f;
    float boostLP = 0.0f;

    float crashEnv = 0.0f;
    float crashPhase = 0.0f;
    float coinEnv = 0.0f;
    float coinAge = 0.0f;
    float coinPhase = 0.0f;

    int seenCrash = 0;
    int seenCoin = 0;

    std::uint32_t seed = 22222u;

    std::vector<float> delay;
    std::size_t delayPos = 0;
};

namespace {

    constexpr float kTwoPi = 6.28318530717958647692f;

    float whiteNoise(AudioState& s) {
        s.seed = s.seed * 1664525u + 1013904223u;
        return static_cast<float>(s.seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        AudioState& s = *static_cast<AudioState*>(device->pUserData);
        float* out = static_cast<float*>(output);

        const float sr = s.sampleRate;

        const float tHz = s.targetHz.load();
        const float tEngine = s.targetEngine.load();
        const float tRain = s.targetRain.load();
        const float tWind = s.targetWind.load();
        const float tBoost = s.targetBoost.load();
        const float tTunnel = s.targetTunnel.load();
        const float tMaster = s.targetMaster.load();

        const int crashNow = s.crashTrigger.load();
        if (crashNow != s.seenCrash) {
            s.seenCrash = crashNow;
            s.crashEnv = 1.0f;
        }

        const int coinNow = s.coinTrigger.load();
        if (coinNow != s.seenCoin) {
            s.seenCoin = coinNow;
            s.coinEnv = 1.0f;
            s.coinAge = 0.0f;
        }

        const std::size_t delaySize = s.delay.size();
        const std::size_t delayTap = static_cast<std::size_t>(sr * 0.070f);

        for (ma_uint32 i = 0; i < frames; ++i) {
            // плавное движение параметров, чтобы не было щелчков
            s.curHz += (tHz - s.curHz) * 0.0008f;
            s.curEngine += (tEngine - s.curEngine) * 0.0008f;
            s.curRain += (tRain - s.curRain) * 0.0004f;
            s.curWind += (tWind - s.curWind) * 0.0004f;
            s.curBoost += (tBoost - s.curBoost) * 0.0010f;
            s.curTunnel += (tTunnel - s.curTunnel) * 0.0006f;
            s.curMaster += (tMaster - s.curMaster) * 0.0006f;

            // двигатель: пила + прямоугольник + низкий тон, срезано фильтром
            s.phase += s.curHz / sr;
            s.phase -= std::floor(s.phase);
            s.phase2 += s.curHz * 2.003f / sr;
            s.phase2 -= std::floor(s.phase2);
            s.phaseSub += s.curHz * 0.5f / sr;
            s.phaseSub -= std::floor(s.phaseSub);

            const float saw = 2.0f * s.phase - 1.0f;
            const float square = s.phase2 < 0.5f ? 1.0f : -1.0f;
            const float sub = std::sin(kTwoPi * s.phaseSub);

            const float rawEngine = saw * 0.55f + square * 0.22f + sub * 0.45f;
            s.engLP += (rawEngine - s.engLP) * 0.16f;
            const float engine = s.engLP * s.curEngine * 0.32f;

            const float white = whiteNoise(s);

            // дождь: шипение (шум без низких частот)
            s.rainLP += (white - s.rainLP) * 0.30f;
            const float rain = (white - s.rainLP) * s.curRain * 0.10f;

            // ветер: низкочастотный шум
            s.windLP += (white - s.windLP) * 0.025f;
            const float wind = s.windLP * s.curWind * 0.9f;

            // буст: свист среднего диапазона
            s.boostLP += (white - s.boostLP) * 0.10f;
            const float boost = (s.boostLP - s.windLP) * s.curBoost * 0.45f;

            // авария: удар шумом и низкий затухающий тон
            float crash = 0.0f;
            if (s.crashEnv > 0.0005f) {
                s.crashPhase += (35.0f + 90.0f * s.crashEnv) / sr;
                s.crashPhase -= std::floor(s.crashPhase);
                crash = (white * 0.6f + std::sin(kTwoPi * s.crashPhase) * 0.8f) * s.crashEnv * 0.7f;
                s.crashEnv *= 0.99993f;
            }

            // монета: два коротких тона подряд
            float coin = 0.0f;
            if (s.coinEnv > 0.001f) {
                s.coinAge += 1.0f / sr;
                const float f = s.coinAge < 0.07f ? 988.0f : 1319.0f;
                s.coinPhase += f / sr;
                s.coinPhase -= std::floor(s.coinPhase);
                coin = std::sin(kTwoPi * s.coinPhase) * s.coinEnv * 0.22f;
                s.coinEnv *= 0.99965f;
            }

            float mono = engine + rain + wind + boost + crash + coin;

            // туннель: эхо
            if (delaySize > delayTap) {
                const std::size_t readPos = (s.delayPos + delaySize - delayTap) % delaySize;
                const float echo = s.delay[readPos];
                s.delay[s.delayPos] = mono + echo * 0.45f;
                s.delayPos = (s.delayPos + 1) % delaySize;
                mono += echo * s.curTunnel * 0.7f;
            }

            mono *= s.curMaster;

            // мягкое ограничение, чтобы не хрипело при сумме звуков
            const float v = std::tanh(mono);

            out[i * 2] = v;
            out[i * 2 + 1] = v;
        }
    }

}

Audio::Audio() {
    state_ = new AudioState();
}

Audio::~Audio() {
    if (state_ != nullptr) {
        if (state_->started) {
            ma_device_uninit(&state_->device);
        }
        delete state_;
    }
}

bool Audio::start() {
    if (state_ == nullptr || state_->started) {
        return state_ != nullptr && state_->started;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 44100;
    config.dataCallback = dataCallback;
    config.pUserData = state_;

    if (ma_device_init(nullptr, &config, &state_->device) != MA_SUCCESS) {
        return false;
    }

    state_->sampleRate = static_cast<float>(state_->device.sampleRate);
    state_->delay.assign(static_cast<std::size_t>(state_->sampleRate * 0.12f), 0.0f);
    state_->delayPos = 0;

    if (ma_device_start(&state_->device) != MA_SUCCESS) {
        ma_device_uninit(&state_->device);
        return false;
    }

    state_->started = true;
    return true;
}

void Audio::update(float speedKmh, bool boosting, float rain, float tunnel, bool active) {
    if (state_ == nullptr || !state_->started) {
        return;
    }

    // «коробка передач»: обороты растут в пределах передачи, потом падают
    const float gearSpan = 90.0f;
    const float gear = std::floor(speedKmh / gearSpan);
    const float inGear = (speedKmh - gear * gearSpan) / gearSpan;

    float hz = 45.0f + 70.0f * inGear + 8.0f * gear;
    if (boosting) {
        hz += 10.0f;
    }

    state_->targetHz = hz;
    state_->targetEngine = std::clamp(0.65f + speedKmh / 1200.0f, 0.0f, 1.0f);
    state_->targetRain = std::clamp(rain, 0.0f, 1.0f);
    state_->targetWind = std::clamp((speedKmh - 100.0f) / 300.0f, 0.0f, 1.0f);
    state_->targetBoost = boosting ? 1.0f : 0.0f;
    state_->targetTunnel = std::clamp(tunnel, 0.0f, 1.0f);
    state_->targetMaster = active ? 0.8f : 0.0f;
}

void Audio::playCrash() {
    if (state_ != nullptr && state_->started) {
        ++state_->crashTrigger;
    }
}

void Audio::playCoin() {
    if (state_ != nullptr && state_->started) {
        ++state_->coinTrigger;
    }
}

#else

// заглушка: miniaudio.h не найден, звука нет
struct AudioState {};

Audio::Audio() {}
Audio::~Audio() {}
bool Audio::start() { return false; }
void Audio::update(float, bool, float, float, bool) {}
void Audio::playCrash() {}
void Audio::playCoin() {}

#endif