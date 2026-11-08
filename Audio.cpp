#include "Audio.hpp"

// Если miniaudio.h лежит рядом, работает настоящий звук.
// Если файла нет, собирается пустая заглушка и игра идёт без звука.
#if defined(__has_include)
#if __has_include("miniaudio.h")
#define GAME_HAS_MINIAUDIO 1
#endif
#endif

#ifdef GAME_HAS_MINIAUDIO

// windows.h (его подключает miniaudio) иначе определяет макросы min, max, near, far
#ifndef NOMINMAX
#define NOMINMAX
#endif

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
    std::atomic<float> targetRpm{ 900.0f };
    std::atomic<float> targetEngine{ 0.0f };
    std::atomic<float> targetRain{ 0.0f };
    std::atomic<float> targetWind{ 0.0f };
    std::atomic<float> targetBoost{ 0.0f };
    std::atomic<float> targetTunnel{ 0.0f };
    std::atomic<float> targetMaster{ 0.0f };
    std::atomic<float> targetSiren{ 0.0f };
    std::atomic<float> targetSkid{ 0.0f };
    std::atomic<int> crashTrigger{ 0 };
    std::atomic<int> coinTrigger{ 0 };
    std::atomic<int> powerTrigger{ 0 };
    std::atomic<int> powerKind{ 0 };
    std::atomic<int> achTrigger{ 0 };
    std::atomic<int> nearTrigger{ 0 };

    // состояние звукового потока
    float curRpm = 900.0f;
    float curEngine = 0.0f;
    float curRain = 0.0f;
    float curWind = 0.0f;
    float curBoost = 0.0f;
    float curTunnel = 0.0f;
    float curMaster = 0.0f;
    float curSiren = 0.0f;
    float curSkid = 0.0f;

    float phase = 0.0f;
    float phase2 = 0.0f;
    float phaseSub = 0.0f;
    float engLP = 0.0f;
    float engLP2 = 0.0f;
    float rumbleLP = 0.0f;
    float jitLP = 0.0f;

    float rainLP = 0.0f;
    float windLP = 0.0f;
    float boostLP = 0.0f;

    float crashEnv = 0.0f;
    float crashPhase = 0.0f;
    float coinEnv = 0.0f;
    float coinAge = 0.0f;
    float coinPhase = 0.0f;

    // бонус: age больше длительности = не играет
    float powerAge = 99.0f;
    int powerKindCur = 0;
    float powerPhase = 0.0f;
    float powerLP = 0.0f;

    // фанфара достижения
    float achAge = 99.0f;
    float achPhase = 0.0f;

    // свист near miss
    float nearAge = 99.0f;
    float nearLP = 0.0f;

    // сирена и скрип шин
    float sirenLfo = 0.0f;
    float sirenPhase = 0.0f;
    float skidPhase = 0.0f;
    float skidLfo = 0.0f;
    float skidLP = 0.0f;

    int seenCrash = 0;
    int seenCoin = 0;
    int seenPower = 0;
    int seenAch = 0;
    int seenNear = 0;

    std::uint32_t seed = 22222u;

    std::vector<float> delay;
    std::size_t delayPos = 0;
};

namespace {

    constexpr float kTwoPi = 6.28318530717958647692f;
    // Звук двигателя игрока. Поменяйте kEngineStyle и пересоберите:
    // 0 = V6 спорт, 1 = V8 (низкий, бархатный), 2 = 4 цилиндра (высокий, злой)
    struct EngineStyle {
        float cylinders;   // число цилиндров: чем больше, тем плотнее и ниже звук
        float rpmIdle;     // обороты на малой скорости
        float rpmSpan;     // на сколько растут обороты внутри одной передачи
        float bright;      // 0..1, сколько высоких гармоник (резкость)
        float rough;       // 0..1, неровность работы мотора
        float rumble;      // 0..1, низкий гул впуска и выхлопа
    };

    constexpr EngineStyle kEngineStyles[3] = {
        { 6.0f, 900.0f, 5200.0f, 0.55f, 0.35f, 0.50f },
        { 8.0f, 700.0f, 4600.0f, 0.38f, 0.60f, 0.85f },
        { 4.0f, 1100.0f, 6800.0f, 0.80f, 0.22f, 0.30f }
    };

    constexpr int kEngineStyle = 0;

    constexpr EngineStyle kEngine = kEngineStyles[kEngineStyle];

    constexpr float kPowerLen = 0.50f;
    constexpr float kAchNote = 0.09f;
    constexpr float kAchLen = 1.00f;
    constexpr float kNearLen = 0.28f;

    float whiteNoise(AudioState& s) {
        s.seed = s.seed * 1664525u + 1013904223u;
        return static_cast<float>(s.seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        AudioState& s = *static_cast<AudioState*>(device->pUserData);
        float* out = static_cast<float*>(output);

        const float sr = s.sampleRate;

        const float tRpm = s.targetRpm.load();
        const float tEngine = s.targetEngine.load();
        const float tRain = s.targetRain.load();
        const float tWind = s.targetWind.load();
        const float tBoost = s.targetBoost.load();
        const float tTunnel = s.targetTunnel.load();
        const float tMaster = s.targetMaster.load();
        const float tSiren = s.targetSiren.load();
        const float tSkid = s.targetSkid.load();

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

        const int powerNow = s.powerTrigger.load();
        if (powerNow != s.seenPower) {
            s.seenPower = powerNow;
            s.powerKindCur = s.powerKind.load();
            s.powerAge = 0.0f;
            s.powerPhase = 0.0f;
        }

        const int achNow = s.achTrigger.load();
        if (achNow != s.seenAch) {
            s.seenAch = achNow;
            s.achAge = 0.0f;
            s.achPhase = 0.0f;
        }

        const int nearNow = s.nearTrigger.load();
        if (nearNow != s.seenNear) {
            s.seenNear = nearNow;
            s.nearAge = 0.0f;
        }

        const std::size_t delaySize = s.delay.size();
        const std::size_t delayTap = static_cast<std::size_t>(sr * 0.070f);

        for (ma_uint32 i = 0; i < frames; ++i) {
            // плавное движение параметров, чтобы не было щелчков
            s.curRpm += (tRpm - s.curRpm) * 0.0012f;
            s.curEngine += (tEngine - s.curEngine) * 0.0008f;
            s.curRain += (tRain - s.curRain) * 0.0004f;
            s.curWind += (tWind - s.curWind) * 0.0004f;
            s.curBoost += (tBoost - s.curBoost) * 0.0010f;
            s.curTunnel += (tTunnel - s.curTunnel) * 0.0006f;
            s.curMaster += (tMaster - s.curMaster) * 0.0006f;
            s.curSiren += (tSiren - s.curSiren) * 0.0006f;
            s.curSkid += (tSkid - s.curSkid) * 0.0015f;

            const float white = whiteNoise(s);

            // двигатель: импульсы цилиндров, неровность оборотов, выхлопной чих и гул
            const EngineStyle& st = kEngine;

            const float rpmN = std::clamp((s.curRpm - st.rpmIdle) / (st.rpmSpan + 1500.0f), 0.0f, 1.0f);

            // обороты слегка «плавают», как у живого мотора
            s.jitLP += (white - s.jitLP) * 0.002f;

            const float fire = s.curRpm / 60.0f * st.cylinders * 0.5f * (1.0f + st.rough * 5.0f * s.jitLP);

            s.phase += fire / sr;
            s.phase -= std::floor(s.phase);
            s.phase2 += fire * 0.5f / sr;
            s.phase2 -= std::floor(s.phase2);

            const float p = kTwoPi * s.phase;

            // основной тон вспышек и гармоники; bright добавляет резкости
            float h = std::sin(p)
                + 0.65f * std::sin(2.0f * p + 0.6f)
                + st.bright * (0.80f * std::sin(3.0f * p + 1.3f)
                    + 0.50f * std::sin(4.0f * p + 2.1f)
                    + 0.32f * std::sin(5.0f * p + 0.4f)
                    + 0.20f * std::sin(6.0f * p + 1.7f));

            // половинная частота даёт характерную неровность порядка работы цилиндров
            h += st.rough * 0.9f * std::sin(kTwoPi * s.phase2);

            // фильтр открывается с ростом оборотов: на холостых глухо, на высоких звонко
            const float lpk = 0.05f + 0.20f * st.bright * (0.3f + rpmN);
            s.engLP += (h - s.engLP) * lpk;
            s.engLP2 += (s.engLP - s.engLP2) * lpk * 1.5f;

            // чих выхлопа: шум в начале каждой вспышки
            const float chuff = std::exp(-12.0f * s.phase) * white * (0.35f + 0.65f * rpmN);

            // низкий гул впуска и выхлопа
            s.rumbleLP += (white - s.rumbleLP) * 0.01f;
            const float rumbleSig = s.rumbleLP * 8.0f * st.rumble;

            const float engineBody = s.engLP2 * 0.55f + chuff * 0.10f + rumbleSig * 0.35f;
            const float engine = engineBody * (0.80f + 0.40f * rpmN) * s.curEngine * 0.30f;


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

            // бонус: у каждого вида свой звук
            float power = 0.0f;
            if (s.powerAge < kPowerLen) {
                s.powerAge += 1.0f / sr;

                const float k = (std::min)(1.0f, s.powerAge / kPowerLen);
                const float env = (1.0f - k) * (1.0f - k) * (std::min)(1.0f, s.powerAge * 300.0f);

                if (s.powerKindCur == 0) {
                    // щит: чистый тон взлетает вверх
                    const float f = 520.0f + 800.0f * k;
                    s.powerPhase += f / sr;
                    s.powerPhase -= std::floor(s.powerPhase);
                    power = (std::sin(kTwoPi * s.powerPhase) * 0.7f + std::sin(2.0f * kTwoPi * s.powerPhase) * 0.2f) * env * 0.26f;
                }
                else if (s.powerKindCur == 1) {
                    // магнит: низкий гудящий тон с дрожанием
                    const float f = 330.0f + 45.0f * std::sin(kTwoPi * 14.0f * s.powerAge);
                    s.powerPhase += f / sr;
                    s.powerPhase -= std::floor(s.powerPhase);
                    power = (std::sin(kTwoPi * s.powerPhase) * 0.6f + (2.0f * s.powerPhase - 1.0f) * 0.25f) * env * 0.26f;
                }
                else {
                    // нитро: пила и шум, тон резко растёт
                    const float f = 180.0f + 900.0f * k * k;
                    s.powerPhase += f / sr;
                    s.powerPhase -= std::floor(s.powerPhase);
                    s.powerLP += (white - s.powerLP) * (0.05f + 0.30f * k);
                    power = ((2.0f * s.powerPhase - 1.0f) * 0.35f + (white - s.powerLP) * 0.45f) * env * 0.30f;
                }
            }

            // достижение: четыре ноты вверх, последняя тянется
            float ach = 0.0f;
            if (s.achAge < kAchLen) {
                s.achAge += 1.0f / sr;

                static const float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.50f };

                const int n = (std::min)(3, static_cast<int>(s.achAge / kAchNote));
                const float noteAge = s.achAge - static_cast<float>(n) * kAchNote;
                const float env = (std::min)(1.0f, noteAge * 500.0f) * std::exp(-noteAge * (n < 3 ? 9.0f : 4.0f));

                s.achPhase += notes[n] / sr;
                s.achPhase -= std::floor(s.achPhase);
                ach = (std::sin(kTwoPi * s.achPhase) + 0.30f * std::sin(2.0f * kTwoPi * s.achPhase)) * env * 0.20f;
            }

            // near miss: короткий свист воздуха
            float nearMiss = 0.0f;
            if (s.nearAge < kNearLen) {
                s.nearAge += 1.0f / sr;

                const float k = (std::min)(1.0f, s.nearAge / kNearLen);
                const float env = std::sin(3.14159265f * k);

                s.nearLP += (white - s.nearLP) * (0.06f + 0.35f * k);
                nearMiss = (white - s.nearLP) * env * 0.16f;
            }

            // сирена полиции: плавная волна между двумя тонами
            float siren = 0.0f;
            if (s.curSiren > 0.001f) {
                s.sirenLfo += 0.9f / sr;
                s.sirenLfo -= std::floor(s.sirenLfo);

                const float tri = std::abs(2.0f * s.sirenLfo - 1.0f);
                const float f = 650.0f + 450.0f * tri;

                s.sirenPhase += f / sr;
                s.sirenPhase -= std::floor(s.sirenPhase);
                siren = (std::sin(kTwoPi * s.sirenPhase) + 0.33f * std::sin(3.0f * kTwoPi * s.sirenPhase)) * s.curSiren * 0.11f;
            }

            // скрип шин: тонкий визг и шипение
            float skid = 0.0f;
            if (s.curSkid > 0.001f) {
                s.skidLfo += 3.1f / sr;
                s.skidLfo -= std::floor(s.skidLfo);

                const float f = 1250.0f + 180.0f * std::sin(kTwoPi * s.skidLfo);

                s.skidPhase += f / sr;
                s.skidPhase -= std::floor(s.skidPhase);
                s.skidLP += (white - s.skidLP) * 0.20f;

                skid = (std::sin(kTwoPi * s.skidPhase) * 0.5f + (white - s.skidLP) * 0.5f) * s.curSkid * 0.07f;
            }

            float mono = engine + rain + wind + boost + crash + coin + power + ach + nearMiss + siren + skid;

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

    float rpm = kEngine.rpmIdle + kEngine.rpmSpan * inGear + 250.0f * gear;
    if (boosting) {
        rpm += 600.0f;
    }

    state_->targetRpm = rpm;
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

void Audio::playPower(int kind) {
    if (state_ != nullptr && state_->started) {
        state_->powerKind = std::clamp(kind, 0, 2);
        ++state_->powerTrigger;
    }
}

void Audio::playAchievement() {
    if (state_ != nullptr && state_->started) {
        ++state_->achTrigger;
    }
}

void Audio::playNearMiss() {
    if (state_ != nullptr && state_->started) {
        ++state_->nearTrigger;
    }
}

void Audio::setSiren(float level) {
    if (state_ != nullptr && state_->started) {
        state_->targetSiren = std::clamp(level, 0.0f, 1.0f);
    }
}

void Audio::setSkid(float level) {
    if (state_ != nullptr && state_->started) {
        state_->targetSkid = std::clamp(level, 0.0f, 1.0f);
    }
}

#else

// заглушка: miniaudio.h не найден, звука нет
#pragma message("Audio.cpp: miniaudio.h not found next to sources, building WITHOUT sound")
struct AudioState {};

Audio::Audio() {}
Audio::~Audio() {}
bool Audio::start() { return false; }
void Audio::update(float, bool, float, float, bool) {}
void Audio::playCrash() {}
void Audio::playCoin() {}
void Audio::playPower(int) {}
void Audio::playAchievement() {}
void Audio::playNearMiss() {}
void Audio::setSiren(float) {}
void Audio::setSkid(float) {}

#endif