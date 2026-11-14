#include "Renderer.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

namespace {

    int gBattleCamera = 0;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kRoadWidth = 10.4f;
    constexpr float kLaneWidth = 3.35f;
    constexpr float kWorldNearZ = 14.0f;
    constexpr float kWorldFarZ = -210.0f;

    // Переключатели графики: если что-то выглядит не так или падает FPS,
    // поставьте false у нужного блока.
    constexpr bool kGfxDynamicLight = true;   // свет зависит от времени суток, дождя и туннеля
    constexpr bool kGfxSpecular = true;       // блики на кузове
    constexpr bool kGfxReflection = true;     // отражение неба на крыше и капоте
    constexpr bool kGfxTextures = true;       // текстура асфальта и травы
    constexpr bool kGfxPines = true;          // хвойные деревья вместо кубов

    GLuint gTexAsphalt = 0;
    GLuint gTexGrass = 0;

    constexpr bool kGfxWetExtras = true;    // отражение кузова, капли на кузове, брызги от машин потока
    constexpr bool kGfxSkidMarks = true;    // следы шин и дым при торможении и заносе

    int gTrack = 0;          // трасса: 0 хайвей, 1 пустыня, 2 снег
    float gSnow = 0.0f;      // 0..1, снегопад (только на снежной трассе, там gRain всегда 0)

    struct SkidSeg {
        float x0, d0, x1, d1;   // начало и конец отрезка следа: поперечная позиция и пройденный путь
    };

    std::vector<SkidSeg> gSkids;
    bool gSkidPrev = false;
    float gSkidPrevX = 0.0f;
    float gSkidPrevD = 0.0f;
    float gSkidSmokeAcc = 0.0f;

    // база цвета и цвет неба для отражения на кузове (выставляются в drawLoft и render)
    const Color* gReflBase = nullptr;
    Color gReflSky = { 0.60f, 0.70f, 0.85f, 1.0f };

    // бесшовная текстура-шум: крупные пятна, мелкое зерно и редкие светлые крупинки
    GLuint makeNoiseTexture(int size, float mean, float fine, float coarseAmp, unsigned int seed) {
        std::vector<unsigned char> px(static_cast<size_t>(size) * static_cast<size_t>(size) * 3u);

        unsigned int s = seed * 2654435761u + 12345u;

        auto rnd01 = [&]() {
            s = s * 1664525u + 1013904223u;
            return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
            };

        constexpr int G = 8;
        float grid[G][G];

        for (int gy = 0; gy < G; ++gy) {
            for (int gx = 0; gx < G; ++gx) {
                grid[gy][gx] = rnd01() - 0.5f;
            }
        }

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float fx = static_cast<float>(x) / static_cast<float>(size) * G;
                const float fy = static_cast<float>(y) / static_cast<float>(size) * G;

                const int ix = static_cast<int>(fx);
                const int iy = static_cast<int>(fy);

                float tx = fx - static_cast<float>(ix);
                float ty = fy - static_cast<float>(iy);

                tx = tx * tx * (3.0f - 2.0f * tx);
                ty = ty * ty * (3.0f - 2.0f * ty);

                const int x0 = ix % G;
                const int x1 = (ix + 1) % G;
                const int y0 = iy % G;
                const int y1 = (iy + 1) % G;

                const float top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx;
                const float bot = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx;
                const float coarse = top + (bot - top) * ty;

                float v = mean + coarseAmp * 2.0f * coarse + (rnd01() - 0.5f) * fine;

                if (rnd01() < 0.012f) {
                    v += 0.10f + 0.20f * rnd01();
                }

                v = std::clamp(v, 0.0f, 1.0f);

                const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 3u;

                px[i + 0] = static_cast<unsigned char>(v * 255.0f);
                px[i + 1] = static_cast<unsigned char>(v * 255.0f);
                px[i + 2] = static_cast<unsigned char>(std::min(1.0f, v * 1.03f) * 255.0f);
            }
        }

        GLuint tex = 0;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

        if (GLEW_EXT_texture_filter_anisotropic) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());

        glBindTexture(GL_TEXTURE_2D, 0);

        return tex;
    }

    struct Vec3 {
        float x;
        float y;
        float z;
    };

    Vec3 operator-(const Vec3& a, const Vec3& b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    Vec3 normalize(const Vec3& v) {
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 0.0001f) {
            return { 0.0f, 0.0f, 0.0f };
        }
        return { v.x / len, v.y / len, v.z / len };
    }

    Color mixColor(const Color& a, const Color& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }

    void setColor(const Color& c) {
        glColor4f(c.r, c.g, c.b, c.a);
    }

    void setPerspective(float fovYDegrees, float aspect, float nearPlane, float farPlane) {
        const float top = std::tan(fovYDegrees * kPi / 360.0f) * nearPlane;
        const float right = top * aspect;
        glFrustum(-right, right, -top, top, nearPlane, farPlane);
    }

    void lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = normalize(center - eye);
        const Vec3 s = normalize(cross(f, up));
        const Vec3 u = cross(s, f);

        const GLfloat matrix[16] = {
            s.x, u.x, -f.x, 0.0f,
            s.y, u.y, -f.y, 0.0f,
            s.z, u.z, -f.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        glMultMatrixf(matrix);
        glTranslatef(-eye.x, -eye.y, -eye.z);
    }

    void drawBox(float x, float y, float z, float sx, float sy, float sz, Color color) {
        const float x1 = x - sx * 0.5f;
        const float x2 = x + sx * 0.5f;
        const float y1 = y - sy * 0.5f;
        const float y2 = y + sy * 0.5f;
        const float z1 = z - sz * 0.5f;
        const float z2 = z + sz * 0.5f;

        setColor(color);

        glBegin(GL_QUADS);

        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(x1, y1, z2);
        glVertex3f(x2, y1, z2);
        glVertex3f(x2, y2, z2);
        glVertex3f(x1, y2, z2);

        glNormal3f(0.0f, 0.0f, -1.0f);
        glVertex3f(x2, y1, z1);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y2, z1);
        glVertex3f(x2, y2, z1);

        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(x1, y2, z2);
        glVertex3f(x2, y2, z2);
        glVertex3f(x2, y2, z1);
        glVertex3f(x1, y2, z1);

        glNormal3f(0.0f, -1.0f, 0.0f);
        glVertex3f(x1, y1, z1);
        glVertex3f(x2, y1, z1);
        glVertex3f(x2, y1, z2);
        glVertex3f(x1, y1, z2);

        glNormal3f(-1.0f, 0.0f, 0.0f);
        glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z2);
        glVertex3f(x1, y2, z2);
        glVertex3f(x1, y2, z1);

        glNormal3f(1.0f, 0.0f, 0.0f);
        glVertex3f(x2, y1, z2);
        glVertex3f(x2, y1, z1);
        glVertex3f(x2, y2, z1);
        glVertex3f(x2, y2, z2);

        glEnd();
    }

    void drawGroundQuad(float x1, float z1, float x2, float z2, Color color, float y = 0.0f) {
        setColor(color);

        glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(x1, y, z1);
        glVertex3f(x2, y, z1);
        glVertex3f(x2, y, z2);
        glVertex3f(x1, y, z2);
        glEnd();
    }

    void drawCylinder(float x, float y, float z, float radius, float height, int segments, Color color) {
        setColor(color);

        glBegin(GL_QUAD_STRIP);

        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            const float cx = std::cos(a);
            const float cz = std::sin(a);

            glNormal3f(cx, 0.0f, cz);
            glVertex3f(x + cx * radius, y, z + cz * radius);
            glVertex3f(x + cx * radius, y + height, z + cz * radius);
        }

        glEnd();
    }

    // 0 = день, 1 = глубокая ночь (выставляется в Renderer::render)
    float gNightLevel = 0.0f;

    float gDriverTime = 0.0f;   // время для анимации водителя
    float gDriverWind = 0.0f;   // 0..1, сила встречного ветра (скорость)
    float gDriverAlert = 0.0f;  // 0..1, водитель напрягся: впереди машина, обе руки на руле

    // 0 = ясно, 1 = сильный дождь (берётся из Game::rain(), в туннеле 0)
    float gRain = 0.0f;

    // 0..1, плавно растёт, когда игрок тормозит
    float gBrake = 0.0f;

    // ---------- Туннели ----------
    // Константы туннеля (kTunnelPeriod, kTunnelPhase, kTunnelLength)
    // лежат в Game.hpp, чтобы Game и Renderer использовали одни и те же.

    float gDist = 0.0f;
    float gPlayerZ = 4.0f;
    float gTunnelInside = 0.0f;   // 0..1, камера внутри туннеля
    bool gTunnelValid = false;
    float gTunZNear = 0.0f;
    float gTunZFar = 0.0f;
    float gTunLoD = 0.0f;
    float gTunHiD = 0.0f;
    bool gTunStartVisible = false;
    bool gTunEndVisible = false;
    float gTunStartZ = 0.0f;
    float gTunEndZ = 0.0f;

    bool findTunnel(float dist, float& a, float& b) {
        const float k = std::floor((dist - kTunnelPhase) / kTunnelPeriod);

        for (int i = 0; i < 2; ++i) {
            a = (k + static_cast<float>(i)) * kTunnelPeriod + kTunnelPhase;
            b = a + kTunnelLength;

            if (b > dist - 30.0f && a < dist + 230.0f) {
                return true;
            }
        }

        return false;
    }

    bool inTunnelZ(float z) {
        return gTunnelValid && z <= gTunZNear + 3.0f && z >= gTunZFar - 3.0f;
    }

    float fract(float v) {
        return v - std::floor(v);
    }

    void beginGlow() {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
    }

    void endGlow() {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    // обычное полупрозрачное рисование без записи глубины (лужи, облака);
    // закончить нужно через endGlow()
    void beginAlpha() {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }

    // круглое свечение, повёрнутое к камере (в плоскости XY)
    void drawGlow(float x, float y, float z, float radius, Color c) {
        constexpr int seg = 20;

        glBegin(GL_TRIANGLE_FAN);
        setColor(c);
        glVertex3f(x, y, z);
        setColor({ c.r, c.g, c.b, 0.0f });
        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / seg * 2.0f * kPi;
            glVertex3f(x + std::cos(a) * radius, y + std::sin(a) * radius, z);
        }
        glEnd();
    }

    // световое пятно на земле (в плоскости XZ)
    void drawGroundGlow(float cx, float cz, float rx, float rz, Color c, float y) {
        constexpr int seg = 24;

        glBegin(GL_TRIANGLE_FAN);
        setColor(c);
        glVertex3f(cx, y, cz);
        setColor({ c.r, c.g, c.b, 0.0f });
        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / seg * 2.0f * kPi;
            glVertex3f(cx + std::cos(a) * rx, y, cz + std::sin(a) * rz);
        }
        glEnd();
    }

    void drawLampLight(float headX, float headY, float z, float poolX) {
        const float k = 0.10f + 0.90f * gNightLevel;

        beginGlow();
        drawGlow(headX, headY, z + 0.2f, 1.1f, { 1.0f, 0.80f, 0.40f, 0.55f * k });
        drawGroundGlow(poolX, z, 3.4f, 5.5f, { 1.0f, 0.78f, 0.40f, 0.38f * k }, 0.045f);

        // отражение лампы на мокрой дороге
        if (gRain > 0.01f) {
            drawGroundGlow(poolX * 0.85f, z + 7.0f, 0.9f, 11.0f, { 1.0f, 0.78f, 0.40f, 0.30f * gRain * k }, 0.05f);
        }
        endGlow();
    }

    void drawWall(float x, float y, float z, float w, float h, Color color) {
        setColor(color);

        glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(x - w * 0.5f, y - h * 0.5f, z);
        glVertex3f(x + w * 0.5f, y - h * 0.5f, z);
        glVertex3f(x + w * 0.5f, y + h * 0.5f, z);
        glVertex3f(x - w * 0.5f, y + h * 0.5f, z);
        glEnd();
    }

    // плоский диск, перпендикулярный оси X
    void drawDiscX(float x, float y, float z, float r, float nx, Color c) {
        constexpr int seg = 20;

        setColor(c);
        glNormal3f(nx, 0.0f, 0.0f);

        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(x, y, z);

        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(seg) * 2.0f * kPi;
            glVertex3f(x, y + std::cos(a) * r, z + std::sin(a) * r);
        }

        glEnd();
    }

    // кольцо между радиусами r0 и r1
    void drawRingX(float x, float y, float z, float r0, float r1, float nx, Color c) {
        constexpr int seg = 20;

        setColor(c);
        glNormal3f(nx, 0.0f, 0.0f);

        glBegin(GL_QUAD_STRIP);

        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(seg) * 2.0f * kPi;
            const float ca = std::cos(a);
            const float sa = std::sin(a);

            glVertex3f(x, y + ca * r0, z + sa * r0);
            glVertex3f(x, y + ca * r1, z + sa * r1);
        }

        glEnd();
    }

    // спица: трапеция от радиуса r0 до r1, шире у обода
    void drawSpokeX(float x, float y, float z, float angle, float r0, float r1, float w0, float w1, float nx, Color c) {
        const float ca = std::cos(angle);
        const float sa = std::sin(angle);

        // направление спицы в плоскости (y, z) и перпендикуляр к нему
        const float dy = ca;
        const float dz = sa;
        const float py = -sa;
        const float pz = ca;

        setColor(c);
        glNormal3f(nx, 0.0f, 0.0f);

        glBegin(GL_QUADS);
        glVertex3f(x, y + dy * r0 - py * w0, z + dz * r0 - pz * w0);
        glVertex3f(x, y + dy * r0 + py * w0, z + dz * r0 + pz * w0);
        glVertex3f(x, y + dy * r1 + py * w1, z + dz * r1 + pz * w1);
        glVertex3f(x, y + dy * r1 - py * w1, z + dz * r1 - pz * w1);
        glEnd();
    }

    // колесо: шина с боковинами, обод, пять спиц и ступица; наружу смотрит сторона, где x больше нуля у правых колёс
    void drawWheel(float x, float y, float z, float radius, float width, float spin) {
        const float dir = x < 0.0f ? -1.0f : 1.0f;
        const float xo = x + dir * width * 0.5f;
        const float xi = x - dir * width * 0.5f;

        // протектор
        setColor({ 0.025f, 0.028f, 0.032f, 1.0f });

        glBegin(GL_QUAD_STRIP);

        constexpr int segments = 20;

        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            const float cy = std::cos(a);
            const float cz = std::sin(a);

            glNormal3f(0.0f, cy, cz);
            glVertex3f(x - width * 0.5f, y + cy * radius, z + cz * radius);
            glVertex3f(x + width * 0.5f, y + cy * radius, z + cz * radius);
        }

        glEnd();

        // боковины
        const Color sidewall = { 0.050f, 0.052f, 0.058f, 1.0f };

        drawDiscX(xo + dir * 0.002f, y, z, radius, dir, sidewall);
        drawDiscX(xi - dir * 0.002f, y, z, radius, -dir, sidewall);

        // обод: серебристое кольцо, тёмное дно и пять спиц
        const Color silver = { 0.70f, 0.72f, 0.76f, 1.0f };
        const Color deep = { 0.075f, 0.078f, 0.088f, 1.0f };

        const float f1 = xo + dir * 0.006f;
        const float f2 = xo + dir * 0.010f;
        const float f3 = xo + dir * 0.014f;

        drawDiscX(f1, y, z, radius * 0.60f, dir, deep);
        drawRingX(f1, y, z, radius * 0.60f, radius * 0.72f, dir, silver);

        for (int i = 0; i < 5; ++i) {
            const float ang = static_cast<float>(i) * 2.0f * kPi / 5.0f + 0.3f + spin;

            drawSpokeX(f2, y, z, ang, radius * 0.10f, radius * 0.62f, radius * 0.045f, radius * 0.085f, dir, silver);
        }

        // ступица и красный суппорт тормоза
        drawDiscX(f3, y, z, radius * 0.15f, dir, silver);
        drawDiscX(f3 + dir * 0.003f, y, z, radius * 0.07f, dir, deep);
        drawSpokeX(f1 + dir * 0.002f, y, z, 2.2f, radius * 0.32f, radius * 0.56f, radius * 0.10f, radius * 0.12f, dir, { 0.80f, 0.08f, 0.06f, 1.0f });
    }

    // ---------- Реалистичный кузов: профиль в виде лофта ----------

    // форма кузова: все числа в долях длины (t: 0 нос, 1 корма) или высоты
    struct CarShape {
        float belt;        // высота линии капота и багажника, доля высоты машины
        float hoodEnd;     // где кончается капот и начинается лобовое стекло
        float roofStart;   // начало крыши
        float roofEnd;     // конец крыши
        float rearBase;    // основание заднего стекла
        float nose;        // высота носа, доля линии капота
        float trunk;       // высота багажника, доля линии капота
        float roofW;       // полуширина крыши, доля полуширины корпуса
    };

    CarShape carShape(TrafficType type) {
        switch (type) {
        case TrafficType::Sport: return { 0.43f, 0.37f, 0.52f, 0.66f, 0.82f, 0.67f, 0.83f, 0.72f };
        case TrafficType::Van:   return { 0.46f, 0.16f, 0.22f, 0.90f, 0.97f, 0.92f, 1.00f, 0.82f };
                             // купе: длинный капот, короткая кабина с узкой крышей, пологое заднее стекло
        case TrafficType::Coupe: return { 0.49f, 0.34f, 0.50f, 0.62f, 0.84f, 0.80f, 0.92f, 0.64f };
        default:                 return { 0.50f, 0.30f, 0.43f, 0.67f, 0.82f, 0.84f, 0.95f, 0.70f };
        }
    }

    float piecewise6(const float(&ts)[6], const float(&fs)[6], float t) {
        t = std::clamp(t, 0.0f, 1.0f);

        for (int i = 0; i < 5; ++i) {
            if (t <= ts[i + 1]) {
                const float span = ts[i + 1] - ts[i];
                const float k = span > 0.0001f ? (t - ts[i]) / span : 0.0f;
                return fs[i] + (fs[i + 1] - fs[i]) * k;
            }
        }

        return fs[5];
    }

    // высота верха корпуса (капот, багажник) на отметке t
    float carHullTop(const CarShape& s, float height, float t) {
        const float ts[6] = { 0.0f, s.hoodEnd * 0.5f, s.hoodEnd, s.rearBase, s.rearBase + (1.0f - s.rearBase) * 0.55f, 1.0f };
        const float fs[6] = { s.nose, s.nose + (1.0f - s.nose) * 0.65f, 1.0f, 1.0f, s.trunk, s.trunk - 0.07f };

        return height * s.belt * piecewise6(ts, fs, t);
    }

    // ширина корпуса на отметке t, доля от полной
    float carWidthFrac(const CarShape& s, float t) {
        const float ts[6] = { 0.0f, s.hoodEnd * 0.5f, s.hoodEnd, s.rearBase, s.rearBase + (1.0f - s.rearBase) * 0.55f, 1.0f };
        const float fs[6] = { 0.88f, 0.96f, 1.0f, 1.0f, 0.99f, 0.93f };

        return piecewise6(ts, fs, t);
    }

    struct CabinSec {
        float y0;   // низ кабины (верх корпуса)
        float ch;   // высота кабины
        float wb;   // полуширина внизу
        float wt;   // полуширина вверху
        float z;
    };

    CabinSec cabinAt(const CarShape& s, float W, float L, float H, float t) {
        t = std::clamp(t, 0.0f, 1.0f);

        const float hw = W * 0.5f * carWidthFrac(s, t);
        const float wb = hw * 0.80f;
        const float chFull = H * (1.0f - s.belt);

        float k = 0.0f;

        if (t <= s.hoodEnd) {
            k = 0.0f;
        }
        else if (t < s.roofStart) {
            k = (t - s.hoodEnd) / (s.roofStart - s.hoodEnd);
        }
        else if (t <= s.roofEnd) {
            k = 1.0f;
        }
        else if (t < s.rearBase) {
            k = 1.0f - (t - s.roofEnd) / (s.rearBase - s.roofEnd);
        }

        CabinSec c;
        c.y0 = carHullTop(s, H, t);
        c.ch = chFull * k;
        c.wb = wb;
        c.wt = wb + (s.roofW * hw - wb) * k;
        c.z = -L * 0.5f + t * L;

        return c;
    }

    // один четырёхугольник с нормалью, направленной наружу от точки center;
    // нормаль по Ньюэллу, поэтому вырожденные четырёхугольники безопасны
    void emitQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& center, float offset = 0.0f) {
        const Vec3 p[4] = { a, b, c, d };

        Vec3 n = { 0.0f, 0.0f, 0.0f };

        for (int i = 0; i < 4; ++i) {
            const Vec3& u = p[i];
            const Vec3& v = p[(i + 1) % 4];

            n.x += (u.y - v.y) * (u.z + v.z);
            n.y += (u.z - v.z) * (u.x + v.x);
            n.z += (u.x - v.x) * (u.y + v.y);
        }

        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);

        if (len < 0.000001f) {
            return;
        }

        n = { n.x / len, n.y / len, n.z / len };

        const float qx = (a.x + b.x + c.x + d.x) * 0.25f;
        const float qy = (a.y + b.y + c.y + d.y) * 0.25f;
        const float qz = (a.z + b.z + c.z + d.z) * 0.25f;

        if (n.x * (qx - center.x) + n.y * (qy - center.y) + n.z * (qz - center.z) < 0.0f) {
            n = { -n.x, -n.y, -n.z };
        }

        // отражение неба: верхние грани светлеют, нижние темнеют
        if (gReflBase) {
            Color c = *gReflBase;

            if (n.y > 0.0f) {
                c = mixColor(c, gReflSky, 0.34f * n.y);
            }
            else {
                c = mixColor(c, { 0.0f, 0.0f, 0.0f, 1.0f }, -0.45f * n.y);
            }

            setColor(c);
        }

        glNormal3f(n.x, n.y, n.z);

        for (int i = 0; i < 4; ++i) {
            glVertex3f(p[i].x + n.x * offset, p[i].y + n.y * offset, p[i].z + n.z * offset);
        }
    }

    // тело вращения по сечениям: кольца соединяются четырёхугольниками
    void drawLoft(const std::vector<std::vector<Vec3>>& rings, const Color& color, bool caps, bool reflect = false) {
        if (rings.size() < 2) {
            return;
        }

        Vec3 center = { 0.0f, 0.0f, 0.0f };
        int count = 0;

        for (const auto& ring : rings) {
            for (const auto& p : ring) {
                center.x += p.x;
                center.y += p.y;
                center.z += p.z;
                ++count;
            }
        }

        center = { center.x / count, center.y / count, center.z / count };

        setColor(color);

        gReflBase = (reflect && kGfxReflection) ? &color : nullptr;

        glBegin(GL_QUADS);

        for (size_t r = 0; r + 1 < rings.size(); ++r) {
            const size_t n = rings[r].size();

            for (size_t i = 0; i < n; ++i) {
                const size_t j = (i + 1) % n;

                emitQuad(rings[r][i], rings[r + 1][i], rings[r + 1][j], rings[r][j], center);
            }
        }

        if (caps) {
            for (int end = 0; end < 2; ++end) {
                const auto& ring = end == 0 ? rings.front() : rings.back();

                if (ring.size() == 8) {
                    emitQuad(ring[0], ring[1], ring[2], ring[3], center);
                    emitQuad(ring[0], ring[3], ring[4], ring[7], center);
                    emitQuad(ring[7], ring[4], ring[5], ring[6], center);
                }
            }
        }

        glEnd();

        gReflBase = nullptr;
    }

    // кузов легковой машины: скошенный капот, лобовое и заднее стекло, боковые окна
    void drawCarBody(const CarShape& s, float W, float L, float H, const Color& body, bool player) {
        // корпус: 7 сечений от носа до кормы, каждое с фасками по бокам
        const float hullT[7] = {
            0.0f,
            s.hoodEnd * 0.5f,
            s.hoodEnd,
            (s.hoodEnd + s.rearBase) * 0.5f,
            s.rearBase,
            s.rearBase + (1.0f - s.rearBase) * 0.55f,
            1.0f
        };

        std::vector<std::vector<Vec3>> hull;

        for (int i = 0; i < 7; ++i) {
            const float t = hullT[i];
            const float top = carHullTop(s, H, t);
            const float hw = W * 0.5f * carWidthFrac(s, t);
            const float yb = 0.17f;
            const float h = std::max(top - yb, 0.05f);
            const float z = -L * 0.5f + t * L;

            hull.push_back({
                { -hw * 0.86f, yb, z },
                { -hw, yb + h * 0.25f, z },
                { -hw, yb + h * 0.66f, z },
                { -hw * 0.86f, top, z },
                { hw * 0.86f, top, z },
                { hw, yb + h * 0.66f, z },
                { hw, yb + h * 0.25f, z },
                { hw * 0.86f, yb, z }
                });
        }

        // блики: глянцевый лак кузова
        if (kGfxSpecular) {
            const GLfloat spec[] = { 0.60f, 0.60f, 0.64f, 1.0f };

            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 56.0f);
        }

        drawLoft(hull, body, true, true);

        // кабина: трапеция, сужающаяся кверху
        const float cabT[4] = { s.hoodEnd, s.roofStart, s.roofEnd, s.rearBase };

        std::vector<std::vector<Vec3>> cabin;

        for (int i = 0; i < 4; ++i) {
            const CabinSec c = cabinAt(s, W, L, H, cabT[i]);

            cabin.push_back({
                { -c.wb, c.y0, c.z },
                { -c.wt, c.y0 + c.ch, c.z },
                { c.wt, c.y0 + c.ch, c.z },
                { c.wb, c.y0, c.z }
                });
        }

        drawLoft(cabin, mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.12f), false, true);

        if (kGfxSpecular) {
            const GLfloat none[] = { 0.0f, 0.0f, 0.0f, 1.0f };

            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, none);
        }

        // стёкла чуть выступают над поверхностью кабины
        const Color glass = { 0.06f, 0.15f, 0.21f, 1.0f };
        const Vec3 gc = { 0.0f, H * s.belt * 0.7f, 0.0f };

        setColor(glass);
        glBegin(GL_QUADS);

        // лобовое
        {
            const CabinSec a = cabinAt(s, W, L, H, s.hoodEnd + (s.roofStart - s.hoodEnd) * 0.08f);
            const CabinSec b = cabinAt(s, W, L, H, s.hoodEnd + (s.roofStart - s.hoodEnd) * 0.94f);

            emitQuad(
                { -a.wt * 0.92f, a.y0 + a.ch, a.z },
                { a.wt * 0.92f, a.y0 + a.ch, a.z },
                { b.wt * 0.92f, b.y0 + b.ch, b.z },
                { -b.wt * 0.92f, b.y0 + b.ch, b.z },
                gc, 0.006f
            );
        }

        // заднее
        {
            const CabinSec a = cabinAt(s, W, L, H, s.roofEnd + (s.rearBase - s.roofEnd) * 0.06f);
            const CabinSec b = cabinAt(s, W, L, H, s.roofEnd + (s.rearBase - s.roofEnd) * 0.92f);

            emitQuad(
                { -a.wt * 0.92f, a.y0 + a.ch, a.z },
                { a.wt * 0.92f, a.y0 + a.ch, a.z },
                { b.wt * 0.92f, b.y0 + b.ch, b.z },
                { -b.wt * 0.92f, b.y0 + b.ch, b.z },
                gc, 0.006f
            );
        }

        // боковые окна: три секции, повторяющие излом кабины
        {
            const float tf = s.hoodEnd + (s.roofStart - s.hoodEnd) * 0.55f;
            const float tb = s.roofEnd + (s.rearBase - s.roofEnd) * 0.45f;
            const float tw[4] = { tf, s.roofStart, s.roofEnd, tb };

            for (int side = -1; side <= 1; side += 2) {
                const float sg = static_cast<float>(side);

                auto sidePoint = [&](float t, float sv) -> Vec3 {
                    const CabinSec c = cabinAt(s, W, L, H, t);
                    return { sg * (c.wb + (c.wt - c.wb) * sv), c.y0 + c.ch * sv, c.z };
                    };

                for (int i = 0; i < 3; ++i) {
                    emitQuad(
                        sidePoint(tw[i], 0.20f),
                        sidePoint(tw[i + 1], 0.20f),
                        sidePoint(tw[i + 1], 0.86f),
                        sidePoint(tw[i], 0.86f),
                        gc, 0.005f
                    );
                }
            }
        }

        glEnd();

        // линии дверей на боковинах
        {
            const Color seam = mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.45f);
            const float hw = W * 0.5f;
            const float yMid = H * s.belt * 0.62f;
            const float yH = H * s.belt * 0.40f;

            const float zs[2] = {
                -L * 0.5f + (s.hoodEnd + 0.02f) * L,
                -L * 0.5f + (s.roofEnd + 0.02f) * L
            };

            for (int i = 0; i < 2; ++i) {
                drawBox(-(hw + 0.004f), yMid, zs[i], 0.008f, yH, 0.014f, seam);
                drawBox(hw + 0.004f, yMid, zs[i], 0.008f, yH, 0.014f, seam);
            }
        }

    }

    // Helpers for vehicle details.
    void drawRainBeads(const CarShape& shape, float width, float length, float height) {
        beginAlpha();
        for (int i = 0; i < 18; ++i) {
            const float t = 0.18f + 0.035f * static_cast<float>(i);
            const float x = (i % 3 - 1) * width * 0.19f;
            const float y = carHullTop(shape, height, t) + 0.018f;
            const float z = -length * 0.5f + t * length;
            drawBox(x, y, z, 0.018f, 0.009f, 0.045f, { 0.72f, 0.86f, 1.0f, 0.45f });
        }
        endGlow();
    }

    void drawCarDetails(TrafficType type, const CarShape& shape,
        float width, float length, float height, bool player) {
        if (type == TrafficType::Truck || type == TrafficType::Bus) return;
        const Color trim = { 0.045f, 0.05f, 0.06f, 1.0f };
        const float side = width * 0.505f;
        const float y = height * shape.belt * 0.62f;
        for (int dir : {-1, 1}) {
            drawBox(dir * side, y, length * 0.08f, 0.018f, 0.07f, length * 0.34f, trim);
            drawBox(dir * side, y + 0.04f, -length * 0.12f, 0.022f, 0.045f, 0.16f,
                player ? Color{ 0.75f, 0.78f, 0.85f, 1.0f } : trim);
        }
    }

    void drawPlate(float x, float y, float z, float width, float height, unsigned int seed) {
        drawBox(x, y, z, width, height, 0.015f, { 0.92f, 0.92f, 0.84f, 1.0f });
        const float shift = static_cast<float>(seed % 7u) / 7.0f;
        for (int i = 0; i < 4; ++i) {
            drawBox(x - width * 0.30f + i * width * 0.20f,
                y + (i % 2 ? shift : -shift) * height * 0.08f,
                z + 0.009f, width * 0.08f, height * 0.44f, 0.008f,
                { 0.09f, 0.12f, 0.17f, 1.0f });
        }
    }

    void drawHeadlight(float x, float y, float z, float width, float height, bool player) {
        drawBox(x, y, z, width, height, 0.05f, { 0.045f, 0.05f, 0.06f, 1.0f });
        drawBox(x, y, z - 0.029f, width * 0.82f, height * 0.70f, 0.012f,
            player ? Color{ 0.93f, 0.96f, 1.0f, 1.0f }
        : Color{ 0.85f, 0.87f, 0.78f, 1.0f });
    }

    void drawCarModel(
        float x,
        float z,
        float width,
        float length,
        float height,
        Color body,
        TrafficType type,
        float yaw,
        bool player,
        float wheelSpin = 0.0f,
        int signalDir = 0,
        float signalTime = 0.0f
    ) {
        glPushMatrix();

        glTranslatef(x, 0.0f, z);
        glRotatef(yaw, 0.0f, 1.0f, 0.0f);

        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        drawGroundQuad(
            -width * 0.72f,
            -length * 0.63f,
            width * 0.72f,
            length * 0.63f,
            { 0.0f, 0.0f, 0.0f, 0.16f },
            0.025f
        );

        // мягкая тень по центру: тёмнее под кузовом, к краям тает
        drawGroundGlow(0.0f, 0.0f, width * 0.80f, length * 0.62f, { 0.0f, 0.0f, 0.0f, 0.34f }, 0.026f);

        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);

        const float bodyHeight = height * 0.48f;

        const bool boxy = (type == TrafficType::Truck || type == TrafficType::Bus);
        const CarShape shape = carShape(type);

        // высота фар и стоп-сигналов зависит от формы носа и кормы
        float lightY = 0.42f;
        float barY = 0.42f;

        if (boxy) {
            drawBox(0.0f, bodyHeight * 0.55f, 0.0f, width, bodyHeight, length, body);
        }
        else {
            drawCarBody(shape, width, length, height, body, player);

            if (kGfxWetExtras && gRain > 0.12f) {
                drawRainBeads(shape, width, length, height);
            }

            lightY = std::max(0.30f, carHullTop(shape, height, 0.02f) - 0.08f);
            barY = lightY + 0.06f;
        }

        if (type == TrafficType::Truck) {
            drawBox(
                0.0f,
                1.55f,
                length * 0.12f,
                width * 0.94f,
                height * 0.78f,
                length * 0.55f,
                mixColor(body, { 0.95f, 0.95f, 0.95f, 1.0f }, 0.18f)
            );

            drawBox(
                0.0f,
                1.2f,
                -length * 0.31f,
                width * 0.82f,
                1.35f,
                length * 0.28f,
                mixColor(body, { 0.1f, 0.1f, 0.12f, 1.0f }, 0.18f)
            );
        }
        else if (type == TrafficType::Bus) {
            // автобус: высокий верх, лента окон по бокам и лобовое стекло
            const Color busGlass = { 0.05f, 0.13f, 0.19f, 1.0f };
            const float upperH = height - bodyHeight;

            const float cabinHeight =
                type == TrafficType::Van
                ? height * 0.63f
                : height * 0.52f;

            drawBox(
                0.0f,
                bodyHeight + upperH * 0.5f,
                0.0f,
                width * 0.98f,
                upperH,
                length * 0.98f,
                mixColor(body, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.12f)
            );

            drawBox(-(width * 0.5f + 0.012f), bodyHeight + upperH * 0.52f, 0.0f, 0.02f, upperH * 0.46f, length * 0.86f, busGlass);
            drawBox(width * 0.5f + 0.012f, bodyHeight + upperH * 0.52f, 0.0f, 0.02f, upperH * 0.46f, length * 0.86f, busGlass);

            drawBox(0.0f, bodyHeight + upperH * 0.52f, -length * 0.49f - 0.01f, width * 0.88f, upperH * 0.50f, 0.03f, busGlass);

            // табло маршрута над лобовым стеклом
            drawBox(0.0f, height - 0.14f, -length * 0.49f - 0.01f, width * 0.5f, 0.12f, 0.03f, { 1.0f, 0.65f, 0.10f, 1.0f });
        }
        else {
            // боковые зеркала у основания лобового стекла
            const Color mirror = mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.35f);
            const float mirrorY = height * shape.belt + 0.01f;
            const float mirrorZ = -length * 0.5f + shape.hoodEnd * length + 0.10f;

            drawBox(-(width * 0.5f + 0.10f), bodyHeight + 0.10f, -length * 0.14f, 0.18f, 0.10f, 0.12f, mirror);
            drawBox(width * 0.5f + 0.10f, bodyHeight + 0.10f, -length * 0.14f, 0.18f, 0.10f, 0.12f, mirror);

            // задний спойлер у спортивных машин в потоке
            if (type == TrafficType::Sport && !player) {
                const Color dark = { 0.04f, 0.04f, 0.05f, 1.0f };
                const float deck = carHullTop(shape, height, 0.90f);

                drawBox(0.0f, deck + 0.14f, length * 0.43f, width * 0.80f, 0.06f, 0.22f, dark);
                drawBox(-width * 0.30f, deck + 0.07f, length * 0.43f, 0.06f, 0.14f, 0.12f, dark);
                drawBox(width * 0.30f, deck + 0.07f, length * 0.43f, 0.06f, 0.14f, 0.12f, dark);
            }

            // гоночные полосы на машине игрока
            if (player) {
                const Color stripe = { 0.95f, 0.96f, 1.0f, 1.0f };
                const float topY = bodyHeight * 1.05f + 0.006f;
                const float roofT = (shape.roofStart + shape.roofEnd) * 0.5f;
                const CabinSec roof = cabinAt(shape, width, length, height, roofT);
                const float roofY = roof.y0 + roof.ch + 0.006f;
                const float cabinLength = (shape.roofEnd - shape.roofStart) * length;

                drawBox(0.0f, topY, -length * 0.38f, width * 0.18f, 0.012f, length * 0.24f, stripe);
                drawBox(0.0f, roofY, -length * 0.03f, width * 0.16f, 0.012f, cabinLength, stripe);
                drawBox(0.0f, topY, length * 0.35f, width * 0.18f, 0.012f, length * 0.28f, stripe);
            }
        }

        const float wheelY = 0.27f;
        const float wheelZ1 = -length * 0.28f;
        const float wheelZ2 = length * 0.28f;

        drawWheel(-width * 0.53f, wheelY, wheelZ1, 0.31f, 0.20f, wheelSpin);
        drawWheel(width * 0.53f, wheelY, wheelZ1, 0.31f, 0.20f, wheelSpin);
        drawWheel(-width * 0.53f, wheelY, wheelZ2, 0.31f, 0.20f, wheelSpin);
        drawWheel(width * 0.53f, wheelY, wheelZ2, 0.31f, 0.20f, wheelSpin);

        drawCarDetails(type, shape, width, length, height, player);

        // бамперы, планка стоп-сигналов и номер
        const Color bumper = { 0.07f, 0.07f, 0.08f, 1.0f };

        drawBox(0.0f, 0.28f, -length * 0.5f - 0.02f, width * 0.96f, 0.16f, 0.10f, bumper);
        drawBox(0.0f, 0.28f, length * 0.5f + 0.02f, width * 0.96f, 0.16f, 0.10f, bumper);
        drawBox(0.0f, barY, length * 0.506f, width * 0.30f, 0.05f, 0.06f, { 0.35f, 0.02f, 0.02f, 1.0f });

        // номер на заднем бампере: у каждой машины свой, считается из цвета и длины
        const unsigned int plateSeed = player
            ? 2026u
            : (static_cast<unsigned int>(body.r * 255.0f) * 73856093u) ^
            (static_cast<unsigned int>(body.g * 255.0f) * 19349663u) ^
            (static_cast<unsigned int>(body.b * 255.0f) * 83492791u) ^
            (static_cast<unsigned int>(length * 100.0f) * 2654435761u);

        drawPlate(0.0f, 0.30f, length * 0.5f + 0.085f, width * 0.30f, 0.14f, plateSeed);

        const Color rearLight = { 1.0f, 0.04f, 0.03f, 1.0f };

        // фары с корпусом, отражателем, лампой, дневными огнями и линзой
        drawHeadlight(-width * 0.27f, lightY, -length * 0.505f, width * 0.21f, 0.12f, player);
        drawHeadlight(width * 0.27f, lightY, -length * 0.505f, width * 0.21f, 0.12f, player);

        // решётка радиатора между фарами
        if (!boxy) {
            drawBox(0.0f, lightY - 0.01f, -length * 0.5f - 0.005f, width * 0.26f, 0.09f, 0.04f, { 0.05f, 0.05f, 0.06f, 1.0f });
        }

        drawBox(-width * 0.27f, lightY, length * 0.505f, width * 0.18f, 0.12f, 0.07f, rearLight);
        drawBox(width * 0.27f, lightY, length * 0.505f, width * 0.18f, 0.12f, 0.07f, rearLight);

        // поворотники: signalDir -1 влево, +1 вправо, 0 выключены
        const bool signalOn = signalDir != 0 && fract(signalTime * 2.5f) < 0.5f;

        if (signalDir != 0) {
            const Color amberOn = { 1.0f, 0.65f, 0.05f, 1.0f };
            const Color amberOff = { 0.30f, 0.17f, 0.02f, 1.0f };
            const float sx = static_cast<float>(signalDir) * width * 0.42f;

            drawBox(sx, lightY, length * 0.505f, width * 0.07f, 0.10f, 0.075f, signalOn ? amberOn : amberOff);
            drawBox(sx, lightY, -length * 0.505f, width * 0.07f, 0.10f, 0.075f, signalOn ? amberOn : amberOff);
        }

        if (player && type == TrafficType::Sport) {
            // заднее крыло на двух стойках
            const float deck = carHullTop(shape, height, 0.90f);
            const Color wingCol = { 0.04f, 0.04f, 0.05f, 1.0f };

            drawBox(0.0f, deck + 0.17f, length * 0.43f, width * 0.86f, 0.05f, 0.26f, wingCol);
            drawBox(-width * 0.28f, deck + 0.085f, length * 0.43f, 0.05f, 0.17f, 0.10f, wingCol);
            drawBox(width * 0.28f, deck + 0.085f, length * 0.43f, 0.05f, 0.17f, 0.10f, wingCol);
        }

        // свечение стоп-сигналов и свет фар игрока
        beginGlow();

        // при торможении стоп-сигналы игрока горят ярче и больше
        const float brakeK = player ? gBrake : 0.0f;
        const float rg = 0.40f + 0.45f * gNightLevel + 0.40f * brakeK;
        const float rr = 0.55f + 0.40f * brakeK;

        drawGlow(-width * 0.27f, lightY, length * 0.53f, rr, { 1.0f, 0.10f, 0.05f, std::min(rg, 1.0f) });
        drawGlow(width * 0.27f, lightY, length * 0.53f, rr, { 1.0f, 0.10f, 0.05f, std::min(rg, 1.0f) });

        // ночью вокруг фар виден ореол
        if (gNightLevel > 0.05f) {
            const float fa = (player ? 0.55f : 0.35f) * gNightLevel;

            drawGlow(-width * 0.27f, lightY, -length * 0.53f, 0.50f, { 1.0f, 0.92f, 0.65f, fa });
            drawGlow(width * 0.27f, lightY, -length * 0.53f, 0.50f, { 1.0f, 0.92f, 0.65f, fa });
        }

        if (signalOn) {
            const float sx = static_cast<float>(signalDir) * width * 0.42f;

            drawGlow(sx, lightY, length * 0.53f, 0.45f, { 1.0f, 0.60f, 0.05f, 0.80f });
            drawGlow(sx, lightY, -length * 0.53f, 0.45f, { 1.0f, 0.60f, 0.05f, 0.80f });
        }

        if (brakeK > 0.05f) {
            drawGroundGlow(0.0f, length * 0.5f + 2.5f, width * 0.8f, 3.0f, { 1.0f, 0.10f, 0.05f, 0.30f * brakeK }, 0.06f);
        }

        if (gRain > 0.01f) {
            drawGroundGlow(-width * 0.27f, length * 0.5f + 2.2f, 0.4f, 2.6f, { 1.0f, 0.10f, 0.05f, 0.35f * gRain }, 0.05f);
            drawGroundGlow(width * 0.27f, length * 0.5f + 2.2f, 0.4f, 2.6f, { 1.0f, 0.10f, 0.05f, 0.35f * gRain }, 0.05f);
        }

        // в дождь свет фар отражается длинными полосами на мокрой дороге перед машиной
        if (gRain > 0.01f) {
            const float refl = (player ? 0.34f : 0.22f) * gRain * (0.45f + 0.55f * gNightLevel);

            drawGroundGlow(-width * 0.27f, -length * 0.5f - 3.6f, 0.38f, 3.4f, { 1.0f, 0.93f, 0.72f, refl }, 0.05f);
            drawGroundGlow(width * 0.27f, -length * 0.5f - 3.6f, 0.38f, 3.4f, { 1.0f, 0.93f, 0.72f, refl }, 0.05f);

            // отражение самого кузова: размытая полоса цвета машины на мокром асфальте
            if (kGfxWetExtras) {
                drawGroundGlow(0.0f, length * 0.5f + 1.6f, width * 0.62f, length * 0.62f, { body.r * 0.9f, body.g * 0.9f, body.b * 0.9f, 0.20f * gRain }, 0.049f);
            }
        }

        if (player) {
            drawGroundGlow(0.0f, 0.0f, width * 1.15f, length * 0.75f, { body.r, body.g, body.b, 0.10f + 0.22f * gNightLevel }, 0.07f);

            const float beamA = 0.10f + 0.20f * gNightLevel;
            const float fz = -length * 0.5f;

            glBegin(GL_TRIANGLES);
            for (int side = -1; side <= 1; side += 2) {
                const float lx = static_cast<float>(side) * width * 0.27f;

                setColor({ 1.0f, 0.95f, 0.75f, beamA });
                glVertex3f(lx, 0.05f, fz);

                setColor({ 1.0f, 0.95f, 0.75f, 0.0f });
                glVertex3f(lx - 2.2f, 0.05f, fz - 20.0f);
                glVertex3f(lx + 2.2f, 0.05f, fz - 20.0f);
            }
            glEnd();
        }
        else if (gNightLevel > 0.15f) {
            // машины потока: свет слабее и короче, чем у вашей, только в темноте
            const float beamA = 0.17f * gNightLevel;
            const float fz = -length * 0.5f;

            glBegin(GL_TRIANGLES);
            for (int side = -1; side <= 1; side += 2) {
                const float lx = static_cast<float>(side) * width * 0.27f;

                setColor({ 1.0f, 0.93f, 0.70f, beamA });
                glVertex3f(lx, 0.05f, fz);

                setColor({ 1.0f, 0.93f, 0.70f, 0.0f });
                glVertex3f(lx - 1.5f, 0.05f, fz - 13.0f);
                glVertex3f(lx + 1.5f, 0.05f, fz - 13.0f);
            }
            glEnd();
        }

        endGlow();

        glPopMatrix();
    }

    // масляное пятно: тёмный блин с радужным отливом
    void drawOilSlick(float x, float z, float t) {
        beginAlpha();

        drawGroundGlow(x, z, 1.55f, 2.9f, { 0.0f, 0.0f, 0.0f, 0.88f }, 0.018f);
        drawGroundGlow(x + 0.15f, z - 0.3f, 1.10f, 2.1f, { 0.0f, 0.0f, 0.0f, 0.55f }, 0.019f);

        // радужный отлив переливается со временем
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        const float s1 = std::sin(t * 1.3f);
        const float s2 = std::sin(t * 1.7f + 2.0f);

        drawGroundGlow(x - 0.45f + 0.15f * s1, z - 0.6f, 0.75f, 1.5f, { 0.25f, 0.45f, 1.0f, 0.30f }, 0.020f);
        drawGroundGlow(x + 0.40f + 0.15f * s2, z + 0.5f, 0.70f, 1.4f, { 0.95f, 0.30f, 0.70f, 0.25f }, 0.021f);
        drawGroundGlow(x + 0.05f, z + 1.2f + 0.2f * s1, 0.55f, 1.0f, { 0.30f, 0.90f, 0.45f, 0.20f }, 0.022f);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        endGlow();
    }

    // дорожные работы: полосатый барьер, мигающие лампы и конусы перед ним
    void drawRoadBlock(float x, float z, float t) {
        const Color orange = { 1.0f, 0.45f, 0.05f, 1.0f };
        const Color white = { 0.95f, 0.95f, 0.92f, 1.0f };
        const Color post = { 0.55f, 0.57f, 0.60f, 1.0f };

        // ножки барьера
        drawBox(x - 1.15f, 0.40f, z, 0.12f, 0.80f, 0.12f, post);
        drawBox(x + 1.15f, 0.40f, z, 0.12f, 0.80f, 0.12f, post);

        // две полосатые доски
        for (int row = 0; row < 2; ++row) {
            const float y = 0.62f + static_cast<float>(row) * 0.34f;

            for (int i = 0; i < 6; ++i) {
                const float sx = x - 1.125f + static_cast<float>(i) * 0.45f;
                const bool even = ((i + row) % 2) == 0;

                drawBox(sx + 0.225f, y, z, 0.45f, 0.30f, 0.10f, even ? orange : white);
            }
        }

        // конусы перед барьером (со стороны игрока)
        for (int i = -1; i <= 1; ++i) {
            const float cx = x + static_cast<float>(i) * 1.05f;
            const float cz = z + 3.2f - std::abs(static_cast<float>(i)) * 0.6f;

            drawBox(cx, 0.04f, cz, 0.40f, 0.08f, 0.40f, { 0.12f, 0.12f, 0.13f, 1.0f });
            drawBox(cx, 0.30f, cz, 0.26f, 0.44f, 0.26f, orange);
            drawBox(cx, 0.34f, cz, 0.28f, 0.08f, 0.28f, white);
        }

        // мигающие лампы сверху
        const bool phase = fract(t * 2.0f) < 0.5f;

        drawBox(x - 1.15f, 1.02f, z, 0.14f, 0.14f, 0.14f, phase ? Color{ 1.0f, 0.70f, 0.10f, 1.0f } : Color{ 0.35f, 0.20f, 0.05f, 1.0f });
        drawBox(x + 1.15f, 1.02f, z, 0.14f, 0.14f, 0.14f, phase ? Color{ 0.35f, 0.20f, 0.05f, 1.0f } : Color{ 1.0f, 0.70f, 0.10f, 1.0f });

        beginGlow();
        drawGlow(x - 1.15f, 1.02f, z + 0.2f, 0.9f, { 1.0f, 0.60f, 0.10f, phase ? 0.65f : 0.08f });
        drawGlow(x + 1.15f, 1.02f, z + 0.2f, 0.9f, { 1.0f, 0.60f, 0.10f, phase ? 0.08f : 0.65f });
        endGlow();
    }

    void drawCoin(float x, float z, float phase) {
        glPushMatrix();

        glTranslatef(x, 1.0f + std::sin(phase) * 0.18f, z);
        glRotatef(phase * 65.0f, 0.0f, 1.0f, 0.0f);

        glDisable(GL_LIGHTING);

        setColor({ 1.0f, 0.78f, 0.12f, 1.0f });

        constexpr int segments = 20;
        constexpr float radius = 0.46f;

        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0.0f, 0.0f, 0.0f);

        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            glVertex3f(std::cos(a) * radius, std::sin(a) * radius, 0.0f);
        }

        glEnd();

        setColor({ 1.0f, 0.94f, 0.36f, 1.0f });

        glBegin(GL_LINE_LOOP);

        for (int i = 0; i < segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            glVertex3f(
                std::cos(a) * radius * 0.72f,
                std::sin(a) * radius * 0.72f,
                0.01f
            );
        }

        glEnd();

        glEnable(GL_LIGHTING);

        glPopMatrix();
    }

    // внешний вид машин игрока: тип кузова и размеры (хитбокс в Game не меняется)
    struct CarLook {
        TrafficType type;
        float w;
        float l;
        float h;
    };

    CarLook carLook(int id) {
        switch (id) {
        case 0: return { TrafficType::Coupe, 1.66f, 3.76f, 0.91f };
        case 1: return { TrafficType::Sport, 1.80f, 4.02f, 0.82f };
        case 2: return { TrafficType::Coupe, 1.88f, 4.12f, 0.78f };
        case 3: return { TrafficType::Sport, 1.98f, 4.48f, 0.90f };
        case 4: return { TrafficType::Coupe, 2.04f, 4.22f, 0.72f };
        case 5: return { TrafficType::Sport, 2.14f, 4.46f, 0.67f };
        default: return { TrafficType::Coupe, 1.66f, 3.76f, 0.91f };
        }
    }

    // конус с острой вершиной вверх и наклонными нормалями
    void drawCone(float x, float y, float z, float radius, float height, int seg, Color c) {
        setColor(c);

        const float len = std::sqrt(height * height + radius * radius);

        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(x, y + height, z);

        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(seg) * 2.0f * kPi;
            const float ca = std::cos(a);
            const float sa = std::sin(a);

            glNormal3f(ca * height / len, radius / len, sa * height / len);
            glVertex3f(x + ca * radius, y, z + sa * radius);
        }

        glEnd();
    }

    // ель из трёх ярусов; ствол рисуется отдельно
    void drawPine(float x, float z, float s, Color c) {
        drawCone(x, 1.1f * s, z, 1.75f * s, 2.2f * s, 12, c);
        drawCone(x, 2.1f * s, z, 1.35f * s, 2.0f * s, 12, mixColor(c, { 0.05f, 0.45f, 0.20f, 1.0f }, 0.15f));
        drawCone(x, 3.0f * s, z, 0.90f * s, 1.8f * s, 12, mixColor(c, { 0.10f, 0.55f, 0.25f, 1.0f }, 0.25f));
    }

    // на снежной трассе хвоя и кусты припорошены
    Color snowTint(Color c) {
        return gTrack == 2 ? mixColor(c, { 0.88f, 0.93f, 1.0f, 1.0f }, 0.55f) : c;
    }

    Color trackBush(int k) {
        if (gTrack == 1) {
            return k == 0 ? Color{ 0.46f, 0.33f, 0.20f, 1.0f } : Color{ 0.40f, 0.29f, 0.18f, 1.0f };
        }

        if (gTrack == 2) {
            return { 0.78f, 0.82f, 0.90f, 1.0f };
        }

        return k == 0 ? Color{ 0.03f, 0.20f, 0.08f, 1.0f } : Color{ 0.03f, 0.18f, 0.075f, 1.0f };
    }

    // кактус: ствол с двумя ветками, верхушки закрыты плитками
    void drawCactus(float x, float z, float s, Color c) {
        drawCylinder(x, 0.0f, z, 0.28f * s, 2.4f * s, 10, c);
        drawBox(x, 2.4f * s, z, 0.56f * s, 0.06f, 0.56f * s, c);

        drawBox(x - 0.55f * s, 1.5f * s, z, 0.70f * s, 0.22f * s, 0.22f * s, c);
        drawCylinder(x - 0.85f * s, 1.5f * s, z, 0.18f * s, 0.9f * s, 8, c);
        drawBox(x - 0.85f * s, 2.4f * s, z, 0.36f * s, 0.06f, 0.36f * s, c);

        drawBox(x + 0.50f * s, 1.1f * s, z, 0.60f * s, 0.20f * s, 0.20f * s, c);
        drawCylinder(x + 0.78f * s, 1.1f * s, z, 0.16f * s, 0.7f * s, 8, c);
        drawBox(x + 0.78f * s, 1.8f * s, z, 0.32f * s, 0.06f, 0.32f * s, c);
    }

    // дальний план: в пустыне плоскогорья, на снежной трассе горы с белыми шапками
    void drawBackdrop() {
        for (int i = 0; i < 9; ++i) {
            const float x = -80.0f + static_cast<float>(i) * 20.0f;
            const float z = -150.0f - static_cast<float>(i % 3) * 18.0f;
            const float hgt = 14.0f + static_cast<float>((i * 7) % 5) * 5.0f;

            if (gTrack == 1) {
                drawBox(x, hgt * 0.5f, z, 24.0f, hgt, 14.0f, { 0.48f, 0.30f, 0.17f, 1.0f });
                drawBox(x, hgt + 0.7f, z, 25.0f, 1.4f, 15.0f, { 0.60f, 0.40f, 0.22f, 1.0f });
            }
            else {
                const float peak = hgt * 1.6f;

                drawCone(x, 0.0f, z, 22.0f, peak, 10, { 0.34f, 0.37f, 0.42f, 1.0f });
                drawCone(x, peak * 0.62f, z, 22.0f * 0.38f, peak * 0.38f, 10, { 0.92f, 0.95f, 1.0f, 1.0f });
            }
        }
    }

    // следы шин на дороге: тёмные полосы за задними колёсами
    void drawSkidMarks(float pz) {
        if (gSkids.empty()) {
            return;
        }

        beginAlpha();
        setColor({ 0.0f, 0.0f, 0.0f, 0.50f });

        glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);

        for (const auto& sk : gSkids) {
            const float z0 = pz - (sk.d0 - gDist);
            const float z1 = pz - (sk.d1 - gDist);

            for (int side = -1; side <= 1; side += 2) {
                const float o = static_cast<float>(side) * 0.72f;

                glVertex3f(sk.x0 + o - 0.08f, 0.018f, z0);
                glVertex3f(sk.x0 + o + 0.08f, 0.018f, z0);
                glVertex3f(sk.x1 + o + 0.08f, 0.018f, z1);
                glVertex3f(sk.x1 + o - 0.08f, 0.018f, z1);
            }
        }

        glEnd();

        endGlow();
    }

    // земля с текстурой; текстура привязана к земле и едет вместе с дорогой
    // (размер текстуры делит 72, поэтому при сбросе offset нет скачка)
    void drawTexturedGround(float x1, float z1, float x2, float z2, Color color, float y, GLuint tex, float size, float offset) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);

        setColor(color);

        glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(x1 / size, (z1 - offset) / size);
        glVertex3f(x1, y, z1);
        glTexCoord2f(x2 / size, (z1 - offset) / size);
        glVertex3f(x2, y, z1);
        glTexCoord2f(x2 / size, (z2 - offset) / size);
        glVertex3f(x2, y, z2);
        glTexCoord2f(x1 / size, (z2 - offset) / size);
        glVertex3f(x1, y, z2);
        glEnd();

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    // бонус на дороге: вращающийся кристалл с мягким свечением своего цвета
    void drawPowerUp(float x, float z, float phase, PowerType type) {
        Color c = { 0.25f, 0.85f, 1.0f, 1.0f };   // щит

        if (type == PowerType::Magnet) {
            c = { 1.0f, 0.50f, 0.12f, 1.0f };
        }
        else if (type == PowerType::Nitro) {
            c = { 0.35f, 1.0f, 0.45f, 1.0f };
        }

        const float y = 1.05f + std::sin(phase) * 0.20f;

        glPushMatrix();
        glTranslatef(x, y, z);

        beginGlow();
        drawGlow(0.0f, 0.0f, 0.3f, 1.5f, { c.r, c.g, c.b, 0.55f });
        drawGroundGlow(0.0f, 0.0f, 1.0f, 1.0f, { c.r, c.g, c.b, 0.30f }, 0.06f - y);
        endGlow();

        glDisable(GL_LIGHTING);
        glRotatef(phase * 55.0f, 0.0f, 1.0f, 0.0f);

        // два куба под углом дают восьмиконечную звезду, внутри белое ядро
        drawBox(0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.55f, c);

        glRotatef(45.0f, 0.0f, 1.0f, 0.0f);
        drawBox(0.0f, 0.0f, 0.0f, 0.55f, 0.55f, 0.55f, mixColor(c, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.35f));
        drawBox(0.0f, 0.0f, 0.0f, 0.28f, 0.28f, 0.28f, { 1.0f, 1.0f, 1.0f, 1.0f });

        glEnable(GL_LIGHTING);
        glPopMatrix();
    }

    void drawRoad(float offset, float worldTime) {
        const float pulse = 0.5f + 0.5f * std::sin(worldTime * 0.08f);

        Color grassA = { 0.028f, 0.11f, 0.060f, 1.0f };
        Color grassB = { 0.022f, 0.075f, 0.045f, 1.0f };

        if (gTrack == 1) {
            // песок
            grassA = { 0.46f, 0.34f, 0.18f, 1.0f };
            grassB = { 0.40f, 0.29f, 0.15f, 1.0f };
        }
        else if (gTrack == 2) {
            // снег
            grassA = { 0.60f, 0.65f, 0.74f, 1.0f };
            grassB = { 0.54f, 0.59f, 0.68f, 1.0f };
        }
        const Color grass = mixColor(grassA, grassB, pulse * 0.4f);

        if (kGfxTextures && gTexGrass != 0) {
            // текстура в среднем темнее на 20%, поэтому цвет подсвечен
            drawTexturedGround(
                -90.0f, kWorldNearZ, 90.0f, kWorldFarZ,
                { grass.r * 1.25f, grass.g * 1.25f, grass.b * 1.25f, 1.0f },
                0.0f, gTexGrass, 12.0f, offset
            );
        }
        else {
            drawGroundQuad(-90.0f, kWorldNearZ, 90.0f, kWorldFarZ, grass);
        }

        // полосы на траве: дают ощущение скорости (период 24 делит 72)
        {
            const Color mowTo =
                gTrack == 0 ? Color{ 0.05f, 0.17f, 0.09f, 1.0f } :
                (gTrack == 1 ? Color{ 0.58f, 0.44f, 0.24f, 1.0f } : Color{ 0.78f, 0.83f, 0.92f, 1.0f });
            const Color mow = mixColor(grass, mowTo, 0.45f);

            for (float z = kWorldNearZ + std::fmod(offset, 24.0f); z > kWorldFarZ; z -= 24.0f) {
                drawGroundQuad(-90.0f, z, -kRoadWidth * 0.62f, z - 12.0f, mow, 0.004f);
                drawGroundQuad(kRoadWidth * 0.62f, z, 90.0f, z - 12.0f, mow, 0.004f);
            }
        }

        if (kGfxTextures && gTexAsphalt != 0) {
            // мокрый асфальт темнее
            const float wetK = 1.25f * (1.0f - 0.30f * gRain);

            Color roadCol = { 0.065f * wetK, 0.070f * wetK, 0.077f * wetK, 1.0f };

            if (gTrack == 1) {
                roadCol = mixColor(roadCol, { 0.30f, 0.26f, 0.20f, 1.0f }, 0.25f);
            }
            else if (gTrack == 2) {
                // на снежной трассе дорога припорошена, при снегопаде светлее
                roadCol = mixColor(roadCol, { 0.45f, 0.48f, 0.52f, 1.0f }, 0.30f + 0.45f * gSnow);
            }

            drawTexturedGround(
                -kRoadWidth * 0.5f, kWorldNearZ, kRoadWidth * 0.5f, kWorldFarZ,
                roadCol,
                0.01f, gTexAsphalt, 6.0f, offset
            );
        }
        else {
            drawGroundQuad(
                -kRoadWidth * 0.5f,
                kWorldNearZ,
                kRoadWidth * 0.5f,
                kWorldFarZ,
                { 0.065f, 0.070f, 0.077f, 1.0f },
                0.01f
            );
        }

        drawGroundQuad(
            -kRoadWidth * 0.61f,
            kWorldNearZ,
            -kRoadWidth * 0.50f,
            kWorldFarZ,
            { 0.92f, 0.76f, 0.12f, 1.0f },
            0.025f
        );

        drawGroundQuad(
            kRoadWidth * 0.50f,
            kWorldNearZ,
            kRoadWidth * 0.61f,
            kWorldFarZ,
            { 0.92f, 0.76f, 0.12f, 1.0f },
            0.025f
        );

        for (int lane = 1; lane <= 2; ++lane) {
            const float x = -kLaneWidth * 1.5f + kLaneWidth * static_cast<float>(lane);

            for (float z = kWorldNearZ + std::fmod(offset, 12.0f); z > kWorldFarZ; z -= 12.0f) {
                drawBox(x, 0.045f, z, 0.13f, 0.045f, 5.0f, { 0.87f, 0.88f, 0.84f, 1.0f });
            }
        }

        for (float z = kWorldNearZ + std::fmod(offset, 12.0f); z > kWorldFarZ; z -= 12.0f) {
            drawBox(-6.35f, 0.33f, z, 0.15f, 0.66f, 0.15f, { 0.60f, 0.62f, 0.64f, 1.0f });
            drawBox(6.35f, 0.33f, z, 0.15f, 0.66f, 0.15f, { 0.60f, 0.62f, 0.64f, 1.0f });

            // светоотражатели: слева белые, справа красные
            drawBox(-6.35f, 0.56f, z + 0.08f, 0.10f, 0.10f, 0.03f, { 0.95f, 0.95f, 0.88f, 1.0f });
            drawBox(6.35f, 0.56f, z + 0.08f, 0.10f, 0.10f, 0.03f, { 0.95f, 0.18f, 0.12f, 1.0f });
        }

        // ночью отражатели светятся в свете фар
        if (gNightLevel > 0.15f) {
            beginGlow();

            for (float z = kWorldNearZ + std::fmod(offset, 12.0f); z > kWorldFarZ; z -= 12.0f) {
                drawGlow(-6.35f, 0.56f, z + 0.12f, 0.30f, { 1.0f, 1.0f, 0.90f, 0.45f * gNightLevel });
                drawGlow(6.35f, 0.56f, z + 0.12f, 0.30f, { 1.0f, 0.25f, 0.15f, 0.45f * gNightLevel });
            }

            endGlow();
        }

        // колеи от шин в центре каждой полосы
        for (int lane = 0; lane < 3; ++lane) {
            const float cx = (static_cast<float>(lane) - 1.0f) * kLaneWidth;
            const Color track = { 0.052f, 0.056f, 0.063f, 1.0f };

            drawGroundQuad(cx - 1.05f, kWorldNearZ, cx - 0.55f, kWorldFarZ, track, 0.013f);
            drawGroundQuad(cx + 0.55f, kWorldNearZ, cx + 1.05f, kWorldFarZ, track, 0.013f);
        }

        // лужи на мокрой дороге: привязаны к пройденному пути, поэтому не прыгают
        if (gRain > 0.05f) {
            constexpr float slot = 16.0f;

            const int k0 = static_cast<int>(std::floor((gDist - 12.0f) / slot));
            const int k1 = static_cast<int>(std::floor((gDist + 215.0f) / slot));

            beginAlpha();

            for (int k = k0; k <= k1; ++k) {
                const float fk = static_cast<float>(k);
                const float h1 = fract(std::sin(fk * 12.9898f) * 43758.5453f);
                const float h2 = fract(std::sin(fk * 78.233f) * 12345.6789f);
                const float h3 = fract(std::sin(fk * 39.346f) * 9999.0f);

                if (h1 < 0.35f) {
                    continue;
                }

                const float z = gPlayerZ - ((fk + h2) * slot - gDist);

                if (z > kWorldNearZ + 4.0f || z < kWorldFarZ) {
                    continue;
                }

                const float x = (h3 - 0.5f) * 8.6f;
                const float rx = 0.8f + h1 * 0.9f;
                const float rz = 2.0f + h2 * 2.6f;

                // тёмное основание лужи
                drawGroundGlow(x, z, rx, rz, { 0.0f, 0.0f, 0.0f, 0.40f * gRain }, 0.016f);

                // отражение неба
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                drawGroundGlow(x, z, rx * 0.85f, rz * 0.85f, { 0.40f, 0.52f, 0.70f, 0.22f * gRain }, 0.017f);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }

            endGlow();
        }

        // блеск мокрого асфальта, сильнее вдали
        if (gRain > 0.01f) {
            const float hw = kRoadWidth * 0.5f;

            beginGlow();
            glBegin(GL_QUADS);
            setColor({ 0.45f, 0.55f, 0.70f, 0.03f * gRain });
            glVertex3f(-hw, 0.015f, kWorldNearZ);
            glVertex3f(hw, 0.015f, kWorldNearZ);
            setColor({ 0.45f, 0.55f, 0.70f, 0.28f * gRain });
            glVertex3f(hw, 0.015f, -110.0f);
            glVertex3f(-hw, 0.015f, -110.0f);
            glEnd();
            endGlow();
        }
    }

    void drawEnvironment(float offset, float worldTime) {
        for (float z = 4.0f + std::fmod(offset, 24.0f); z > kWorldFarZ; z -= 24.0f) {
            const float sway = std::sin(worldTime * 0.6f + z * 0.03f) * 0.05f;

            if (gTrack != 1) {
                drawCylinder(-11.5f + sway, 0.0f, z, 0.22f, 2.0f, 10, { 0.28f, 0.17f, 0.07f, 1.0f });
                drawCylinder(11.5f - sway, 0.0f, z - 11.0f, 0.22f, 2.0f, 10, { 0.28f, 0.17f, 0.07f, 1.0f });
            }

            if (gTrack == 1) {
                const int cIdx = static_cast<int>(std::lround((gDist - z) / 24.0f));
                const float cA = fract(std::sin(static_cast<float>(cIdx) * 12.9898f) * 43758.5453f);
                const float cB = fract(std::sin(static_cast<float>(cIdx) * 78.233f + 3.0f) * 12345.6789f);

                drawCactus(-11.5f, z, 0.8f + 0.5f * cA, { 0.12f, 0.38f, 0.16f, 1.0f });
                drawCactus(11.5f, z - 11.0f, 0.8f + 0.5f * cB, { 0.10f, 0.34f, 0.14f, 1.0f });
            }
            else if (kGfxPines) {
                // номер дерева привязан к пройденному пути, поэтому размер не прыгает
                const int treeIdx = static_cast<int>(std::lround((gDist - z) / 24.0f));
                const float hA = fract(std::sin(static_cast<float>(treeIdx) * 12.9898f) * 43758.5453f);
                const float hB = fract(std::sin(static_cast<float>(treeIdx) * 78.233f + 3.0f) * 12345.6789f);

                drawPine(-11.5f + sway, z, 0.85f + 0.40f * hA, snowTint({ 0.035f, 0.28f, 0.10f, 1.0f }));
                drawPine(11.5f - sway, z - 11.0f, 0.85f + 0.40f * hB, snowTint({ 0.035f, 0.26f, 0.095f, 1.0f }));
            }
            else {
                drawBox(-11.5f + sway, 2.45f, z, 2.8f, 2.3f, 2.8f, { 0.035f, 0.28f, 0.10f, 1.0f });
                drawBox(11.5f - sway, 2.45f, z - 11.0f, 2.8f, 2.3f, 2.8f, { 0.035f, 0.26f, 0.095f, 1.0f });
            }

            // низкие кусты вдоль обочины
            drawBox(-9.3f, 0.30f, z - 6.0f, 1.4f, 0.60f, 1.1f, trackBush(0));
            drawBox(9.5f, 0.27f, z - 17.0f, 1.2f, 0.54f, 1.3f, trackBush(1));
        }

        for (float z = -6.0f + std::fmod(offset, 36.0f); z > kWorldFarZ; z -= 36.0f) {
            if (!inTunnelZ(z)) {
                drawBox(-8.0f, 1.9f, z, 0.16f, 3.8f, 0.16f, { 0.44f, 0.47f, 0.49f, 1.0f });
                drawBox(-7.2f, 3.65f, z, 1.45f, 0.14f, 0.14f, { 0.44f, 0.47f, 0.49f, 1.0f });
                drawBox(-6.48f, 3.48f, z, 0.32f, 0.20f, 0.32f, { 1.0f, 0.79f, 0.36f, 1.0f });

                drawLampLight(-6.48f, 3.48f, z, -4.6f);
            }

            if (!inTunnelZ(z - 17.0f)) {
                drawBox(8.0f, 1.9f, z - 17.0f, 0.16f, 3.8f, 0.16f, { 0.44f, 0.47f, 0.49f, 1.0f });
                drawBox(7.2f, 3.65f, z - 17.0f, 1.45f, 0.14f, 0.14f, { 0.44f, 0.47f, 0.49f, 1.0f });
                drawBox(6.48f, 3.48f, z - 17.0f, 0.32f, 0.20f, 0.32f, { 1.0f, 0.79f, 0.36f, 1.0f });

                drawLampLight(6.48f, 3.48f, z - 17.0f, 4.6f);
            }
        }

        if (gTrack != 0) {
            drawBackdrop();
        }
        else for (int i = 0; i < 9; ++i) {
            const float x = -65.0f + static_cast<float>(i % 5) * 31.0f;
            const float z = -105.0f - static_cast<float>(i) * 13.0f;
            const float h = 8.0f + static_cast<float>((i * 7) % 5) * 2.6f;

            drawBox(x, h * 0.5f, z, 15.0f, h, 8.0f, { 0.045f, 0.065f, 0.080f, 1.0f });

            // окна: ночью светятся, днём тёмное стекло
            glDisable(GL_LIGHTING);

            for (int r = 0; (1.6f + static_cast<float>(r) * 2.0f) < h - 0.8f; ++r) {
                for (int c = 0; c < 5; ++c) {
                    const float hv = fract(
                        std::sin(static_cast<float>(i) * 91.7f +
                            static_cast<float>(r) * 13.1f +
                            static_cast<float>(c) * 7.3f) * 43758.5453f
                    );

                    Color col = { 0.025f, 0.04f, 0.055f, 1.0f };

                    if (hv > 0.45f) {
                        const Color lit =
                            hv > 0.8f
                            ? Color{ 0.45f, 0.85f, 1.0f, 1.0f }
                        : Color{ 1.0f, 0.82f, 0.42f, 1.0f };

                        col = mixColor({ 0.30f, 0.40f, 0.50f, 1.0f }, lit, 0.12f + 0.88f * gNightLevel);
                    }

                    drawWall(
                        x + (static_cast<float>(c) - 2.0f) * 2.8f,
                        1.6f + static_cast<float>(r) * 2.0f,
                        z + 4.03f,
                        1.4f,
                        1.0f,
                        col
                    );
                }
            }

            glEnable(GL_LIGHTING);
        }
    }

    void drawOverlayRect(float x, float y, float w, float h, Color color) {
        setColor(color);

        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    }

    const std::array<int, 10> kDigitMasks = {
        0b1111110,
        0b0110000,
        0b1101101,
        0b1111001,
        0b0110011,
        0b1011011,
        0b1011111,
        0b1110000,
        0b1111111,
        0b1111011
    };

    void segment(float x, float y, float w, float h, Color color) {
        drawOverlayRect(x, y, w, h, color);
    }

    void drawDigit(char ch, float x, float y, float scale, Color color) {
        if (ch < '0' || ch > '9') {
            return;
        }

        const int mask = kDigitMasks[ch - '0'];

        const float t = 4.0f * scale;
        const float w = 24.0f * scale;
        const float h = 42.0f * scale;
        const float mid = y + (h - t) * 0.5f;
        const float bottom = y + h - t;

        if (mask & 0b1000000) segment(x + t, y, w - 2.0f * t, t, color);
        if (mask & 0b0100000) segment(x + w - t, y + t, t, h * 0.5f - t, color);
        if (mask & 0b0010000) segment(x + w - t, mid + t, t, h * 0.5f - t, color);
        if (mask & 0b0001000) segment(x + t, bottom, w - 2.0f * t, t, color);
        if (mask & 0b0000100) segment(x, mid + t, t, h * 0.5f - t, color);
        if (mask & 0b0000010) segment(x, y + t, t, h * 0.5f - t, color);
        if (mask & 0b0000001) segment(x + t, mid, w - 2.0f * t, t, color);
    }

    void drawLetter(char raw, float x, float y, float scale, Color color) {
        const char ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(raw))
            );

        const float w = 24.0f * scale;
        const float h = 42.0f * scale;
        const float t = 4.0f * scale;
        const float m = y + (h - t) * 0.5f;

        switch (ch) {
        case 'A':
            segment(x, y + t, t, h - t, color);
            segment(x + w - t, y + t, t, h - t, color);
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            break;

        case 'B':
            segment(x, y, t, h, color);
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            segment(x + w - t, y + t, t, h * 0.5f - t, color);
            segment(x + w - t, m + t, t, h * 0.5f - t, color);
            break;

        case 'C':
            segment(x + t, y, w - t, t, color);
            segment(x, y + t, t, h - 2.0f * t, color);
            segment(x + t, y + h - t, w - t, t, color);
            break;

        case 'D':
            segment(x, y, t, h, color);
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + w - t, y + t, t, h - 2.0f * t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            break;

        case 'E':
            segment(x, y, t, h, color);
            segment(x + t, y, w - t, t, color);
            segment(x + t, m, w - t, t, color);
            segment(x + t, y + h - t, w - t, t, color);
            break;

        case 'F':
            segment(x, y, t, h, color);
            segment(x + t, y, w - t, t, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            break;

        case 'G':
            segment(x + t, y, w - t, t, color);
            segment(x, y + t, t, h - 2.0f * t, color);
            segment(x + t, y + h - t, w - t, t, color);
            segment(x + w * 0.5f, m, w * 0.5f, t, color);
            segment(x + w - t, m, t, h * 0.5f - t, color);
            break;

        case 'H':
            segment(x, y, t, h, color);
            segment(x + w - t, y, t, h, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            break;

        case 'I':
            segment(x, y, w, t, color);
            segment(x + w * 0.5f - t * 0.5f, y, t, h, color);
            segment(x, y + h - t, w, t, color);
            break;

        case 'J':
            segment(x + w - t, y, t, h - t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            segment(x, y + h - 3.0f * t, t, 2.0f * t, color);
            break;

        case 'K':
            segment(x, y, t, h, color);
            for (int k = 0; k <= 3; ++k) {
                const float f = static_cast<float>(k) / 3.0f;
                // upper diagonal
                segment(
                    x + w - t - f * (w - 3.0f * t),
                    y + f * (m - y),
                    t, 2.0f * t, color
                );
                // lower diagonal
                segment(
                    x + 2.0f * t + f * (w - 3.0f * t),
                    m + f * (y + h - 2.0f * t - m),
                    t, 2.0f * t, color
                );
            }
            break;

        case 'L':
            segment(x, y, t, h, color);
            segment(x + t, y + h - t, w - t, t, color);
            break;

        case 'M':
            segment(x, y, t, h, color);
            segment(x + w - t, y, t, h, color);
            segment(x + t, y + t, t, t * 4.0f, color);
            segment(x + w - 2.0f * t, y + t, t, t * 4.0f, color);
            break;

        case 'N':
            segment(x, y, t, h, color);
            segment(x + w - t, y, t, h, color);
            segment(x + t, y + t, t, h - 2.0f * t, color);
            break;

        case 'O':
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            segment(x, y + t, t, h - 2.0f * t, color);
            segment(x + w - t, y + t, t, h - 2.0f * t, color);
            break;

        case 'P':
            segment(x, y, t, h, color);
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + w - t, y + t, t, h * 0.5f - t, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            break;

        case 'Q':
            segment(x + t, y, w - 2.0f * t, t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            segment(x, y + t, t, h - 2.0f * t, color);
            segment(x + w - t, y + t, t, h - 2.0f * t, color);
            segment(x + w * 0.5f, y + h * 0.6f, t * 1.5f, t * 1.5f, color);
            break;

        case 'R':
            drawLetter('P', x, y, scale, color);
            segment(x + w - t, m + t, t, h * 0.5f - t, color);
            break;

        case 'S':
            segment(x + t, y, w - t, t, color);
            segment(x, y + t, t, h * 0.5f - t, color);
            segment(x + t, m, w - 2.0f * t, t, color);
            segment(x + w - t, m + t, t, h * 0.5f - t, color);
            segment(x, y + h - t, w - t, t, color);
            break;

        case 'T':
            segment(x, y, w, t, color);
            segment(x + w * 0.5f - t * 0.5f, y, t, h, color);
            break;

        case 'U':
            segment(x, y, t, h - t, color);
            segment(x + w - t, y, t, h - t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            break;

        case 'V':
            segment(x, y, t, h - t, color);
            segment(x + w - t, y, t, h - t, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            break;

        case 'W':
            segment(x, y, t, h, color);
            segment(x + w - t, y, t, h, color);
            segment(x + w * 0.5f - t * 0.5f, y + h * 0.35f, t, h * 0.65f, color);
            segment(x + t, y + h - t, w - 2.0f * t, t, color);
            break;

        case 'X':
            for (int k = 0; k <= 4; ++k) {
                const float f = static_cast<float>(k) / 4.0f;
                segment(x + f * (w - t), y + f * (h - 2.0f * t), t, 2.0f * t, color);
                segment(x + w - t - f * (w - t), y + f * (h - 2.0f * t), t, 2.0f * t, color);
            }
            break;

        case 'Y':
            segment(x, y, t, h * 0.5f, color);
            segment(x + w - t, y, t, h * 0.5f, color);
            segment(x + w * 0.5f - t * 0.5f, m, t, h * 0.5f, color);
            break;

        case 'Z':
            segment(x, y, w, t, color);
            segment(x, y + h - t, w, t, color);
            for (int k = 0; k <= 4; ++k) {
                const float f = static_cast<float>(k) / 4.0f;
                segment(x + w - t - f * (w - t), y + t + f * (h - 4.0f * t), t, 2.0f * t, color);
            }
            break;

        default:
            break;
        }
    }

    void drawText(const std::string& text, float x, float y, float scale, Color color) {
        const float advance = 31.0f * scale;
        float cursor = x;

        for (char ch : text) {
            if (ch == ' ') {
                cursor += advance;
                continue;
            }

            if (ch >= '0' && ch <= '9') {
                drawDigit(ch, cursor, y, scale, color);
            }
            else {
                drawLetter(ch, cursor, y, scale, color);
            }

            cursor += advance;
        }
    }

    struct SkyColors {
        Color top;
        Color horizon;
    };

    // night: 0 = день, ~0.78 = глубокая ночь
    SkyColors skyColors(float night) {
        const Color dayTop = { 0.10f, 0.30f, 0.58f, 1.0f };
        const Color dayHorizon = { 0.52f, 0.70f, 0.84f, 1.0f };
        const Color nightTop = { 0.008f, 0.015f, 0.045f, 1.0f };
        const Color nightHorizon = { 0.07f, 0.10f, 0.19f, 1.0f };

        const float n = std::clamp(night / 0.78f, 0.0f, 1.0f);

        SkyColors c;
        c.top = mixColor(dayTop, nightTop, n);
        c.horizon = mixColor(dayHorizon, nightHorizon, n);

        // тёплый закатный оттенок на горизонте в середине перехода
        const float warm = 1.0f - std::abs(n * 2.0f - 1.0f);
        c.horizon = mixColor(c.horizon, { 0.95f, 0.45f, 0.22f, 1.0f }, warm * 0.35f);

        return c;
    }

    // мягкое эллиптическое облачко (экранные координаты)
    void drawSkyBlob(float cx, float cy, float rx, float ry, Color c) {
        constexpr int seg = 20;

        glBegin(GL_TRIANGLE_FAN);
        setColor(c);
        glVertex2f(cx, cy);
        setColor({ c.r, c.g, c.b, 0.0f });
        for (int i = 0; i <= seg; ++i) {
            const float a = static_cast<float>(i) / seg * 2.0f * kPi;
            glVertex2f(cx + std::cos(a) * rx, cy + std::sin(a) * ry);
        }
        glEnd();
    }

    void drawSky(int widthI, int heightI, float time, float night, const SkyColors& sky) {
        const float w = static_cast<float>(widthI);
        const float h = static_cast<float>(heightI);
        const float horizonY = h * 0.43f;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, w, h, 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // градиент неба: верх -> горизонт, ниже горизонта сплошной цвет тумана
        glBegin(GL_QUADS);
        setColor(sky.top);
        glVertex2f(0.0f, 0.0f);
        glVertex2f(w, 0.0f);
        setColor(sky.horizon);
        glVertex2f(w, horizonY);
        glVertex2f(0.0f, horizonY);

        glVertex2f(0.0f, horizonY);
        glVertex2f(w, horizonY);
        glVertex2f(w, h);
        glVertex2f(0.0f, h);
        glEnd();

        // солнце: днём высоко, на закате опускается и гаснет
        {
            const float dayK = (1.0f - gNightLevel) * (1.0f - gRain * 0.85f);

            if (dayK > 0.01f) {
                const float sx = w * 0.24f;
                const float sy = horizonY * (0.30f + 0.60f * gNightLevel);
                const float sr = 28.0f * (h / 720.0f);
                const float warmK = gNightLevel;

                const Color sunCol = mixColor({ 1.0f, 0.95f, 0.72f, 1.0f }, { 1.0f, 0.60f, 0.28f, 1.0f }, warmK);

                drawSkyBlob(sx, sy, sr * 6.0f, sr * 6.0f, { sunCol.r, sunCol.g, sunCol.b, 0.16f * dayK });
                drawSkyBlob(sx, sy, sr * 2.6f, sr * 2.6f, { sunCol.r, sunCol.g, sunCol.b, 0.30f * dayK });
                drawSkyBlob(sx, sy, sr * 1.15f, sr * 1.15f, { 1.0f, 0.98f, 0.90f, 0.95f * dayK });
            }
        }

        // ночью над горизонтом теплится свет далёкого города
        {
            const float cityK = std::clamp((night - 0.30f) / 0.35f, 0.0f, 1.0f) * (1.0f - gRain * 0.6f);

            if (cityK > 0.01f) {
                const float y0 = horizonY - h * 0.14f;

                glBegin(GL_QUADS);
                setColor({ 0.95f, 0.55f, 0.28f, 0.0f });
                glVertex2f(0.0f, y0);
                glVertex2f(w, y0);
                setColor({ 0.95f, 0.55f, 0.28f, 0.26f * cityK });
                glVertex2f(w, horizonY);
                glVertex2f(0.0f, horizonY);
                glEnd();
            }
        }

        // дальние холмы двумя слоями, тёмнее вблизи
        for (int layer = 0; layer < 2; ++layer) {
            const float lf = static_cast<float>(layer);
            const Color hc = mixColor(sky.horizon, { 0.03f, 0.06f, 0.07f, 1.0f }, layer == 0 ? 0.22f : 0.42f);
            const float baseH = h * (layer == 0 ? 0.10f : 0.07f);
            const float amp = h * (layer == 0 ? 0.040f : 0.025f);

            glBegin(GL_QUAD_STRIP);
            setColor(hc);

            for (float x = 0.0f; x <= w + 24.0f; x += 24.0f) {
                const float n =
                    std::sin(x * 0.0045f * (1.0f + lf * 0.6f) + lf * 2.1f) * 0.6f +
                    std::sin(x * 0.013f + lf * 5.0f) * 0.4f;

                glVertex2f(x, horizonY - (baseH + amp * n));
                glVertex2f(x, horizonY + 6.0f);
            }

            glEnd();
        }

        const float nightAmount = std::clamp((night - 0.30f) / 0.35f, 0.0f, 1.0f) * (1.0f - gRain * 0.9f);

        if (nightAmount > 0.01f) {
            // звёзды
            glPointSize(2.0f);
            glBegin(GL_POINTS);

            for (int i = 0; i < 140; ++i) {
                const float fi = static_cast<float>(i);
                const float sx = fract(std::sin(fi * 12.9898f) * 43758.5453f) * w;
                const float sy = fract(std::sin(fi * 78.233f) * 12345.6789f) * horizonY * 0.95f;
                const float twinkle = 0.6f + 0.4f * std::sin(time * 2.0f + fi * 1.7f);
                const float fade = 1.0f - (sy / horizonY) * 0.7f;

                setColor({ 1.0f, 1.0f, 1.0f, nightAmount * twinkle * fade });
                glVertex2f(sx, sy);
            }

            glEnd();

            // луна и мягкое свечение вокруг неё
            const float mx = w * 0.78f;
            const float my = h * 0.13f;
            const float r = 26.0f * (h / 720.0f);

            constexpr int seg = 32;

            glBegin(GL_TRIANGLE_FAN);
            setColor({ 0.75f, 0.82f, 1.0f, 0.30f * nightAmount });
            glVertex2f(mx, my);
            setColor({ 0.75f, 0.82f, 1.0f, 0.0f });
            for (int i = 0; i <= seg; ++i) {
                const float a = static_cast<float>(i) / seg * 2.0f * kPi;
                glVertex2f(mx + std::cos(a) * r * 4.0f, my + std::sin(a) * r * 4.0f);
            }
            glEnd();

            glBegin(GL_TRIANGLE_FAN);
            setColor({ 0.94f, 0.96f, 1.0f, nightAmount });
            glVertex2f(mx, my);
            for (int i = 0; i <= seg; ++i) {
                const float a = static_cast<float>(i) / seg * 2.0f * kPi;
                glVertex2f(mx + std::cos(a) * r, my + std::sin(a) * r);
            }
            glEnd();
        }

        // облака: медленно плывут, в дождь плотнее и темнее
        {
            const Color cloudDay = { 0.96f, 0.97f, 1.0f, 1.0f };
            const Color cloudNight = { 0.10f, 0.12f, 0.20f, 1.0f };
            const Color cloudStorm = { 0.30f, 0.32f, 0.36f, 1.0f };

            Color cc = mixColor(cloudDay, cloudNight, gNightLevel);
            cc = mixColor(cc, mixColor(cloudStorm, cloudNight, gNightLevel), gRain);

            const float alpha = 0.20f + 0.30f * gRain + 0.10f * (1.0f - gNightLevel);

            for (int i = 0; i < 9; ++i) {
                const float fi = static_cast<float>(i);
                const float hx = fract(std::sin(fi * 31.7f) * 43758.5453f);
                const float hy = fract(std::sin(fi * 17.3f) * 12345.6789f);
                const float hs = fract(std::sin(fi * 5.9f) * 9999.0f);

                const float cx = fract(hx + time * (0.003f + 0.003f * hs)) * (w + 500.0f) - 250.0f;
                const float cy = horizonY * (0.10f + 0.55f * hy);
                const float rx = (90.0f + 110.0f * hs) * (w / 1280.0f);
                const float ry = rx * 0.17f;

                for (int j = -1; j <= 1; ++j) {
                    const bool mid = (j == 0);

                    drawSkyBlob(
                        cx + static_cast<float>(j) * rx * 0.55f,
                        cy + (mid ? -ry * 0.3f : 0.0f),
                        rx * (mid ? 0.9f : 0.65f),
                        ry * (mid ? 1.3f : 1.0f),
                        { cc.r, cc.g, cc.b, alpha }
                    );
                }
            }
        }

        glDisable(GL_BLEND);

        glEnable(GL_FOG);
        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
    }

    // ---------- HUD helpers ----------

    int gPrevCombo = 0;
    float gComboPulseStart = -100.0f;
    float gMilestoneStart = -100.0f;   // всплывающая надпись на комбо 5, 10, 15
    int gMilestoneCombo = 0;

    // панель со срезанными углами и цветной рамкой
    void drawPanel(float x, float y, float w, float h, Color bg, Color accent) {
        const float c = 10.0f;

        const float px[8] = { x + c, x + w - c, x + w, x + w, x + w - c, x + c, x, x };
        const float py[8] = { y, y, y + c, y + h - c, y + h, y + h, y + h - c, y + c };

        setColor(bg);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(x + w * 0.5f, y + h * 0.5f);
        for (int i = 0; i < 8; ++i) {
            glVertex2f(px[i], py[i]);
        }
        glVertex2f(px[0], py[0]);
        glEnd();

        setColor(accent);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 8; ++i) {
            glVertex2f(px[i], py[i]);
        }
        glEnd();
    }

    // шкала из сегментов
    void drawSegBar(float x, float y, float w, float h, float value, int segments, Color fill, Color bg) {
        value = std::clamp(value, 0.0f, 1.0f);

        drawOverlayRect(x, y, w, h, bg);

        const float gap = 2.0f;
        const float inner = w - 6.0f;
        const float segW = (inner - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);

        const Color off = { fill.r * 0.15f, fill.g * 0.15f, fill.b * 0.15f, 0.65f };

        for (int i = 0; i < segments; ++i) {
            const float sx = x + 3.0f + static_cast<float>(i) * (segW + gap);
            const bool on = value * static_cast<float>(segments) > static_cast<float>(i) + 0.001f;

            drawOverlayRect(sx, y + 3.0f, segW, h - 6.0f, on ? fill : off);
        }
    }

    float textWidth(const std::string& text, float scale) {
        if (text.empty()) {
            return 0.0f;
        }
        return static_cast<float>(text.size()) * 31.0f * scale - 7.0f * scale;
    }

    void drawTextCentered(const std::string& text, float cx, float y, float scale, Color color) {
        drawText(text, cx - textWidth(text, scale) * 0.5f, y, scale, color);
    }

    // красная виньетка по краям экрана
    void drawEdgeGlow(float w, float h, Color c) {
        const float sideW = w * 0.14f;
        const float sideH = h * 0.22f;
        const Color clear = { c.r, c.g, c.b, 0.0f };

        glBegin(GL_QUADS);

        // левый
        setColor(c); glVertex2f(0.0f, 0.0f);
        setColor(clear); glVertex2f(sideW, 0.0f);
        setColor(clear); glVertex2f(sideW, h);
        setColor(c); glVertex2f(0.0f, h);

        // правый
        setColor(clear); glVertex2f(w - sideW, 0.0f);
        setColor(c); glVertex2f(w, 0.0f);
        setColor(c); glVertex2f(w, h);
        setColor(clear); glVertex2f(w - sideW, h);

        // верхний
        setColor(c); glVertex2f(0.0f, 0.0f);
        setColor(c); glVertex2f(w, 0.0f);
        setColor(clear); glVertex2f(w, sideH);
        setColor(clear); glVertex2f(0.0f, sideH);

        // нижний
        setColor(clear); glVertex2f(0.0f, h - sideH);
        setColor(clear); glVertex2f(w, h - sideH);
        setColor(c); glVertex2f(w, h);
        setColor(c); glVertex2f(0.0f, h);

        glEnd();
    }

    // ---------- Эффекты скорости ----------

    struct Particle {
        float x, y, z;
        float vx, vy, vz;
        float life;
        float maxLife;
        float size;
        Color color;
        float gravity = 2.0f;   // отрицательная: частица поднимается (дым)
    };

    std::vector<Particle> gParticles;

    float gBoostBlend = 0.0f;   // 0..1, плавно растёт при бусте
    float gFovExtra = 0.0f;     // добавка к углу обзора
    float gShake = 0.0f;        // тряска при ударе
    float gHitFlash = 0.0f;     // красная вспышка при ударе
    float gPrevHealth = 100.0f;
    float gLastTime = 0.0f;
    float gDustAcc = 0.0f;
    float gEmberAcc = 0.0f;
    float gSprayAcc = 0.0f;
    float gSmokeAcc = 0.0f;
    unsigned int gSeed = 12345u;

    float rnd() {
        gSeed = gSeed * 1664525u + 1013904223u;
        return static_cast<float>(gSeed >> 8) * (1.0f / 16777216.0f);
    }

    void emitParticle(float x, float y, float z, float vx, float vy, float vz, float life, float size, Color c, float gravity = 2.0f) {
        if (gParticles.size() > 700) {
            return;
        }
        gParticles.push_back({ x, y, z, vx, vy, vz, life, life, size, c, gravity });
    }

    void updateEffects(const Game& game) {
        const float t = game.worldTime();
        float dt = t - gLastTime;

        if (dt < 0.0f) {
            // перезапуск игры
            gParticles.clear();
            gSkids.clear();
            gSkidPrev = false;
            gShake = 0.0f;
            gHitFlash = 0.0f;
            gBoostBlend = 0.0f;
            gPrevHealth = game.health();
            dt = 0.0f;
        }

        dt = std::min(dt, 0.25f);
        gLastTime = t;

        // пройденный путь теперь считает Game
        gDist = game.distance();

        gBrake += ((game.braking() ? 1.0f : 0.0f) - gBrake) * std::min(1.0f, dt * 14.0f);
        gPlayerZ = game.playerZ();

        // где сейчас туннель относительно машины
        gTunnelValid = false;
        gTunnelInside = game.tunnelInside();

        float ta = 0.0f;
        float tb = 0.0f;

        if (findTunnel(gDist, ta, tb)) {
            const float lo = std::max(ta, gDist - 18.0f);
            const float hi = std::min(tb, gDist + 215.0f);

            if (lo < hi) {
                const float pzT = game.playerZ();

                gTunnelValid = true;
                gTunLoD = lo;
                gTunHiD = hi;
                gTunZNear = pzT - (lo - gDist);
                gTunZFar = pzT - (hi - gDist);

                gTunStartVisible = ta >= lo;
                gTunEndVisible = tb <= hi;
                gTunStartZ = pzT - (ta - gDist);
                gTunEndZ = pzT - (tb - gDist);
            }
        }

        const float speed = game.speedKmh();
        const float speedMs = speed / 3.6f;
        const float px = game.playerX();
        const float pz = game.playerZ();

        const float boostTarget = game.boosting() ? 1.0f : 0.0f;
        gBoostBlend += (boostTarget - gBoostBlend) * std::min(1.0f, dt * 6.0f);

        gFovExtra = game.speedFxScale() * (std::clamp((speed - 150.0f) / 200.0f, 0.0f, 1.0f) * 5.0f + gBoostBlend * 7.0f);

        gShake *= std::exp(-dt * 5.0f);
        gHitFlash *= std::exp(-dt * 4.0f);

        // удар: тряска, вспышка и искры
        if (game.health() < gPrevHealth - 0.5f) {
            gShake = 1.0f;
            gHitFlash = 1.0f;

            for (int i = 0; i < 45; ++i) {
                emitParticle(
                    px, 0.7f, pz,
                    (rnd() - 0.5f) * 10.0f, rnd() * 6.0f, (rnd() - 0.5f) * 10.0f,
                    0.5f + rnd() * 0.4f, 0.14f,
                    { 1.0f, 0.65f, 0.20f, 1.0f }
                );
            }
        }
        gPrevHealth = game.health();

        if (dt <= 0.0f) {
            return;
        }

        // следы шин и дым: торможение на скорости, занос на масле, резкая смена полосы
        {
            const bool skidding =
                game.state() == GameState::Running &&
                ((game.braking() && speed > 120.0f) || game.oilActive() || std::abs(game.playerYaw()) > 7.5f);

            if (kGfxSkidMarks && skidding) {
                if (gSkidPrev && gDist >= gSkidPrevD) {
                    gSkids.push_back({ gSkidPrevX, gSkidPrevD, px, gDist });
                }

                gSkidPrev = true;
                gSkidPrevX = px;
                gSkidPrevD = gDist;

                gSkidSmokeAcc += dt * 40.0f;

                while (gSkidSmokeAcc >= 1.0f) {
                    gSkidSmokeAcc -= 1.0f;

                    const float side = rnd() < 0.5f ? -0.72f : 0.72f;

                    emitParticle(
                        px + side, 0.12f, pz + 1.2f,
                        (rnd() - 0.5f) * 1.5f, 0.5f + rnd() * 0.6f, speedMs * 0.25f + rnd() * 2.0f,
                        0.6f + rnd() * 0.4f, 0.22f,
                        { 0.82f, 0.83f, 0.86f, 0.28f },
                        -0.3f
                    );
                }
            }
            else {
                gSkidPrev = false;
                gSkidSmokeAcc = 0.0f;
            }

            // следы остаются позади и пропадают, когда уходят за экран
            gSkids.erase(
                std::remove_if(
                    gSkids.begin(),
                    gSkids.end(),
                    [](const SkidSeg& sk) { return sk.d1 < gDist - 16.0f; }
                ),
                gSkids.end()
            );
        }

        // пыль из-под колёс на высокой скорости
        gDustAcc += dt * 45.0f * std::clamp((speed - 100.0f) / 200.0f, 0.0f, 1.0f);
        while (gDustAcc >= 1.0f) {
            gDustAcc -= 1.0f;
            const float side = rnd() < 0.5f ? -0.75f : 0.75f;

            emitParticle(
                px + side, 0.15f + rnd() * 0.2f, pz + 1.2f,
                (rnd() - 0.5f) * 2.0f, 0.4f + rnd() * 0.8f, speedMs * 0.35f + rnd() * 3.0f,
                0.5f + rnd() * 0.3f, 0.18f,
                gTrack == 1 ? Color{ 0.78f, 0.64f, 0.42f, 0.40f } : Color{ 0.55f, 0.58f, 0.65f, 0.35f }
            );
        }

        // брызги из-под колёс на мокрой дороге
        if (gRain > 0.15f) {
            gSprayAcc += dt * 90.0f * gRain * std::clamp(speed / 200.0f, 0.2f, 1.5f);

            while (gSprayAcc >= 1.0f) {
                gSprayAcc -= 1.0f;
                const float side = rnd() < 0.5f ? -0.80f : 0.80f;

                emitParticle(
                    px + side, 0.10f, pz + 1.3f,
                    (rnd() - 0.5f) * 2.6f, 1.2f + rnd() * 2.2f, speedMs * 0.30f + rnd() * 3.0f,
                    0.35f + rnd() * 0.25f, 0.10f,
                    { 0.78f, 0.88f, 1.0f, 0.45f }
                );
            }
        }
        else {
            gSprayAcc = 0.0f;
        }

        // брызги из-под колёс машин потока в дождь
        if (kGfxWetExtras && gRain > 0.25f) {
            for (const auto& v : game.traffic()) {
                if (v.z > pz + 2.0f || v.z < pz - 85.0f) {
                    continue;
                }

                if (rnd() > dt * 14.0f * gRain) {
                    continue;
                }

                const float side = rnd() < 0.5f ? -0.5f : 0.5f;
                const float rel = game.speed() + v.relativeSpeed;

                emitParticle(
                    v.x + side * v.width * 0.9f, 0.10f, v.z + v.length * 0.5f,
                    (rnd() - 0.5f) * 2.0f, 1.0f + rnd() * 1.8f, rel * 0.30f + rnd() * 2.0f,
                    0.35f + rnd() * 0.25f, 0.10f,
                    { 0.78f, 0.88f, 1.0f, 0.35f }
                );
            }
        }

        // дым из-под капота при низком здоровье, при совсем низком ещё и искры
        const float hpNow = game.health();

        if (hpNow <= 45.0f && game.state() == GameState::Running) {
            const float sev = 1.0f - hpNow / 45.0f;

            gSmokeAcc += dt * (14.0f + 40.0f * sev);

            while (gSmokeAcc >= 1.0f) {
                gSmokeAcc -= 1.0f;

                emitParticle(
                    px + (rnd() - 0.5f) * 0.5f, 0.85f, pz - 1.0f,
                    (rnd() - 0.5f) * 0.8f, 0.8f + rnd() * 0.8f, speedMs * 0.30f + rnd() * 2.0f,
                    0.8f + rnd() * 0.5f, 0.22f,
                    { 0.62f, 0.64f, 0.68f, 0.20f + 0.15f * sev },
                    -0.8f
                );

                if (hpNow <= 20.0f && rnd() < 0.25f) {
                    emitParticle(
                        px + (rnd() - 0.5f) * 0.5f, 0.85f, pz - 1.0f,
                        (rnd() - 0.5f) * 1.6f, 1.0f + rnd() * 1.5f, speedMs * 0.30f + rnd() * 3.0f,
                        0.4f + rnd() * 0.3f, 0.08f,
                        { 1.0f, 0.60f, 0.20f, 0.9f }
                    );
                }
            }
        }
        else {
            gSmokeAcc = 0.0f;
        }

        // искры из выхлопа при бусте
        if (game.boosting()) {
            gEmberAcc += dt * 120.0f;
            while (gEmberAcc >= 1.0f) {
                gEmberAcc -= 1.0f;
                const float side = rnd() < 0.5f ? -0.22f : 0.22f;
                const bool orange = rnd() < 0.3f;

                emitParticle(
                    px + side, 0.34f, pz + 1.95f,
                    (rnd() - 0.5f) * 1.2f, (rnd() - 0.3f) * 0.8f, speedMs * 0.3f + rnd() * 8.0f,
                    0.35f + rnd() * 0.25f, 0.12f,
                    orange ? Color{ 1.0f, 0.60f, 0.20f, 0.9f } : Color{ 0.35f, 0.80f, 1.0f, 0.9f }
                );
            }
        }
        else {
            gEmberAcc = 0.0f;
        }

        for (auto& p : gParticles) {
            p.life -= dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.z += p.vz * dt;
            p.vy -= p.gravity * dt;

            if (p.y < 0.02f) {
                p.y = 0.02f;
                p.vy = 0.0f;
            }
        }

        gParticles.erase(
            std::remove_if(
                gParticles.begin(),
                gParticles.end(),
                [](const Particle& p) { return p.life <= 0.0f; }
            ),
            gParticles.end()
        );
    }

    void drawParticles() {
        if (gParticles.empty()) {
            return;
        }

        beginGlow();

        glBegin(GL_QUADS);

        for (const auto& p : gParticles) {
            const float a = p.life / p.maxLife;
            const float sz = p.size * (0.7f + 0.8f * (1.0f - a));

            setColor({ p.color.r, p.color.g, p.color.b, p.color.a * a });

            glVertex3f(p.x - sz, p.y - sz, p.z);
            glVertex3f(p.x + sz, p.y - sz, p.z);
            glVertex3f(p.x + sz, p.y + sz, p.z);
            glVertex3f(p.x - sz, p.y + sz, p.z);
        }

        glEnd();

        endGlow();
    }

    // молния: редкие короткие вспышки при сильном дожде
    float lightningFlash(float t) {
        if (gRain < 0.6f) {
            return 0.0f;
        }

        const float k = std::floor(t / 9.0f);
        if (fract(std::sin(k * 17.31f) * 43758.5453f) < 0.4f) {
            return 0.0f;
        }

        const float c = fract(t / 9.0f);
        if (c > 0.10f) {
            return 0.0f;
        }

        return (1.0f - c * 10.0f) * (0.65f + 0.35f * std::sin(c * 140.0f));
    }

    // капли дождя на экране
    void drawRain(float w, float h, float t, float speedKmh) {
        if (gRain < 0.02f) {
            return;
        }

        const int count = static_cast<int>(40.0f + 190.0f * gRain);
        const float cx = w * 0.5f;

        glLineWidth(1.2f);
        glBegin(GL_LINES);

        for (int i = 0; i < count; ++i) {
            const float fi = static_cast<float>(i);
            const float hx = fract(std::sin(fi * 12.9898f + 1.0f) * 43758.5453f);
            const float hy = fract(std::sin(fi * 78.233f) * 12345.6789f);
            const float hl = fract(std::sin(fi * 39.346f) * 9999.0f);

            const float fall = (1.2f + hl * 0.8f) * (1.0f + speedKmh / 900.0f);
            const float x = hx * w;
            const float y = fract(hy + t * fall) * h;
            const float len = 14.0f + hl * 26.0f;
            const float slant = (x - cx) / w * 0.5f;

            setColor({ 0.70f, 0.80f, 0.95f, 0.0f });
            glVertex2f(x - slant * len, y - len);

            setColor({ 0.70f, 0.80f, 0.95f, 0.30f * gRain * (0.5f + 0.5f * hl) });
            glVertex2f(x, y);
        }

        glEnd();
        glLineWidth(1.0f);
    }

    // снегопад: медленные белые точки, дрейфуют вбок
    void drawSnow(float w, float h, float t, float speedKmh) {
        if (gSnow < 0.02f) {
            return;
        }

        const int count = static_cast<int>(60.0f + 260.0f * gSnow);

        glPointSize(2.4f);
        glBegin(GL_POINTS);

        for (int i = 0; i < count; ++i) {
            const float fi = static_cast<float>(i);
            const float hx = fract(std::sin(fi * 12.9898f + 1.0f) * 43758.5453f);
            const float hy = fract(std::sin(fi * 78.233f) * 12345.6789f);
            const float hl = fract(std::sin(fi * 39.346f) * 9999.0f);

            const float fall = (0.10f + 0.16f * hl) * (1.0f + speedKmh / 700.0f);
            const float x = fract(hx + std::sin(t * 0.6f + fi) * 0.015f) * w;
            const float y = fract(hy + t * fall) * h;

            setColor({ 0.95f, 0.97f, 1.0f, 0.35f + 0.45f * hl });
            glVertex2f(x, y);
        }

        glEnd();
        glPointSize(1.0f);
    }

    // линии скорости, расходящиеся от точки схода
    void drawSpeedLines(float w, float h, float t, float speedKmh, float fxScale) {
        const float intensity = fxScale * (
            std::clamp((speedKmh - 170.0f) / 150.0f, 0.0f, 1.0f) * 0.30f +
            gBoostBlend * 0.45f);

        if (intensity < 0.01f) {
            return;
        }

        const float cx = w * 0.5f;
        const float cy = h * 0.42f;
        const float rMax = 0.5f * std::sqrt(w * w + h * h);

        glLineWidth(1.5f);
        glBegin(GL_LINES);

        for (int i = 0; i < 44; ++i) {
            const float fi = static_cast<float>(i);
            const float ang = fract(std::sin(fi * 12.9898f) * 43758.5453f) * 2.0f * kPi;
            const float spd = 0.8f + fract(std::sin(fi * 4.1f) * 1000.0f) * 0.8f;
            const float ph = fract(std::sin(fi * 78.233f) * 12345.67f);

            const float r = fract(ph + t * spd * (1.2f + gBoostBlend));

            const float r0 = rMax * (0.30f + 0.70f * r);
            const float r1 = r0 + rMax * (0.04f + 0.12f * r);

            const float dx = std::cos(ang);
            const float dy = std::sin(ang);

            setColor({ 0.85f, 0.95f, 1.0f, 0.0f });
            glVertex2f(cx + dx * r0, cy + dy * r0);

            setColor({ 0.85f, 0.95f, 1.0f, intensity * r });
            glVertex2f(cx + dx * r1, cy + dy * r1);
        }

        glEnd();
        glLineWidth(1.0f);
    }

    // мигающий красно-синий свет полиции на дороге вокруг машины игрока
    void drawPoliceGlow(float px, float pz, float intensity, float t) {
        if (intensity < 0.01f) {
            return;
        }

        const bool phase = fract(t * 3.0f) < 0.5f;
        const float a = 0.30f * intensity;

        beginGlow();

        drawGroundGlow(
            px - 2.2f, pz + 1.5f, 3.2f, 6.0f,
            { 1.0f, 0.10f, 0.08f, phase ? a : a * 0.15f },
            0.06f
        );

        drawGroundGlow(
            px + 2.2f, pz + 1.5f, 3.2f, 6.0f,
            { 0.15f, 0.35f, 1.0f, phase ? a * 0.15f : a },
            0.06f
        );

        endGlow();
    }

    // зеркало заднего вида: полицейская машина приближается сзади
    void drawRearMirror(float x, float y, float w, float h, int winH, float gap, float policeRel, float t) {
        const bool phase = fract(t * 3.0f) < 0.5f;

        const Color red = { 1.0f, 0.12f, 0.10f, 1.0f };
        const Color blue = { 0.18f, 0.40f, 1.0f, 1.0f };
        const Color redDim = { 0.28f, 0.05f, 0.05f, 1.0f };
        const Color blueDim = { 0.05f, 0.08f, 0.28f, 1.0f };

        drawPanel(x, y, w, h, { 0.01f, 0.012f, 0.018f, 0.92f }, phase ? red : blue);

        const float ix = x + 8.0f;
        const float iy = y + 8.0f;
        const float iw = w - 16.0f;
        const float ih = h - 16.0f;

        glEnable(GL_SCISSOR_TEST);
        glScissor(
            static_cast<int>(ix),
            winH - static_cast<int>(iy + ih),
            static_cast<int>(iw),
            static_cast<int>(ih)
        );

        const float horizon = iy + ih * 0.38f;
        const float bottom = iy + ih;
        const float cx = ix + iw * 0.5f;
        const float topHalf = iw * 0.06f;
        const float botHalf = iw * 0.50f;

        // небо и трава
        glBegin(GL_QUADS);
        setColor({ 0.10f, 0.13f, 0.20f, 1.0f });
        glVertex2f(ix, iy);
        glVertex2f(ix + iw, iy);
        setColor({ 0.28f, 0.32f, 0.40f, 1.0f });
        glVertex2f(ix + iw, horizon);
        glVertex2f(ix, horizon);

        setColor({ 0.03f, 0.10f, 0.05f, 1.0f });
        glVertex2f(ix, horizon);
        glVertex2f(ix + iw, horizon);
        glVertex2f(ix + iw, bottom);
        glVertex2f(ix, bottom);

        // дорога
        setColor({ 0.10f, 0.11f, 0.13f, 1.0f });
        glVertex2f(cx - topHalf, horizon);
        glVertex2f(cx + topHalf, horizon);
        glVertex2f(cx + botHalf, bottom);
        glVertex2f(cx - botHalf, bottom);
        glEnd();

        // разметка полос
        setColor({ 0.80f, 0.80f, 0.70f, 0.70f });
        glLineWidth(1.5f);
        glBegin(GL_LINES);
        for (int k = -1; k <= 1; k += 2) {
            glVertex2f(cx + static_cast<float>(k) * topHalf * 0.32f, horizon);
            glVertex2f(cx + static_cast<float>(k) * botHalf * 0.32f, bottom);
        }
        glEnd();

        // полицейская машина: чем меньше расстояние, тем она ниже и крупнее
        const float u = 1.0f / (1.0f + std::max(gap, 0.0f) * 0.06f);
        const float py = horizon + (bottom - horizon) * u;
        const float hw = topHalf + (botHalf - topHalf) * u;
        const float carW = hw * 0.34f * (1.0f + 1.2f * u * u);
        const float carH = carW * 0.62f;
        const float carX = cx + std::clamp(policeRel, -1.0f, 1.0f) * hw * 0.82f;
        const float barH = carH * 0.14f;
        const float barY = py - carH - barH;

        drawOverlayRect(carX - carW * 0.5f, py - carH, carW, carH, { 0.92f, 0.93f, 0.97f, 1.0f });
        drawOverlayRect(carX - carW * 0.5f, py - carH * 0.40f, carW, carH * 0.40f, { 0.05f, 0.06f, 0.08f, 1.0f });
        drawOverlayRect(carX - carW * 0.36f, py - carH * 0.92f, carW * 0.72f, carH * 0.38f, { 0.04f, 0.10f, 0.16f, 1.0f });

        const Color headLight = { 1.0f, 0.95f, 0.70f, 1.0f };
        drawOverlayRect(carX - carW * 0.45f, py - carH * 0.34f, carW * 0.18f, carH * 0.15f, headLight);
        drawOverlayRect(carX + carW * 0.27f, py - carH * 0.34f, carW * 0.18f, carH * 0.15f, headLight);

        // мигалка
        drawOverlayRect(carX - carW * 0.36f, barY, carW * 0.36f, barH, phase ? red : redDim);
        drawOverlayRect(carX, barY, carW * 0.36f, barH, phase ? blueDim : blue);

        // свечение мигалки и фар
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        drawGlow(carX - carW * 0.18f, barY + barH * 0.5f, 0.0f, carW * 0.75f, { 1.0f, 0.10f, 0.08f, phase ? 0.75f : 0.10f });
        drawGlow(carX + carW * 0.18f, barY + barH * 0.5f, 0.0f, carW * 0.75f, { 0.15f, 0.35f, 1.0f, phase ? 0.10f : 0.75f });

        drawGlow(carX - carW * 0.36f, py - carH * 0.26f, 0.0f, carW * 0.30f, { 1.0f, 0.95f, 0.70f, 0.55f });
        drawGlow(carX + carW * 0.36f, py - carH * 0.26f, 0.0f, carW * 0.30f, { 1.0f, 0.95f, 0.70f, 0.55f });

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        drawText(std::to_string(static_cast<int>(std::max(gap, 0.0f))) + " M", ix + 8.0f, iy + 6.0f, 0.26f, { 0.85f, 0.92f, 1.0f, 0.95f });

        glDisable(GL_SCISSOR_TEST);
        glLineWidth(1.0f);
    }

    void drawPortal(float z) {
        const Color concrete = { 0.13f, 0.13f, 0.15f, 1.0f };

        drawBox(-14.0f, 6.0f, z, 13.0f, 12.0f, 2.0f, concrete);
        drawBox(14.0f, 6.0f, z, 13.0f, 12.0f, 2.0f, concrete);
        drawBox(0.0f, 10.5f, z, 15.0f, 3.0f, 2.0f, concrete);

        // неоновая арка
        drawBox(0.0f, 8.7f, z + 1.05f, 14.0f, 0.22f, 0.12f, { 1.0f, 0.25f, 0.75f, 1.0f });
        drawBox(-7.1f, 4.3f, z + 1.05f, 0.22f, 8.6f, 0.12f, { 0.20f, 0.90f, 1.0f, 1.0f });
        drawBox(7.1f, 4.3f, z + 1.05f, 0.22f, 8.6f, 0.12f, { 0.20f, 0.90f, 1.0f, 1.0f });
    }

    void drawTunnel(float pz) {
        if (!gTunnelValid) {
            return;
        }

        const float zN = gTunZNear;
        const float zF = gTunZFar;
        const float len = zN - zF;
        const float cz = (zN + zF) * 0.5f;

        const float wallX = 7.0f;
        const float wallH = 8.5f;

        glDisable(GL_LIGHTING);

        // стены и потолок
        drawBox(-wallX - 0.25f, wallH * 0.5f, cz, 0.5f, wallH, len, { 0.09f, 0.10f, 0.12f, 1.0f });
        drawBox(wallX + 0.25f, wallH * 0.5f, cz, 0.5f, wallH, len, { 0.09f, 0.10f, 0.12f, 1.0f });
        drawBox(0.0f, wallH + 0.25f, cz, 2.0f * wallX + 1.5f, 0.5f, len, { 0.06f, 0.065f, 0.08f, 1.0f });

        // неоновые полосы вдоль стен
        drawBox(-wallX + 0.1f, 7.3f, cz, 0.12f, 0.12f, len, { 0.20f, 0.90f, 1.0f, 1.0f });
        drawBox(wallX - 0.1f, 7.3f, cz, 0.12f, 0.12f, len, { 1.0f, 0.25f, 0.75f, 1.0f });
        drawBox(-wallX + 0.1f, 0.6f, cz, 0.12f, 0.12f, len, { 1.0f, 0.70f, 0.20f, 1.0f });
        drawBox(wallX - 0.1f, 0.6f, cz, 0.12f, 0.12f, len, { 1.0f, 0.70f, 0.20f, 1.0f });

        // рёбра обделки, дают ощущение скорости
        const float firstD = std::ceil(gTunLoD / 12.0f) * 12.0f;

        for (float d = firstD; d < gTunHiD; d += 12.0f) {
            const float z = pz - (d - gDist);
            const int idx = static_cast<int>(std::lround(d / 12.0f));

            drawBox(-wallX + 0.15f, wallH * 0.5f, z, 0.3f, wallH, 0.8f, { 0.15f, 0.16f, 0.19f, 1.0f });
            drawBox(wallX - 0.15f, wallH * 0.5f, z, 0.3f, wallH, 0.8f, { 0.15f, 0.16f, 0.19f, 1.0f });
            drawBox(0.0f, wallH - 0.15f, z, 2.0f * wallX, 0.3f, 0.8f, { 0.12f, 0.13f, 0.16f, 1.0f });

            // потолочные лампы
            if (idx % 2 == 0) {
                drawBox(0.0f, wallH - 0.05f, z, 2.4f, 0.06f, 1.2f, { 1.0f, 0.95f, 0.80f, 1.0f });
            }
        }

        if (gTunStartVisible) {
            drawPortal(gTunStartZ);
        }

        if (gTunEndVisible) {
            drawPortal(gTunEndZ - 1.0f);
        }

        // свет ламп на асфальте
        for (float d = firstD; d < gTunHiD; d += 12.0f) {
            const int idx = static_cast<int>(std::lround(d / 12.0f));

            if (idx % 2 == 0) {
                const float z = pz - (d - gDist);

                beginGlow();
                drawGroundGlow(0.0f, z, 5.0f, 7.0f, { 1.0f, 0.90f, 0.70f, 0.30f }, 0.05f);
                endGlow();
            }
        }

        glEnable(GL_LIGHTING);
    }

}

bool Renderer::initialize()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    // сглаживание (нужен GLFW_SAMPLES в main.cpp), точные блики, чёткая перспектива
    glEnable(GL_MULTISAMPLE);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    if (kGfxTextures) {
        gTexAsphalt = makeNoiseTexture(128, 0.80f, 0.20f, 0.10f, 7u);
        gTexGrass = makeNoiseTexture(128, 0.80f, 0.28f, 0.16f, 21u);
    }

    const GLfloat ambient[] = { 0.28f, 0.30f, 0.34f, 1.0f };
    const GLfloat diffuse[] = { 0.94f, 0.90f, 0.78f, 1.0f };
    const GLfloat specular[] = { 0.25f, 0.25f, 0.22f, 1.0f };

    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    glEnable(GL_FOG);

    const GLfloat fogColor[] = { 0.055f, 0.080f, 0.105f, 1.0f };

    glFogfv(GL_FOG_COLOR, fogColor);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 85.0f);
    glFogf(GL_FOG_END, 205.0f);

    glClearColor(0.055f, 0.080f, 0.105f, 1.0f);

    return true;
}

void Renderer::resize(int width, int height)
{
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    glViewport(0, 0, width_, height_);
}

void Renderer::render(const Game& game)
{
    const float night = (0.5f + 0.5f * std::sin(game.worldTime() * 0.055f)) * 0.78f;

    SkyColors sky = skyColors(night);

    // дождь берём из Game: та же формула, что и для управления;
    // в туннеле уже 0
    gTrack = game.track();

    gRain = game.rain();

    // на снежной трассе тот же уровень осадков рисуется снегом, а не дождём
    gSnow = 0.0f;

    if (gTrack == 2) {
        gSnow = gRain;
        gRain = 0.0f;
    }

    // грозовое небо: серое, темнее ночью
    const float nn = std::clamp(night / 0.78f, 0.0f, 1.0f);
    const Color stormTop = mixColor({ 0.30f, 0.33f, 0.38f, 1.0f }, { 0.02f, 0.03f, 0.05f, 1.0f }, nn);
    const Color stormHorizon = mixColor({ 0.50f, 0.53f, 0.57f, 1.0f }, { 0.06f, 0.08f, 0.11f, 1.0f }, nn);
    sky.top = mixColor(sky.top, stormTop, gRain * 0.75f);
    sky.horizon = mixColor(sky.horizon, stormHorizon, gRain * 0.75f);

    // небо на разных трассах: жаркое в пустыне, серо-белое над снегом
    if (gTrack == 1) {
        sky.top = mixColor(sky.top, { 0.28f, 0.46f, 0.74f, 1.0f }, 0.45f * (1.0f - nn));
        sky.horizon = mixColor(sky.horizon, { 0.96f, 0.80f, 0.58f, 1.0f }, 0.55f * (1.0f - nn));
    }
    else if (gTrack == 2) {
        sky.top = mixColor(sky.top, stormTop, 0.35f + 0.50f * gSnow);
        sky.horizon = mixColor(sky.horizon, stormHorizon, 0.50f + 0.40f * gSnow);
    }

    gNightLevel = std::clamp((night - 0.30f) / 0.35f, 0.0f, 1.0f);

    // отражение неба на кузове: чуть светлее горизонта
    gReflSky = mixColor(sky.horizon, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.25f);

    updateEffects(game);


    gDriverTime = game.worldTime();
    gDriverWind = std::clamp(game.speed() / std::max(game.maxSpeed(), 1.0f), 0.0f, 1.0f);

    // водитель: если впереди машина - обе руки на руль, проехали - рука снова на окне
    {
        static float prevT = 0.0f;
        static float hold = 0.0f;

        const float nowT = game.worldTime();
        const float dtD = std::clamp(nowT - prevT, 0.0f, 0.1f);
        prevT = nowT;

        bool carAhead = false;

        for (const auto& v : game.traffic()) {
            const float ahead = game.playerZ() - v.z;                 // > 0: машина впереди
            const float side = std::fabs(v.x - game.playerX());

            if (ahead > -3.0f && ahead < 40.0f && side < 2.2f) {
                carAhead = true;
                break;
            }
        }

        // держим руки на руле ещё 0.7 с после того, как машина осталась позади
        hold = carAhead ? 0.7f : std::max(0.0f, hold - dtD);

        const float target = hold > 0.0f ? 1.0f : 0.0f;

        gDriverAlert += (target - gDriverAlert) * std::min(1.0f, dtD * 9.0f);
    }

    // в туннеле туман и фон тёмные, чтобы дальний конец не светился днём
    const Color tunnelFog = { 0.02f, 0.02f, 0.04f, 1.0f };
    const Color fogC = mixColor(sky.horizon, tunnelFog, gTunnelInside);

    glClearColor(fogC.r, fogC.g, fogC.b, 1.0f);

    // туман того же цвета, что и горизонт: земля плавно растворяется вдали
    const GLfloat fogColor[] = { fogC.r, fogC.g, fogC.b, 1.0f };
    glFogfv(GL_FOG_COLOR, fogColor);

    // в дождь видимость хуже
    const float vis = std::max(gRain, gSnow * 0.8f);

    glFogf(GL_FOG_START, 85.0f - 45.0f * vis);
    glFogf(GL_FOG_END, 205.0f - 90.0f * vis);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (game.gameMode() == GameMode::TwoPlayerBattle &&
        (game.state() == GameState::Running || game.state() == GameState::Paused || game.state() == GameState::GameOver))
    {
        const int fullWidth = width_;
        const int leftWidth = fullWidth / 2;
        glEnable(GL_SCISSOR_TEST);
        for (int player = 0; player < 2; ++player)
        {
            const int x = player == 0 ? 0 : leftWidth;
            const int viewportWidth = player == 0 ? leftWidth : fullWidth - leftWidth;
            glViewport(x, 0, viewportWidth, height_);
            glScissor(x, 0, viewportWidth, height_);
            glClear(GL_DEPTH_BUFFER_BIT);
            width_ = viewportWidth;
            gBattleCamera = player;
            drawSky(width_, height_, game.worldTime(), night, sky);
            renderWorld(game);
        }
        width_ = fullWidth;
        gBattleCamera = 0;
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width_, height_);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width_, height_, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glDisable(GL_BLEND);
        glBegin(GL_QUADS);
        glColor3f(0.03f, 0.07f, 0.11f);
        glVertex2f(leftWidth - 2.0f, 0.0f);
        glVertex2f(leftWidth + 2.0f, 0.0f);
        glVertex2f(leftWidth + 2.0f, static_cast<float>(height_));
        glVertex2f(leftWidth - 2.0f, static_cast<float>(height_));
        glEnd();
        drawText("PLAYER 1  WASD + SHIFT", 18.0f, 20.0f, 0.24f, {0.35f, 0.9f, 1.0f, 1.0f});
        drawText("PLAYER 2  ARROWS + RCTRL", leftWidth + 18.0f, 20.0f, 0.24f, {1.0f, 0.65f, 0.3f, 1.0f});
        drawText("SPEED " + std::to_string(static_cast<int>(game.speedKmh())) + " KMH  NITRO " + std::to_string(static_cast<int>(game.boost())) + "%", 18.0f, 52.0f, 0.19f, {1,1,1,1});
        drawText("SPEED " + std::to_string(static_cast<int>(game.player2SpeedKmh())) + " KMH  NITRO " + std::to_string(static_cast<int>(game.player2Boost())) + "%", leftWidth + 18.0f, 52.0f, 0.19f, {1,1,1,1});
        drawText("HP " + std::to_string(static_cast<int>(game.health())) + "  DIST " + std::to_string(static_cast<int>(game.distance())) + " M", 18.0f, 78.0f, 0.19f, {1,1,1,1});
        drawText("HP " + std::to_string(static_cast<int>(game.player2Health())) + "  DIST " + std::to_string(static_cast<int>(game.player2Distance())) + " M", leftWidth + 18.0f, 78.0f, 0.19f, {1,1,1,1});
        return;
    }

    drawSky(width_, height_, game.worldTime(), night, sky);

    renderWorld(game);
    renderHud(game);
}

void Renderer::renderWorld(const Game& game)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    setPerspective(
        60.0f + gFovExtra,
        static_cast<float>(width_) / static_cast<float>(height_),
        0.15f,
        260.0f
    );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // в гараже камера кружит вокруг машины, а сама она сдвинута вправо от панели
    const bool showroom = game.state() == GameState::Garage;

    static const auto kShowStart = std::chrono::steady_clock::now();
    const float showTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - kShowStart).count();

    const float cameraLean = (gBattleCamera == 1 ? game.player2Yaw() : game.playerYaw()) * 0.035f;

    // тряска камеры: лёгкая при бусте, сильная при ударе
    const float tt = game.worldTime();
    const float shakeAmp = game.shakeScale() * (0.035f * gBoostBlend + 0.35f * gShake);
    const float shakeX = std::sin(tt * 47.0f) * shakeAmp;
    const float shakeY = std::sin(tt * 61.0f + 1.3f) * shakeAmp * 0.7f;

    if (showroom) {
        const float ang = showTime * 0.45f;
        const float px = game.playerX();
        const float pz = game.playerZ();

        // сдвиг в системе камеры: машина уходит вправо и чуть вверх
        glTranslatef(3.3f, 0.75f, 0.0f);

        lookAt(
            { px + std::sin(ang) * 6.8f, 1.9f, pz + std::cos(ang) * 6.8f },
            { px, 0.55f, pz },
            { 0.0f, 1.0f, 0.0f }
        );
    }
    else {
        lookAt(
            { (gBattleCamera == 1 ? game.player2X() : game.playerX()) * 0.20f - cameraLean + shakeX, 6.3f + shakeY, game.playerZ() + 10.6f },
            { (gBattleCamera == 1 ? game.player2X() : game.playerX()) * 0.16f, 0.70f, game.playerZ() - 24.0f },
            { 0.0f, 1.0f, 0.0f }
        );
    }

    const GLfloat lightPosition[] = { -8.0f, 18.0f, 6.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

    // свет меняется со временем суток: тёплое солнце, холодная луна,
    // серое небо в дождь и тёплые лампы в туннеле
    if (kGfxDynamicLight) {
        const float nl = gNightLevel;

        Color amb = mixColor({ 0.28f, 0.30f, 0.34f, 1.0f }, { 0.09f, 0.11f, 0.20f, 1.0f }, nl);
        Color dif = mixColor({ 0.94f, 0.90f, 0.78f, 1.0f }, { 0.20f, 0.26f, 0.44f, 1.0f }, nl);
        Color spc = mixColor({ 0.90f, 0.86f, 0.76f, 1.0f }, { 0.22f, 0.28f, 0.50f, 1.0f }, nl);

        dif = mixColor(dif, { 0.52f, 0.56f, 0.62f, 1.0f }, gRain * 0.5f * (1.0f - nl));
        spc = mixColor(spc, { 0.20f, 0.22f, 0.26f, 1.0f }, gRain * 0.7f);

        amb = mixColor(amb, { 0.34f, 0.32f, 0.30f, 1.0f }, gTunnelInside);
        dif = mixColor(dif, { 0.55f, 0.50f, 0.42f, 1.0f }, gTunnelInside);
        spc = mixColor(spc, { 0.50f, 0.46f, 0.40f, 1.0f }, gTunnelInside);

        const GLfloat a4[] = { amb.r, amb.g, amb.b, 1.0f };
        const GLfloat d4[] = { dif.r, dif.g, dif.b, 1.0f };
        const GLfloat s4[] = { spc.r, spc.g, spc.b, 1.0f };

        glLightfv(GL_LIGHT0, GL_AMBIENT, a4);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, d4);
        glLightfv(GL_LIGHT0, GL_SPECULAR, s4);

        const float g = 0.20f * (1.0f - 0.65f * nl);
        const GLfloat global[] = { g, g, g * 1.1f, 1.0f };

        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global);
    }

    const bool secondView = gBattleCamera == 1 && game.gameMode() == GameMode::TwoPlayerBattle;
    const float viewDistance = secondView ? game.player2Distance() : game.distance();
    drawRoad(viewDistance, game.worldTime());
    drawEnvironment(viewDistance, game.worldTime());
    if (!secondView) drawTunnel(game.playerZ());
    if (secondView) {
        glPushMatrix();
        glTranslatef(0.0f, 0.0f, game.player2Distance() - game.distance());
    }

    // витрина гаража: только выбранная курсором машина, цвет берётся с курсора покраски
    if (showroom) {
        const CarLook look = carLook(game.carCursor());

        drawCarModel(
            game.playerX(),
            game.playerZ(),
            look.w,
            look.l,
            look.h,
            Game::paintColor(game.paintCursor()),
            look.type,
            0.0f,
            true,
            std::fmod(showTime * 2.0f, 2.0f * kPi)
        );

        return;
    }

    if (kGfxSkidMarks) {
        drawSkidMarks(game.playerZ());
    }

    for (const auto& coin : game.pickups()) {
        drawCoin(coin.x, coin.z, coin.phase);
    }

    for (const auto& power : game.powerUps()) {
        drawPowerUp(power.x, power.z, power.phase, power.type);
    }

    for (const auto& slick : game.oilSlicks()) {
        drawOilSlick(slick.x, slick.z, game.worldTime());
    }

    for (const auto& block : game.roadBlocks()) {
        drawRoadBlock(block.x, block.z, game.worldTime());
    }

    for (const auto& vehicle : game.traffic()) {
        const float localZ = vehicle.z + (secondView ? game.player2Distance() - game.distance() : 0.0f);
        if (localZ < -260.0f || localZ > 45.0f) continue;
        drawCarModel(
            vehicle.x,
            vehicle.z,
            vehicle.width,
            vehicle.length,
            vehicle.height,
            vehicle.color,
            vehicle.type,
            0.0f,
            false,
            // колёса крутятся по мере движения машины вперёд (relativeSpeed < 0 = едет вперёд)
            std::fmod(vehicle.relativeSpeed * game.worldTime() / 0.31f, 2.0f * kPi)
        );
    }

    // поворотник включается, пока машина перестраивается в другую полосу
    if (game.gameMode() == GameMode::TwoPlayerBattle && game.state() != GameState::Menu)
    {
        const CarLook secondLook = carLook(game.selectedCar());
        if (game.player2Z() + (secondView ? game.player2Distance() - game.distance() : 0.0f) > -260.0f &&
            game.player2Z() + (secondView ? game.player2Distance() - game.distance() : 0.0f) < 45.0f)
        drawCarModel(game.player2X(), game.player2Z(), secondLook.w, secondLook.l,
            secondLook.h, {0.95f, 0.23f, 0.12f, 1.0f}, secondLook.type,
            game.player2Yaw(), true, std::fmod(game.player2Distance() / 0.31f, 2.0f * kPi));
    }
    const float laneDx = Game::laneCenter(game.targetLane()) - game.playerX();
    const int signalDir = laneDx > 0.25f ? 1 : (laneDx < -0.25f ? -1 : 0);

    const CarLook playerLook = carLook(game.state() == GameState::Menu ? 0 : game.selectedCar());

    drawCarModel(
        game.playerX(),
        game.playerZ(),
        playerLook.w,
        playerLook.l,
        playerLook.h,
        Game::paintColor(game.selectedPaint()),
        playerLook.type,
        game.playerYaw(),
        true,
        // колёса крутятся по пройденному пути (радиус колеса 0.31 м)
        std::fmod(game.distance() / 0.31f, 2.0f * kPi),
        signalDir,
        game.worldTime()
    );

    // щит: голубой пузырь вокруг машины, в последние секунды мигает
    if (game.shieldTimer() > 0.0f) {
        const float blink = game.shieldTimer() < 3.0f ? 0.55f + 0.45f * std::sin(tt * 16.0f) : 1.0f;
        const float pulseS = 0.85f + 0.15f * std::sin(tt * 5.0f);

        beginGlow();
        drawGlow(game.playerX(), 0.80f, game.playerZ() + 1.0f, 2.5f * pulseS, { 0.25f, 0.85f, 1.0f, 0.38f * blink });
        drawGroundGlow(game.playerX(), game.playerZ(), 1.7f, 2.8f, { 0.25f, 0.85f, 1.0f, 0.40f * blink }, 0.08f);
        endGlow();
    }

    // магнит: оранжевое кольцо на дороге вокруг машины
    if (game.magnetTimer() > 0.0f) {
        const float blink = game.magnetTimer() < 3.0f ? 0.55f + 0.45f * std::sin(tt * 16.0f) : 1.0f;
        const float pulseM = 1.0f + 0.12f * std::sin(tt * 6.0f);

        beginGlow();
        drawGroundGlow(game.playerX(), game.playerZ(), 3.6f * pulseM, 4.2f * pulseM, { 1.0f, 0.50f, 0.12f, 0.30f * blink }, 0.09f);
        endGlow();
    }

    if (game.boosting()) {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        setColor({ 0.15f, 0.68f, 1.0f, 0.78f });

        glBegin(GL_TRIANGLES);

        glVertex3f(game.playerX() - 0.34f, 0.34f, game.playerZ() + 1.95f);
        glVertex3f(game.playerX() - 0.10f, 0.34f, game.playerZ() + 1.95f);
        glVertex3f(game.playerX() - 0.22f, 0.34f, game.playerZ() + 3.20f);

        glVertex3f(game.playerX() + 0.10f, 0.34f, game.playerZ() + 1.95f);
        glVertex3f(game.playerX() + 0.34f, 0.34f, game.playerZ() + 1.95f);
        glVertex3f(game.playerX() + 0.22f, 0.34f, game.playerZ() + 3.20f);

        glEnd();

        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    drawParticles();

    // свет полицейской мигалки на дороге
    float policeLight = 0.0f;

    if (game.chaseState() == ChaseState::Warning) {
        policeLight = 0.45f;
    }
    else if (game.chaseState() == ChaseState::Chasing) {
        policeLight = std::clamp(1.2f - game.policeGap() / 90.0f, 0.3f, 1.0f);
    }

    drawPoliceGlow(game.playerX(), game.playerZ(), policeLight, game.worldTime());
    if (secondView) glPopMatrix();

}

void Renderer::renderHud(const Game& game)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(0.0, static_cast<double>(width_), static_cast<double>(height_), 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float centerX = w * 0.5f;
    const float t = game.worldTime();
    const float hp = game.health();

    const Color panelBg = { 0.015f, 0.020f, 0.027f, 0.82f };
    const Color labelColor = { 0.76f, 0.84f, 0.90f, 1.0f };

    // Dedicated home screen. Do not draw gameplay HUD or paused/game-over overlays.
    if (game.state() == GameState::Menu)
    {
        const float scale = std::min(w / 1600.0f, h / 900.0f);
        const float px = 62.0f * scale;
        const float py = 165.0f * scale;
        const float pw = 538.0f * scale;
        const float ph = 594.0f * scale;
        const Color cyan = { 0.12f, 0.85f, 1.0f, 1.0f };
        const Color white = { 0.96f, 0.98f, 1.0f, 1.0f };
        const Color muted = { 0.65f, 0.76f, 0.85f, 1.0f };
        const Color red = { 1.0f, 0.29f, 0.38f, 1.0f };
        const auto sx = [&](float n) { return n * scale; };
        // Road stays visible as an animated hero background, without gameplay widgets.
        drawOverlayRect(0.0f, 0.0f, w, h, { 0.005f, 0.013f, 0.032f, 0.62f });
        drawOverlayRect(0.0f, 0.0f, sx(660), h, { 0.007f, 0.018f, 0.037f, 0.80f });
        drawOverlayRect(0.0f, 0.0f, w, sx(8), cyan);
        drawText("HIGHWAY", sx(75), sx(34), 1.15f * scale, white);
        drawText("ENDLESS", sx(75), sx(101), 1.15f * scale, cyan);
        drawText("ARCADE RACING  /  LOCAL BATTLE", sx(830), sx(54), 0.32f * scale, white);

        drawPanel(px, py, pw, ph, { 0.009f, 0.022f, 0.045f, 0.96f },
                  { 0.12f, 0.38f, 0.56f, 0.94f });
        drawText("CHOOSE YOUR RIDE", px + sx(25), py + sx(26), 0.55f * scale, white);
        drawText("START A RUN OR CHALLENGE A FRIEND", px + sx(25), py + sx(74), 0.24f * scale, muted);

        const auto button = [&](float y, float height, Color bg, Color edge,
                                const char* title, const char* subtitle, float titleSize) {
            drawPanel(px + sx(24), py + sx(y), sx(490), sx(height), bg, edge);
            drawOverlayRect(px + sx(24), py + sx(y), sx(6), sx(height), edge);
            drawText(title, px + sx(46), py + sx(y + 10), titleSize * scale, white);
            if (subtitle[0] != '\0')
                drawText(subtitle, px + sx(46), py + sx(y + height - 23), 0.22f * scale, muted);
        };
        button(105, 75, { 0.016f, 0.11f, 0.19f, 0.98f }, cyan,
               "SINGLE PLAYER", "ENDLESS RUN  /  TIME WARP  /  HIGH SCORES", 0.51f);
        button(194, 75, { 0.20f, 0.025f, 0.055f, 0.98f }, red,
               "2 PLAYER BATTLE", "LOCAL SPLIT SCREEN  /  IN DEVELOPMENT", 0.49f);
        drawOverlayRect(px + sx(24), py + sx(283), sx(490), sx(2), { 0.19f, 0.36f, 0.49f, 0.8f });
        button(296, 59, { 0.025f, 0.065f, 0.11f, 0.96f },
               { 0.25f, 0.66f, 0.89f, 1.0f }, "GARAGE", "", 0.48f);
        button(366, 59, { 0.025f, 0.065f, 0.11f, 0.96f },
               { 0.25f, 0.66f, 0.89f, 1.0f }, "ACHIEVEMENTS", "", 0.48f);
        button(436, 59, { 0.025f, 0.065f, 0.11f, 0.96f },
               { 0.25f, 0.66f, 0.89f, 1.0f }, "SETTINGS", "", 0.48f);
        button(506, 59, { 0.065f, 0.035f, 0.055f, 0.96f },
               { 0.75f, 0.30f, 0.34f, 1.0f }, "EXIT GAME", "", 0.48f);

        // Separate player profile, so garage currency and progression remain visible.
        const float infoX = std::max(sx(745), w - sx(560));
        const float infoY = h - sx(232);
        drawPanel(infoX, infoY, sx(470), sx(175),
                  { 0.007f, 0.019f, 0.037f, 0.90f },
                  { 0.12f, 0.78f, 0.94f, 0.85f });
        drawText("DRIVER PROFILE", infoX + sx(22), infoY + sx(15), 0.38f * scale, white);
        drawText("COINS  " + std::to_string(game.bank()), infoX + sx(22), infoY + sx(66),
                 0.43f * scale, { 1.0f, 0.83f, 0.26f, 1.0f });
        drawText("LEVEL  " + std::to_string(game.level()), infoX + sx(255), infoY + sx(66),
                 0.43f * scale, cyan);
        drawText("HIGH SCORE  " + std::to_string(game.highScore()), infoX + sx(22),
                 infoY + sx(121), 0.34f * scale, muted);
        if (game.multiplayerPending())
            drawText("2 PLAYER BATTLE  -  COMING NEXT", px + sx(24), py + sx(571),
                     0.23f * scale, red);
        drawText("MOUSE: CLICK BUTTONS    1: SOLO    2: BATTLE    G: GARAGE    O: SETTINGS",
                 sx(74), h - sx(26), 0.21f * scale, muted);
        return;
    }


    // импульс при росте комбо
    if (game.combo() > gPrevCombo) {
        gComboPulseStart = t;

        if (game.combo() % 5 == 0) {
            gMilestoneStart = t;
            gMilestoneCombo = game.combo();
        }
    }
    gPrevCombo = game.combo();

    float since = t - gComboPulseStart;
    if (since < 0.0f) {
        since = 99.0f;
    }
    const float pulse = std::exp(-since * 7.0f);

    // в гараже обычный HUD не рисуем: справа виден вращающийся автомобиль
    if (game.state() != GameState::Garage) {

        // линии скорости, дождь, молния и красная вспышка при ударе
        // (gRain уже равен 0 в туннеле, отдельной обработки не нужно)
        // затемнение по краям на большой скорости (эффект «туннельного зрения»)
        const float speedVignette = game.speedFxScale() * (
            0.10f +
            std::clamp((game.speedKmh() - 200.0f) / 250.0f, 0.0f, 1.0f) * 0.35f +
            gBoostBlend * 0.20f);

        if (speedVignette > 0.01f) {
            drawEdgeGlow(w, h, { 0.0f, 0.0f, 0.0f, speedVignette });
        }

        drawSpeedLines(w, h, t, game.speedKmh(), game.speedFxScale());

        drawRain(w, h, t, game.speedKmh());

        drawSnow(w, h, t, game.speedKmh());

        const float flash = lightningFlash(t);
        if (flash > 0.01f) {
            drawOverlayRect(0.0f, 0.0f, w, h, { 0.85f, 0.90f, 1.0f, 0.28f * flash });
        }

        if (gHitFlash > 0.01f) {
            drawOverlayRect(0.0f, 0.0f, w, h, { 1.0f, 0.10f, 0.05f, 0.30f * gHitFlash });
        }

        // красная виньетка при низком здоровье
        if (hp <= 35.0f && game.state() == GameState::Running) {
            const float severity = 0.5f + 0.5f * (35.0f - hp) / 35.0f;
            const float a = (0.22f + 0.16f * std::sin(t * 7.0f)) * severity;

            drawEdgeGlow(w, h, { 0.95f, 0.05f, 0.03f, std::max(0.0f, a) });
        }

        // бонус за пройденный путь
        if (game.milestoneTimer() > 0.0f && game.state() == GameState::Running) {
            const float fade = std::min(1.0f, game.milestoneTimer() / 2.5f * 2.0f);

            drawTextCentered(
                std::to_string(game.milestoneMeters() / 1000) + " KM",
                centerX, h * 0.42f, 0.70f, { 0.42f, 0.85f, 1.0f, fade }
            );
            drawTextCentered("BONUS 8 COINS", centerX, h * 0.42f + 52.0f, 0.32f, { 1.0f, 0.88f, 0.28f, fade });
        }

        // всплывающая надпись при комбо 5, 10 и 15
        {
            const float ms = t - gMilestoneStart;

            if (ms >= 0.0f && ms < 1.6f && game.state() == GameState::Running) {
                const float fade = 1.0f - ms / 1.6f;
                const float pop = std::exp(-ms * 8.0f);
                const float y = h * 0.30f - 36.0f * ms;

                const char* label =
                    gMilestoneCombo >= 15 ? "UNSTOPPABLE" :
                    gMilestoneCombo >= 10 ? "GREAT" : "NICE";

                drawTextCentered(
                    "COMBO X" + std::to_string(gMilestoneCombo),
                    centerX, y, 0.62f + 0.22f * pop,
                    { 1.0f, 0.62f + 0.25f * pop, 0.18f + 0.30f * pop, fade }
                );

                drawTextCentered(label, centerX, y + 54.0f, 0.34f, { 1.0f, 0.90f, 0.55f, fade });
            }
        }

        // ---- Счёт ----
        drawPanel(20.0f, 18.0f, 290.0f, 114.0f, panelBg, { 1.0f, 0.78f, 0.20f, 0.85f });

        drawText("SCORE", 36.0f, 30.0f, 0.38f, labelColor);
        drawText(std::to_string(game.score()), 36.0f, 57.0f, 0.62f, { 1.0f, 0.83f, 0.22f, 1.0f });
        drawText("COINS", 36.0f, 96.0f, 0.30f, { 0.95f, 0.84f, 0.35f, 1.0f });
        drawText(std::to_string(game.coins()), 142.0f, 93.0f, 0.36f, { 1.0f, 0.92f, 0.30f, 1.0f });

        // ---- Задание на забег ----
        {
            const bool done = game.missionDone();
            const int target = game.missionTarget();
            const int prog = std::min(game.missionProgress(), target);

            drawText("MISSION", 24.0f, 144.0f, 0.22f, { 0.76f, 0.84f, 0.90f, 0.95f });
            drawText(game.missionName(), 24.0f, 164.0f, 0.24f, done ? Color{ 0.35f, 1.0f, 0.50f, 1.0f } : Color{ 1.0f, 0.88f, 0.28f, 1.0f });

            if (done) {
                drawText("DONE", 24.0f, 186.0f, 0.22f, { 0.35f, 1.0f, 0.50f, 1.0f });
            }
            else {
                drawText(std::to_string(prog) + " OF " + std::to_string(target), 24.0f, 186.0f, 0.22f, { 0.80f, 0.90f, 0.95f, 1.0f });
            }

            if (game.missionPopupTimer() > 0.0f && game.state() == GameState::Running) {
                const float fade = std::min(1.0f, game.missionPopupTimer());

                drawTextCentered("MISSION COMPLETE", centerX, h * 0.56f, 0.52f, { 0.35f, 1.0f, 0.50f, fade });
                drawTextCentered("BONUS 15 COINS", centerX, h * 0.56f + 48.0f, 0.30f, { 1.0f, 0.88f, 0.28f, fade });
            }
        }

        // ---- Рекорд и комбо ----
        drawPanel(w - 312.0f, 18.0f, 292.0f, 114.0f, panelBg, { 0.42f, 0.85f, 1.0f, 0.85f });

        drawText("HIGH", w - 292.0f, 30.0f, 0.38f, labelColor);
        drawText(std::to_string(game.highScore()), w - 292.0f, 57.0f, 0.62f, { 0.42f, 0.85f, 1.0f, 1.0f });

        if (game.combo() > 1) {
            const float cs = 0.36f + 0.14f * pulse;

            drawText("COMBO", w - 292.0f, 96.0f, 0.30f, { 1.0f, 0.55f, 0.20f, 1.0f });
            drawText(
                "X" + std::to_string(game.combo()),
                w - 176.0f,
                93.0f - 3.0f * pulse,
                cs,
                { 1.0f, 0.40f + 0.45f * pulse, 0.16f + 0.40f * pulse, 1.0f }
            );
        }

        // ---- Скорость и буст ----
        const bool boosting = game.boosting();
        const float boostGlow = boosting ? 0.5f + 0.5f * std::sin(t * 14.0f) : 0.0f;

        const Color speedAccent =
            boosting
            ? Color{ 0.22f, 0.78f, 1.0f, 0.55f + 0.45f * boostGlow }
        : Color{ 0.64f, 1.0f, 0.45f, 0.70f };

        drawPanel(centerX - 150.0f, 18.0f, 300.0f, 114.0f, { 0.015f, 0.020f, 0.027f, 0.74f }, speedAccent);

        drawText("SPEED", centerX - 106.0f, 30.0f, 0.38f, labelColor);

        drawText(
            std::to_string(static_cast<int>(game.speedKmh())),
            centerX - 58.0f,
            57.0f,
            0.62f,
            boosting
            ? Color{ 0.22f, 0.78f, 1.0f, 1.0f }
            : Color{ 0.64f, 1.0f, 0.45f, 1.0f }
        );

        drawText("BOOST", centerX - 128.0f, 96.0f, 0.28f, { 0.58f, 0.84f, 1.0f, 1.0f });

        drawSegBar(
            centerX - 28.0f,
            96.0f,
            150.0f,
            18.0f,
            game.boost() / 100.0f,
            12,
            boosting ? Color{ 0.45f, 0.92f, 1.0f, 1.0f } : Color{ 0.15f, 0.62f, 1.0f, 1.0f },
            { 0.08f, 0.11f, 0.14f, 0.95f }
        );

        // ---- TIME WARP HUD ----
        if (game.state() == GameState::Running) {
            const bool warpActive = game.timeWarpActive();
            const float warpTime = game.timeWarpRemaining();
            const float cooldown = game.timeWarpCooldownRemaining();

            const float warpX = static_cast<float>(w) - 265.0f;
            const float warpY = static_cast<float>(h) - 95.0f;

            const Color cyan = { 0.12f, 0.85f, 1.0f, 1.0f };
            const Color green = { 0.30f, 1.0f, 0.55f, 1.0f };
            const Color gray = { 0.55f, 0.65f, 0.72f, 1.0f };

            Color accent = gray;
            std::string status;
            float progress = 0.0f;

            if (warpActive) {
                accent = cyan;
                status = "TIME WARP ACTIVE";
                progress = warpTime / game.timeWarpDuration();

                drawEdgeGlow(w, h,
                    { 0.0f, 0.65f, 1.0f, 0.12f });
            }
            else if (cooldown > 0.0f) {
                accent = gray;
                status = "RECHARGING " +
                    std::to_string(static_cast<int>(
                        std::ceil(cooldown))) + "S";

                progress = 1.0f -
                    cooldown / (game.timeWarpDuration() +
                        game.timeWarpRechargeDuration());
            }
            else {
                accent = green;
                status = "TIME WARP READY";
                progress = 1.0f;
            }

            progress = std::clamp(progress, 0.0f, 1.0f);

            drawPanel(
                warpX, warpY, 250.0f, 70.0f,
                { 0.015f, 0.025f, 0.045f, 0.82f },
                accent
            );

            drawText(
                status,
                warpX + 20.0f,
                warpY + 12.0f,
                0.30f,
                accent
            );

            drawText(
                "Q - TIME WARP",
                warpX + 20.0f,
                warpY + 39.0f,
                0.20f,
                { 0.70f, 0.85f, 0.95f, 1.0f }
            );


            drawSegBar(
                warpX + 15.0f,
                warpY + 49.0f,
                220.0f,
                8.0f,
                progress,
                16,
                accent,
                { 0.08f, 0.12f, 0.18f, 0.95f }
            );

        }

        // предупреждение о дорожных работах впереди

        bool blockAhead = false;

        for (const auto& block : game.roadBlocks()) {
            if (!block.hit && block.z < game.playerZ() - 6.0f && block.z > game.playerZ() - 95.0f) {
                blockAhead = true;
                break;
            }
        }

        if (blockAhead) {
            drawTextCentered("ROADWORKS AHEAD", centerX, 142.0f, 0.28f, { 1.0f, 0.62f, 0.12f, 0.65f + 0.35f * std::sin(t * 8.0f) });
        }
        else if (game.oilActive()) {
            drawTextCentered("OIL SLICK", centerX, 142.0f, 0.28f, { 0.95f, 0.45f, 0.85f, 0.65f + 0.35f * std::sin(t * 10.0f) });
        }
        else if (gTunnelInside > 0.5f) {
            drawTextCentered("TUNNEL", centerX, 142.0f, 0.28f, { 1.0f, 0.35f, 0.80f, 0.65f + 0.35f * std::sin(t * 4.0f) });
        }
        else if (gSnow > 0.3f) {
            drawTextCentered("ICY ROAD", centerX, 142.0f, 0.28f, { 0.80f, 0.90f, 1.0f, 0.65f + 0.35f * std::sin(t * 4.0f) });
        }
        else if (gRain > 0.3f) {
            drawTextCentered("WET ROAD", centerX, 142.0f, 0.28f, { 0.58f, 0.84f, 1.0f, 0.65f + 0.35f * std::sin(t * 4.0f) });
        }

        // ---- Полиция ----
        const ChaseState chase = game.chaseState();

        if (chase != ChaseState::Idle) {
            const bool phase = std::fmod(t * 3.0f, 1.0f) < 0.5f;
            const Color siren =
                phase
                ? Color{ 1.0f, 0.15f, 0.12f, 1.0f }
            : Color{ 0.20f, 0.45f, 1.0f, 1.0f };

            drawTextCentered(
                chase == ChaseState::Warning ? "POLICE BEHIND" : "POLICE CHASE",
                centerX,
                170.0f,
                0.42f,
                siren
            );

            if (chase == ChaseState::Chasing) {
                const float danger = std::clamp(1.0f - game.policeGap() / 140.0f, 0.0f, 1.0f);

                drawSegBar(
                    centerX - 110.0f,
                    205.0f,
                    220.0f,
                    16.0f,
                    danger,
                    14,
                    danger > 0.65f ? Color{ 1.0f, 0.20f, 0.14f, 1.0f } : Color{ 1.0f, 0.72f, 0.18f, 1.0f },
                    { 0.08f, 0.10f, 0.12f, 0.90f }
                );

                // красно-синяя вспышка по краям, когда полиция совсем близко
                if (game.policeGap() < 55.0f) {
                    const float a = 0.22f * (1.0f - game.policeGap() / 55.0f);

                    drawEdgeGlow(
                        w, h,
                        phase ? Color{ 1.0f, 0.10f, 0.08f, a } : Color{ 0.20f, 0.40f, 1.0f, a }
                    );
                }
            }

            drawRearMirror(w - 300.0f, h - 180.0f, 288.0f, 168.0f, height_, game.policeGap(), game.policeX() / (kRoadWidth * 0.5f), t);
        }
        else if (game.escapeTimer() > 0.0f) {
            drawTextCentered("ESCAPED 1500", centerX, 170.0f, 0.42f, { 0.35f, 1.0f, 0.50f, 1.0f });
        }

        // ---- Бонусы: щит и магнит ----
        {
            float by = 146.0f;

            auto timerRow = [&](const char* label, float timer, float maxTime, Color col) {
                if (timer <= 0.0f) {
                    return;
                }

                drawPanel(w - 312.0f, by, 292.0f, 42.0f, panelBg, { col.r, col.g, col.b, 0.85f });
                drawText(label, w - 296.0f, by + 9.0f, 0.24f, col);
                drawSegBar(w - 170.0f, by + 10.0f, 140.0f, 22.0f, timer / maxTime, 10, col, { 0.08f, 0.11f, 0.14f, 0.95f });

                by += 50.0f;
                };

            timerRow("SHIELD", game.shieldTimer(), 12.0f, { 0.25f, 0.85f, 1.0f, 1.0f });
            timerRow("MAGNET", game.magnetTimer(), 10.0f, { 1.0f, 0.50f, 0.12f, 1.0f });

            if (game.powerPopupTimer() > 0.0f && game.state() == GameState::Running) {
                const float fade = std::min(1.0f, game.powerPopupTimer());

                const char* label = "SHIELD ON";
                Color pc = { 0.25f, 0.85f, 1.0f, fade };

                if (game.powerPopupType() == PowerType::Magnet) {
                    label = "MAGNET ON";
                    pc = { 1.0f, 0.50f, 0.12f, fade };
                }
                else if (game.powerPopupType() == PowerType::Nitro) {
                    label = "NITRO FULL";
                    pc = { 0.35f, 1.0f, 0.45f, fade };
                }

                drawTextCentered(label, centerX, h * 0.66f, 0.50f, pc);
            }
        }

        // достижение получено
        if (game.achPopupTimer() > 0.0f && game.state() == GameState::Running) {
            const float fade = std::min(1.0f, game.achPopupTimer());
            const int aid = game.achPopupId();
            const float ay = h * 0.74f - 14.0f;

            drawPanel(centerX - 230.0f, ay, 460.0f, 92.0f, { 0.015f, 0.020f, 0.027f, 0.88f * fade }, { 1.0f, 0.78f, 0.20f, 0.90f * fade });
            drawTextCentered("ACHIEVEMENT UNLOCKED", centerX, ay + 12.0f, 0.22f, { 0.55f, 0.90f, 1.0f, fade });
            drawTextCentered(Game::achName(aid), centerX, ay + 34.0f, 0.38f, { 1.0f, 0.83f, 0.22f, fade });
            drawTextCentered("BONUS " + std::to_string(Game::achReward(aid)) + " COINS", centerX, ay + 68.0f, 0.22f, { 1.0f, 0.88f, 0.28f, fade });
        }

        // ---- Здоровье и подсказки ----
        drawPanel(12.0f, h - 76.0f, 470.0f, 68.0f, { 0.015f, 0.020f, 0.027f, 0.70f }, { 0.55f, 0.65f, 0.75f, 0.45f });

        drawText("HEALTH", 28.0f, h - 60.0f, 0.30f, { 0.82f, 0.86f, 0.90f, 1.0f });

        Color healthColor = { 0.18f, 0.85f, 0.36f, 1.0f };
        if (hp <= 30.0f) {
            healthColor = { 1.0f, 0.24f, 0.16f, 0.65f + 0.35f * std::sin(t * 9.0f) };
        }
        else if (hp <= 60.0f) {
            healthColor = { 1.0f, 0.78f, 0.18f, 1.0f };
        }

        drawSegBar(
            136.0f,
            h - 63.0f,
            210.0f,
            18.0f,
            hp / 100.0f,
            20,
            healthColor,
            { 0.08f, 0.10f, 0.12f, 0.90f }
        );

        drawText(
            "A D LANES   W SPEED   S BRAKE   SHIFT BOOST   P PAUSE",
            28.0f,
            h - 34.0f,
            0.25f,
            { 0.70f, 0.76f, 0.80f, 0.95f }
        );

    }   // конец обычного HUD (не в гараже)

    // ---- Пауза / конец игры ----
    if (game.state() == GameState::Paused || game.state() == GameState::GameOver) {
        drawOverlayRect(0.0f, 0.0f, w, h, { 0.0f, 0.0f, 0.0f, 0.56f });

        const bool paused = game.state() == GameState::Paused;

        drawPanel(
            centerX - 270.0f,
            h * 0.5f - 120.0f,
            540.0f,
            240.0f,
            { 0.015f, 0.022f, 0.030f, 0.94f },
            paused ? Color{ 1.0f, 0.82f, 0.22f, 0.9f } : Color{ 1.0f, 0.24f, 0.18f, 0.9f }
        );

        if (paused) {
            drawTextCentered("PAUSED", centerX, h * 0.5f - 72.0f, 1.0f, { 1.0f, 0.82f, 0.22f, 1.0f });
            drawTextCentered("P RESUME", centerX, h * 0.5f + 34.0f, 0.60f, { 0.80f, 0.90f, 0.95f, 1.0f });
            drawTextCentered("G GARAGE   H ACHIEVEMENTS   O SETTINGS", centerX, h * 0.5f + 84.0f, 0.30f, { 0.42f, 0.85f, 1.0f, 1.0f });
        }
        else {
            drawTextCentered(
                game.busted() ? "BUSTED" : "CRASH",
                centerX,
                h * 0.5f - 72.0f,
                1.0f,
                { 1.0f, 0.24f, 0.18f, 1.0f }
            );

            drawTextCentered(
                "SCORE " + std::to_string(game.score()),
                centerX,
                h * 0.5f - 18.0f,
                0.50f,
                { 1.0f, 0.83f, 0.22f, 1.0f }
            );

            if (game.score() > 0 && game.score() >= game.highScore()) {
                drawTextCentered("NEW RECORD", centerX, h * 0.5f + 10.0f, 0.30f, { 0.42f, 0.85f, 1.0f, 1.0f });
            }

            drawTextCentered("R RESTART", centerX, h * 0.5f + 40.0f, 0.60f, { 0.80f, 0.90f, 0.95f, 1.0f });

            if (game.lastRunXp() > 0) {
                std::string xpLine = "+" + std::to_string(game.lastRunXp()) + " XP";

                if (game.lastRunLevelUps() > 0) {
                    xpLine += "   LEVEL UP " + std::to_string(game.level());
                }

                drawTextCentered(xpLine, centerX, h * 0.5f + 84.0f, 0.26f, { 0.55f, 0.90f, 1.0f, 1.0f });
            }

            drawTextCentered("G GARAGE   H ACHIEVEMENTS   T TRACK   O SETTINGS", centerX, h * 0.5f + 104.0f, 0.18f, { 0.55f, 0.70f, 0.80f, 1.0f });
        }
    }

    // ---- Гараж ----
    if (game.state() == GameState::Garage && game.garageTab() == 0) {
        const float px = 20.0f, py = 95.0f;
        const float pw = std::min(590.0f, w * 0.44f);
        const float ph = h - py - 18.0f;
        const Color cyan = { 0.22f, 0.82f, 1.0f, 1.0f };
        const Color gold = { 1.0f, 0.80f, 0.22f, 1.0f };
        const Color green = { 0.28f, 1.0f, 0.55f, 1.0f };
        drawOverlayRect(0.0f, 0.0f, pw + 45.0f, h, { 0.0f, 0.0f, 0.0f, 0.78f });
        drawPanel(px, py, pw, ph, { 0.015f, 0.025f, 0.045f, 0.96f }, cyan);
        drawText("CAR SHOWROOM", px + 24, py + 14, 0.57f, cyan);
        drawText("COINS  " + std::to_string(game.bank()), px + 25, py + 49, 0.32f, gold);
        const float tabW = (pw - 42.0f) * 0.5f;
        drawPanel(px + 16, py + 72, tabW, 48, { 0.02f, 0.15f, 0.22f, 0.96f }, cyan);
        drawPanel(px + 26 + tabW, py + 72, tabW, 48, { 0.04f, 0.05f, 0.07f, 0.96f }, { 0.35f, 0.42f, 0.50f, 1.0f });
        drawTextCentered("CARS", px + 16 + tabW * 0.5f, py + 83, 0.36f, cyan);
        drawTextCentered("UPGRADES", px + 26 + tabW * 1.5f, py + 83, 0.32f, labelColor);
        const float rowY = py + 137.0f;
        const float rowH = std::min(67.0f, (h - rowY - 160.0f) / 6.0f);
        for (int i = 0; i < kCarCount; ++i) {
            const bool selected = game.carCursor() == i;
            const bool owned = game.carUnlocked(i);
            const bool equipped = game.selectedCar() == i;
            const float ry = rowY + rowH * i;
            drawPanel(px + 18, ry, pw - 36, rowH - 5,
                selected ? Color{0.04f,0.16f,0.23f,0.98f} : Color{0.025f,0.04f,0.065f,0.96f},
                selected ? cyan : Color{0.18f,0.28f,0.36f,0.8f});
            drawText(std::to_string(i + 1), px + 29, ry + 8, 0.33f, selected ? cyan : labelColor);
            drawText(Game::carName(i), px + 60, ry + 7, 0.32f, {0.95f,0.97f,1.0f,1.0f});
            drawText(equipped ? "EQUIPPED" : owned ? "OWNED" : "LOCKED",
                     px + 60, ry + 30, 0.19f, equipped ? green : labelColor);
            drawText(std::to_string(Game::carCost(i)), px + pw - 120, ry + 16, 0.27f, owned ? green : gold);
        }
        const int cur = game.carCursor();
        const bool owned = game.carUnlocked(cur);
        const bool equipped = game.selectedCar() == cur;
        const bool levelOk = game.level() >= Game::carLevelReq(cur);
        const bool canBuy = levelOk && game.bank() >= Game::carCost(cur);
        const float ay = h - 88.0f;
        drawPanel(px + 18, ay, 100, 58, {0.03f,0.08f,0.12f,1.0f}, cyan);
        drawPanel(px + pw - 118, ay, 100, 58, {0.03f,0.08f,0.12f,1.0f}, cyan);
        drawTextCentered("< PREV", px + 68, ay + 17, 0.29f, cyan);
        drawTextCentered("NEXT >", px + pw - 68, ay + 17, 0.29f, cyan);
        const Color actionColor = equipped ? green : owned ? cyan : canBuy ? gold : Color{0.55f,0.58f,0.63f,1.0f};
        drawPanel(px + 130, ay, pw - 260, 58, {0.035f,0.10f,0.13f,1.0f}, actionColor);
        const std::string action = equipped ? "EQUIPPED" : owned ? "EQUIP CAR" : !levelOk ?
            "LEVEL " + std::to_string(Game::carLevelReq(cur)) + " NEEDED" :
            canBuy ? "BUY CAR" : "NOT ENOUGH COINS";
        drawTextCentered(action, px + pw * 0.5f, ay + 17, 0.30f, actionColor);
        // Stats stay near the 3D car, leaving the model visible.
        const float sw = std::min(350.0f, w - pw - 85.0f);
        const float sx = w - sw - 22.0f, sy = h - 255.0f;
        drawPanel(sx, sy, sw, 155, {0.012f,0.025f,0.04f,0.93f}, gold);
        drawTextCentered(Game::carName(cur), sx + sw * 0.5f, sy + 10, 0.41f, gold);
        const float vals[3] = {
            std::clamp((Game::carSpeed(cur)-0.85f)/0.95f,0.0f,1.0f),
            std::clamp((Game::carGrip(cur)-0.75f)/0.50f,0.0f,1.0f),
            std::clamp((1.40f-Game::carArmor(cur))/0.75f,0.0f,1.0f)
        };
        const char* labels[3] = {"SPEED", "GRIP", "ARMOR"};
        for (int i=0;i<3;++i) {
            drawText(labels[i], sx + 15, sy + 52 + i*30, 0.22f, labelColor);
            drawSegBar(sx + 110, sy + 49 + i*30, sw - 130, 18, vals[i], 10, cyan, {0.07f,0.12f,0.16f,1.0f});
        }
        drawTextCentered("PRICE " + std::to_string(Game::carCost(cur)) + "  |  LEVEL " + std::to_string(Game::carLevelReq(cur)),
                         sx + sw * 0.5f, sy + 134, 0.18f, labelColor);
    }

    if (game.state() == GameState::Garage && game.garageTab() == 1) {
        const float pw = 760.0f;
        const float ph = 560.0f;
        const float px0 = 24.0f;
        const float py0 = h * 0.5f - ph * 0.5f;
        const float pcx = px0 + pw * 0.5f;

        // затемнение только слева, справа виден вращающийся автомобиль
        drawOverlayRect(0.0f, 0.0f, px0 + pw + 24.0f, h, { 0.0f, 0.0f, 0.0f, 0.86f });

        drawPanel(px0, py0, pw, ph, { 0.015f, 0.022f, 0.030f, 0.96f }, { 0.42f, 0.85f, 1.0f, 0.9f });

        drawPanel(36.0f, 167.0f, 274.0f, 48.0f, {0.02f,0.10f,0.16f,1.0f}, {0.22f,0.82f,1.0f,1.0f});
        drawTextCentered("CARS", 173.0f, 178.0f, 0.35f, {0.22f,0.82f,1.0f,1.0f});
        drawTextCentered("GARAGE", pcx, py0 + 18.0f, 0.80f, { 0.42f, 0.85f, 1.0f, 1.0f });
        drawTextCentered("COINS " + std::to_string(game.bank()), pcx, py0 + 70.0f, 0.42f, { 1.0f, 0.88f, 0.28f, 1.0f });

        // улучшения
        for (int i = 0; i < kUpgradeCount; ++i) {
            const float ry = py0 + 125.0f + static_cast<float>(i) * 64.0f;
            const int level = game.upgradeLevel(i);
            const int cost = game.upgradeCost(i);
            const bool maxed = cost < 0;
            const bool canBuy = !maxed && game.bank() >= cost;

            drawPanel(
                px0 + 24.0f, ry, pw - 48.0f, 56.0f,
                { 0.03f, 0.04f, 0.055f, 0.90f },
                canBuy ? Color{ 0.35f, 1.0f, 0.50f, 0.80f } : Color{ 0.40f, 0.48f, 0.55f, 0.50f }
            );

            drawText(std::to_string(i + 1), px0 + 44.0f, ry + 8.0f, 0.42f, { 1.0f, 0.78f, 0.20f, 1.0f });
            drawText(Game::upgradeName(i), px0 + 90.0f, ry + 6.0f, 0.30f, { 0.90f, 0.95f, 1.0f, 1.0f });
            drawText(Game::upgradeInfo(i), px0 + 90.0f, ry + 33.0f, 0.17f, { 0.65f, 0.72f, 0.78f, 1.0f });

            drawSegBar(
                px0 + 390.0f, ry + 17.0f, 150.0f, 22.0f,
                static_cast<float>(level) / static_cast<float>(kMaxUpgradeLevel),
                kMaxUpgradeLevel,
                { 0.22f, 0.78f, 1.0f, 1.0f },
                { 0.08f, 0.11f, 0.14f, 0.95f }
            );

            if (maxed) {
                drawText("MAX", px0 + 575.0f, ry + 11.0f, 0.32f, { 0.35f, 1.0f, 0.50f, 1.0f });
            }
            else {
                drawText(
                    std::to_string(cost),
                    px0 + 575.0f, ry + 11.0f, 0.34f,
                    canBuy ? Color{ 1.0f, 0.88f, 0.28f, 1.0f } : Color{ 0.80f, 0.30f, 0.25f, 1.0f }
                );
            }
        }

        // покраска
        const float paintY = py0 + 395.0f;

        drawText("PAINT", px0 + 28.0f, paintY - 4.0f, 0.28f, { 0.76f, 0.84f, 0.90f, 1.0f });

        const float swW = 74.0f;
        const float swGap = 14.0f;
        const float swTotal = static_cast<float>(kPaintCount) * swW + static_cast<float>(kPaintCount - 1) * swGap;
        const float swX0 = pcx - swTotal * 0.5f;
        const float swY = paintY + 28.0f;

        for (int i = 0; i < kPaintCount; ++i) {
            const float sx = swX0 + static_cast<float>(i) * (swW + swGap);
            const bool unlocked = game.paintUnlocked(i);
            const bool equipped = game.selectedPaint() == i;
            const bool cursor = game.paintCursor() == i;

            Color c = Game::paintColor(i);

            if (!unlocked) {
                c = mixColor(c, { 0.03f, 0.03f, 0.04f, 1.0f }, 0.62f);
            }

            Color border = { 0.30f, 0.36f, 0.42f, 0.70f };
            if (equipped) {
                border = { 0.35f, 1.0f, 0.50f, 1.0f };
            }
            if (cursor) {
                border = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            drawPanel(sx, swY, swW, 44.0f, c, border);

            if (unlocked) {
                drawTextCentered(
                    equipped ? "ON" : "OWN",
                    sx + swW * 0.5f, swY + 52.0f, 0.18f,
                    equipped ? Color{ 0.35f, 1.0f, 0.50f, 1.0f } : Color{ 0.65f, 0.72f, 0.78f, 1.0f }
                );
            }
            else {
                const bool affordable = game.bank() >= Game::paintCost(i);

                drawTextCentered(
                    std::to_string(Game::paintCost(i)),
                    sx + swW * 0.5f, swY + 52.0f, 0.20f,
                    affordable ? Color{ 1.0f, 0.88f, 0.28f, 1.0f } : Color{ 0.80f, 0.30f, 0.25f, 1.0f }
                );
            }
        }

        // что делает Enter на выбранном цвете
        {
            const int cur = game.paintCursor();
            std::string line = Game::paintName(cur);
            Color lc = { 0.90f, 0.95f, 1.0f, 1.0f };

            if (game.selectedPaint() == cur) {
                line += "  EQUIPPED";
                lc = { 0.35f, 1.0f, 0.50f, 1.0f };
            }
            else if (game.paintUnlocked(cur)) {
                line += "  ENTER EQUIP";
            }
            else if (game.bank() >= Game::paintCost(cur)) {
                line += "  ENTER BUY " + std::to_string(Game::paintCost(cur));
                lc = { 1.0f, 0.88f, 0.28f, 1.0f };
            }
            else {
                line += "  NOT ENOUGH COINS";
                lc = { 0.80f, 0.30f, 0.25f, 1.0f };
            }

            drawTextCentered(line, pcx, swY + 78.0f, 0.24f, lc);
        }

        drawTextCentered("1 2 3 4 UPGRADE   ARROWS ENTER PAINT   ESC BACK", pcx, py0 + ph - 34.0f, 0.22f, { 0.80f, 0.90f, 0.95f, 1.0f });

        // ---- выбор машины: панель справа поверх превью ----
        {
            const int cur = game.carCursor();
            const float iw = 340.0f;
            const float ih = 236.0f;
            const float ix = w - iw - 24.0f;
            const float iy = h - ih - 16.0f;
            const float icx = ix + iw * 0.5f;

            drawPanel(ix, iy, iw, ih, { 0.015f, 0.022f, 0.030f, 0.90f }, { 1.0f, 0.78f, 0.20f, 0.90f });

            drawTextCentered(Game::carName(cur), icx, iy + 12.0f, 0.42f, { 1.0f, 0.83f, 0.22f, 1.0f });
            drawTextCentered("<    CAR " + std::to_string(cur + 1) + " / " + std::to_string(kCarCount) + "    >", icx, iy + 44.0f, 0.22f, { 0.65f, 0.72f, 0.78f, 1.0f });

            const float vals[3] = {
                std::clamp((Game::carSpeed(cur) - 0.85f) / 0.95f, 0.0f, 1.0f),
                std::clamp((Game::carGrip(cur) - 0.75f) / 0.50f, 0.0f, 1.0f),
                std::clamp((1.40f - Game::carArmor(cur)) / 0.75f, 0.0f, 1.0f)
            };
            const char* labels[3] = { "SPEED", "GRIP", "ARMOR" };

            for (int i = 0; i < 3; ++i) {
                const float ry = iy + 68.0f + static_cast<float>(i) * 30.0f;

                drawText(labels[i], ix + 20.0f, ry + 2.0f, 0.22f, labelColor);
                drawSegBar(ix + 120.0f, ry, 200.0f, 20.0f, vals[i], 10, { 0.22f, 0.78f, 1.0f, 1.0f }, { 0.08f, 0.11f, 0.14f, 0.95f });
            }

            std::string status;
            Color sc = { 0.90f, 0.95f, 1.0f, 1.0f };

            if (game.selectedCar() == cur) {
                status = "EQUIPPED";
                sc = { 0.35f, 1.0f, 0.50f, 1.0f };
            }
            else if (game.carUnlocked(cur)) {
                status = "CLICK / SPACE EQUIP";
            }
            else if (game.level() < Game::carLevelReq(cur)) {
                status = "NEED LEVEL " + std::to_string(Game::carLevelReq(cur));
                sc = { 0.80f, 0.30f, 0.25f, 1.0f };
            }
            else if (game.bank() >= Game::carCost(cur)) {
                status = "CLICK / SPACE BUY " + std::to_string(Game::carCost(cur));
                sc = { 1.0f, 0.88f, 0.28f, 1.0f };
            }
            else {
                status = "NOT ENOUGH COINS";
                sc = { 0.80f, 0.30f, 0.25f, 1.0f };
            }

            drawTextCentered(status, icx, iy + 160.0f, 0.24f, sc);

            if (!game.carUnlocked(cur)) {
                drawTextCentered(
                    "PRICE " + std::to_string(Game::carCost(cur)) + "   LEVEL " + std::to_string(Game::carLevelReq(cur)),
                    icx, iy + 188.0f, 0.18f, { 0.65f, 0.72f, 0.78f, 1.0f }
                );
            }

            drawTextCentered("CLICK < > OR Q E TO BROWSE", icx, iy + 212.0f, 0.18f, { 0.80f, 0.90f, 0.95f, 1.0f });
        }
    }

    // ---- Настройки ----
    if (game.state() == GameState::Settings) {
        drawOverlayRect(0.0f, 0.0f, w, h, { 0.0f, 0.0f, 0.0f, 0.86f });

        const float pw = 760.0f;
        const float rowH = 62.0f;
        const float ph = 110.0f + rowH * static_cast<float>(kSettingCount) + 80.0f;
        const float px0 = centerX - pw * 0.5f;
        const float py0 = h * 0.5f - ph * 0.5f;

        drawPanel(px0, py0, pw, ph, { 0.015f, 0.022f, 0.030f, 0.96f }, { 0.42f, 0.85f, 1.0f, 0.90f });

        drawTextCentered("SETTINGS", centerX, py0 + 18.0f, 0.70f, { 0.42f, 0.85f, 1.0f, 1.0f });
        drawTextCentered("SOUND AND EFFECTS", centerX, py0 + 70.0f, 0.22f, { 0.65f, 0.72f, 0.78f, 1.0f });

        for (int i = 0; i < kSettingCount; ++i) {
            const float ry = py0 + 104.0f + static_cast<float>(i) * rowH;
            const bool selected = game.settingsCursor() == i;
            const int value = game.settingValue(i);

            drawPanel(
                px0 + 24.0f, ry, pw - 48.0f, rowH - 8.0f,
                { 0.03f, 0.04f, 0.055f, 0.90f },
                selected ? Color{ 1.0f, 0.78f, 0.20f, 0.95f } : Color{ 0.40f, 0.48f, 0.55f, 0.50f }
            );

            drawText(
                Game::settingName(i), px0 + 44.0f, ry + 6.0f, 0.28f,
                selected ? Color{ 1.0f, 0.83f, 0.22f, 1.0f } : Color{ 0.90f, 0.95f, 1.0f, 1.0f }
            );
            drawText(Game::settingInfo(i), px0 + 44.0f, ry + 33.0f, 0.15f, { 0.65f, 0.72f, 0.78f, 1.0f });

            drawSegBar(
                px0 + 400.0f, ry + 13.0f, 220.0f, 22.0f,
                static_cast<float>(value) / static_cast<float>(kSettingMax),
                kSettingMax,
                { 0.22f, 0.78f, 1.0f, 1.0f },
                { 0.08f, 0.11f, 0.14f, 0.95f }
            );

            drawText(
                std::to_string(value * 10), px0 + 640.0f, ry + 12.0f, 0.30f,
                selected ? Color{ 1.0f, 0.88f, 0.28f, 1.0f } : Color{ 0.80f, 0.90f, 0.95f, 1.0f }
            );
        }

        drawTextCentered("UP DOWN SELECT   LEFT RIGHT CHANGE   ESC BACK", centerX, py0 + ph - 44.0f, 0.22f, { 0.80f, 0.90f, 0.95f, 1.0f });
    }

    // ---- Достижения ----
    if (game.state() == GameState::Achievements) {
        drawOverlayRect(0.0f, 0.0f, w, h, { 0.0f, 0.0f, 0.0f, 0.86f });

        const float pw = 760.0f;
        const float ph = 600.0f;
        const float px0 = centerX - pw * 0.5f;
        const float py0 = h * 0.5f - ph * 0.5f;

        drawPanel(px0, py0, pw, ph, { 0.015f, 0.022f, 0.030f, 0.96f }, { 1.0f, 0.78f, 0.20f, 0.90f });

        drawTextCentered("ACHIEVEMENTS", centerX, py0 + 18.0f, 0.70f, { 1.0f, 0.83f, 0.22f, 1.0f });
        drawTextCentered(
            std::to_string(game.achUnlockedCount()) + " OF " + std::to_string(kAchCount),
            centerX, py0 + 66.0f, 0.30f, { 0.55f, 0.90f, 1.0f, 1.0f }
        );

        for (int i = 0; i < kAchCount; ++i) {
            const float ry = py0 + 104.0f + static_cast<float>(i) * 52.0f;
            const bool done = game.achUnlocked(i);
            const int target = Game::achTarget(i);
            const int prog = std::min(game.achProgress(i), target);

            drawPanel(
                px0 + 24.0f, ry, pw - 48.0f, 46.0f,
                { 0.03f, 0.04f, 0.055f, 0.90f },
                done ? Color{ 0.35f, 1.0f, 0.50f, 0.80f } : Color{ 0.40f, 0.48f, 0.55f, 0.50f }
            );

            drawText(Game::achName(i), px0 + 44.0f, ry + 5.0f, 0.26f, done ? Color{ 0.35f, 1.0f, 0.50f, 1.0f } : Color{ 0.90f, 0.95f, 1.0f, 1.0f });
            drawText(Game::achInfo(i), px0 + 44.0f, ry + 29.0f, 0.16f, { 0.65f, 0.72f, 0.78f, 1.0f });

            drawSegBar(
                px0 + 440.0f, ry + 13.0f, 150.0f, 20.0f,
                static_cast<float>(prog) / static_cast<float>(std::max(1, target)),
                10,
                done ? Color{ 0.35f, 1.0f, 0.50f, 1.0f } : Color{ 0.22f, 0.78f, 1.0f, 1.0f },
                { 0.08f, 0.11f, 0.14f, 0.95f }
            );

            if (done) {
                drawText("DONE", px0 + 620.0f, ry + 14.0f, 0.26f, { 0.35f, 1.0f, 0.50f, 1.0f });
            }
            else {
                drawText("+" + std::to_string(Game::achReward(i)), px0 + 620.0f, ry + 14.0f, 0.26f, { 1.0f, 0.88f, 0.28f, 1.0f });
            }
        }

        drawTextCentered("ESC OR BACK BUTTON", centerX, py0 + ph - 36.0f, 0.30f, { 0.80f, 0.90f, 0.95f, 1.0f });
    }

    if (game.state() == GameState::Garage || game.state() == GameState::Settings ||
        game.state() == GameState::Achievements)
    {
        drawPanel(w - 205.0f, 24.0f, 180.0f, 54.0f,
                  { 0.025f, 0.065f, 0.10f, 0.98f },
                  { 0.30f, 0.88f, 1.0f, 1.0f });
        drawTextCentered("< BACK", w - 115.0f, 37.0f, 0.38f,
                         { 0.92f, 0.97f, 1.0f, 1.0f });
    }

    glLineWidth(1.0f);

    glDisable(GL_BLEND);

    glEnable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}