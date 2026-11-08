#include "Renderer.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>

namespace {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kRoadWidth = 10.4f;
    constexpr float kLaneWidth = 3.35f;
    constexpr float kWorldNearZ = 14.0f;
    constexpr float kWorldFarZ = -210.0f;

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

    void drawWheel(float x, float y, float z, float radius, float width) {
        setColor({ 0.025f, 0.028f, 0.032f, 1.0f });

        glBegin(GL_QUAD_STRIP);

        constexpr int segments = 16;

        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            const float cy = std::cos(a);
            const float cz = std::sin(a);

            glNormal3f(0.0f, cy, cz);
            glVertex3f(x - width * 0.5f, y + cy * radius, z + cz * radius);
            glVertex3f(x + width * 0.5f, y + cy * radius, z + cz * radius);
        }

        glEnd();
    }

    // ---------- водитель, салон и зеркала машины игрока ----------

    // коробка вдоль отрезка a -> b (рука, нога и т.п.)
    void drawLimb(const Vec3& a, const Vec3& b, float thick, Color color) {
        const Vec3 d = b - a;
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);

        if (len < 0.0001f) {
            return;
        }

        const float yaw = std::atan2(d.x, d.z) * 180.0f / kPi;
        const float pitch = -std::asin(std::clamp(d.y / len, -1.0f, 1.0f)) * 180.0f / kPi;

        glPushMatrix();
        glTranslatef((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
        glRotatef(yaw, 0.0f, 1.0f, 0.0f);
        glRotatef(pitch, 1.0f, 0.0f, 0.0f);
        drawBox(0.0f, 0.0f, 0.0f, thick, thick, len, color);
        glPopMatrix();
    }

    // левая рука водителя: либо на двери за окном (grip = 0), либо на руле (grip = 1)
    void drawDriverLeftHand(const Vec3& elbow, const Vec3& wrist, Color skin, Color sleeve, float grip) {
        const float t = gDriverTime;
        const float w = gDriverWind;
        const float calm = 1.0f - grip;   // на руле кисть не машет

        // предплечье: рукав до середины, дальше кожа
        const Vec3 mid = {
            elbow.x + (wrist.x - elbow.x) * 0.45f,
            elbow.y + (wrist.y - elbow.y) * 0.45f,
            elbow.z + (wrist.z - elbow.z) * 0.45f
        };

        drawLimb(elbow, mid, 0.115f, sleeve);
        drawLimb(mid, wrist, 0.085f, skin);

        // часы на запястье
        drawBox(wrist.x + 0.02f, wrist.y, wrist.z, 0.07f, 0.095f, 0.095f, { 0.08f, 0.08f, 0.10f, 1.0f });
        drawBox(wrist.x + 0.02f, wrist.y + 0.052f, wrist.z, 0.045f, 0.006f, 0.05f, { 0.55f, 0.85f, 1.0f, 1.0f });

        // кисть: на окне ладонь покачивается на ветру, на руле сжимается в кулак
        const float flapAmp = (7.0f + 16.0f * w) * calm;
        const float flapFreq = 4.5f + 4.0f * w;

        const float pitch = 10.0f * calm + std::sin(t * flapFreq) * flapAmp;
        const float yawW = std::sin(t * flapFreq * 0.7f + 1.1f) * (4.0f + 8.0f * w) * calm;
        const float fist = 1.0f - 0.7f * grip;   // длина пальцев

        glPushMatrix();
        glTranslatef(wrist.x - 0.05f, wrist.y, wrist.z);
        glRotatef(yawW, 0.0f, 1.0f, 0.0f);
        glRotatef(pitch, 0.0f, 0.0f, 1.0f);

        // ладонь
        drawBox(-0.06f * fist, 0.0f, 0.0f, 0.12f * fist, 0.085f, 0.035f + 0.04f * grip, skin);

        // четыре пальца, каждый чуть по-своему дрожит
        for (int i = 0; i < 4; ++i) {
            const float fy = -0.032f + static_cast<float>(i) * 0.022f;
            const float fl = (0.085f - ((i == 0 || i == 3) ? 0.012f : 0.0f)) * fist;
            const float wob = std::sin(t * (flapFreq * 1.6f) + static_cast<float>(i) * 0.9f) * (3.0f + 6.0f * w) * calm;

            glPushMatrix();
            glTranslatef(-0.12f * fist, fy, 0.0f);
            glRotatef(wob, 0.0f, 0.0f, 1.0f);
            drawBox(-fl * 0.5f, 0.0f, 0.0f, fl, 0.019f, 0.028f + 0.02f * grip, skin);
            glPopMatrix();
        }

        // большой палец
        drawBox(-0.045f * fist, 0.058f, 0.0f, 0.05f * fist, 0.03f, 0.028f, skin);

        glPopMatrix();
    }

    // КАБРИОЛЕТ: крыши нет, водитель виден целиком
    void drawPlayerCabin(float width, float length, float bodyHeight, Color body) {
        const float half = width * 0.37f;                 // половина ширины салона
        const float beltY = bodyHeight + 0.025f;          // линия дверей
        const float windZ = -length * 0.215f;             // низ лобового стекла
        const float rearZ = length * 0.10f;               // конец салона, дальше - накладка за сиденьями

        const Color cabin = mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.18f);
        const Color seat = { 0.13f, 0.12f, 0.14f, 1.0f };
        const Color seatAccent = { 0.62f, 0.10f, 0.08f, 1.0f };
        const Color skin = { 0.86f, 0.66f, 0.52f, 1.0f };
        const Color hair = { 0.14f, 0.09f, 0.06f, 1.0f };
        const Color jacket = { 0.96f, 0.55f, 0.12f, 1.0f };
        const Color jacketDark = { 0.78f, 0.40f, 0.08f, 1.0f };
        const Color metal = { 0.72f, 0.74f, 0.78f, 1.0f };

        // 0 = рука на окне, 1 = обе руки на руле (плавно)
        const float grip = gDriverAlert * gDriverAlert * (3.0f - 2.0f * gDriverAlert);

        // тёмный пол салона
        drawBox(0.0f, beltY + 0.006f, (windZ + rearZ) * 0.5f, half * 2.0f, 0.012f, rearZ - windZ, { 0.06f, 0.06f, 0.07f, 1.0f });

        // борта дверей: на левом лежит локоть водителя
        for (int side = -1; side <= 1; side += 2) {
            drawBox(static_cast<float>(side) * (half + 0.03f), beltY + 0.02f, (windZ + rearZ) * 0.5f, 0.10f, 0.06f, rearZ - windZ, cabin);
        }

        // накладка за сиденьями и две дуги безопасности
        drawBox(0.0f, beltY + 0.02f, (rearZ + length * 0.17f) * 0.5f, half * 2.0f + 0.16f, 0.05f, length * 0.17f - rearZ, cabin);

        for (int side = -1; side <= 1; side += 2) {
            const float hx = static_cast<float>(side) * half * 0.52f;

            drawLimb({ hx, beltY + 0.02f, rearZ + 0.04f }, { hx, beltY + 0.30f, rearZ + 0.02f }, 0.055f, metal);
            drawBox(hx, beltY + 0.325f, rearZ + 0.02f, 0.13f, 0.045f, 0.08f, metal);
        }

        // лобовое стекло: рамка и полупрозрачное стекло
        const float wTopY = beltY + 0.34f;
        const float wTopZ = windZ + 0.20f;

        for (int side = -1; side <= 1; side += 2) {
            const float sx = static_cast<float>(side) * (half + 0.01f);

            drawLimb({ sx, beltY, windZ }, { sx, wTopY, wTopZ }, 0.05f, cabin);
        }
        drawBox(0.0f, wTopY, wTopZ, half * 2.0f + 0.04f, 0.04f, 0.05f, cabin);

        // ---- салон ----
        const float seatBackZ = -0.08f;

        for (int side = -1; side <= 1; side += 2) {
            const float sx = static_cast<float>(side) * half * 0.52f;

            // подушка и спинка
            drawBox(sx, bodyHeight - 0.06f, seatBackZ - 0.17f, 0.42f, 0.10f, 0.40f, seat);
            drawBox(sx, bodyHeight + 0.10f, seatBackZ, 0.42f, 0.36f, 0.10f, seat);
            drawBox(sx, bodyHeight + 0.10f, seatBackZ + 0.052f, 0.30f, 0.28f, 0.012f, seatAccent);
        }

        // водитель (левое сиденье, x < 0)
        const float dx = -half * 0.52f;
        const float torsoZ = seatBackZ - 0.17f;

        // торс, плечи, шея, голова
        drawBox(dx, bodyHeight + 0.08f, torsoZ, 0.38f, 0.30f, 0.20f, jacket);
        drawBox(dx, bodyHeight + 0.23f, torsoZ, 0.44f, 0.08f, 0.20f, jacketDark);
        drawBox(dx, bodyHeight + 0.30f, torsoZ, 0.09f, 0.07f, 0.09f, skin);

        // голова чуть поворачивается к зеркалу, когда рука на окне, и смотрит прямо на дорогу, когда на руле
        const float headTurn = (1.0f - grip) * std::sin(gDriverTime * 0.6f) * 6.0f;

        glPushMatrix();
        glTranslatef(dx, 0.0f, torsoZ + 0.01f);
        glRotatef(headTurn, 0.0f, 1.0f, 0.0f);

        const float headY = bodyHeight + 0.41f;
        const float flutter = std::sin(gDriverTime * 17.0f) * 0.012f * gDriverWind;

        drawBox(0.0f, headY, 0.0f, 0.19f, 0.20f, 0.20f, skin);
        drawBox(0.0f, headY + 0.07f, 0.015f, 0.215f, 0.075f, 0.225f, hair);                 // волосы сверху
        drawBox(0.0f, headY - 0.01f, 0.105f + flutter, 0.20f, 0.15f, 0.03f, hair);          // затылок, треплет ветер
        drawBox(-0.105f, headY - 0.01f, 0.0f, 0.025f, 0.06f, 0.04f, skin);                  // уши
        drawBox(0.105f, headY - 0.01f, 0.0f, 0.025f, 0.06f, 0.04f, skin);

        glPopMatrix();

        // руль
        const float wheelZ = torsoZ - 0.30f;
        const Color wheelC = { 0.05f, 0.05f, 0.06f, 1.0f };

        drawBox(dx, bodyHeight + 0.34f, wheelZ, 0.46f, 0.04f, 0.04f, wheelC);
        drawBox(dx, bodyHeight + 0.10f, wheelZ, 0.46f, 0.04f, 0.04f, wheelC);
        drawBox(dx - 0.23f, bodyHeight + 0.22f, wheelZ, 0.04f, 0.28f, 0.04f, wheelC);
        drawBox(dx + 0.23f, bodyHeight + 0.22f, wheelZ, 0.04f, 0.28f, 0.04f, wheelC);

        // правая рука всегда на руле
        const Vec3 rShoulder = { dx + 0.18f, bodyHeight + 0.21f, torsoZ };
        const Vec3 rElbow = { dx + 0.40f, bodyHeight + 0.14f, torsoZ - 0.10f };
        const Vec3 rHand = { dx + 0.23f, bodyHeight + 0.22f, wheelZ + 0.02f };

        drawLimb(rShoulder, rElbow, 0.105f, jacket);
        drawLimb(rElbow, rHand, 0.085f, skin);
        drawBox(rHand.x, rHand.y, rHand.z, 0.075f, 0.09f, 0.075f, skin);

        // левая рука: плечо -> (локоть, запястье) между окном и рулём
        const Vec3 lShoulder = { dx - 0.20f, bodyHeight + 0.21f, torsoZ };

        const Vec3 elbowRest = { -(half + 0.07f), beltY + 0.08f, torsoZ - 0.12f };
        const Vec3 wristRest = { -(half + 0.56f), beltY + 0.20f, torsoZ + 0.04f };

        const Vec3 elbowGrip = { dx - 0.40f, bodyHeight + 0.14f, torsoZ - 0.10f };
        const Vec3 wristGrip = { dx - 0.17f, bodyHeight + 0.22f, wheelZ + 0.03f };

        const Vec3 elbow = {
            elbowRest.x + (elbowGrip.x - elbowRest.x) * grip,
            elbowRest.y + (elbowGrip.y - elbowRest.y) * grip,
            elbowRest.z + (elbowGrip.z - elbowRest.z) * grip
        };

        const Vec3 wrist = {
            wristRest.x + (wristGrip.x - wristRest.x) * grip,
            wristRest.y + (wristGrip.y - wristRest.y) * grip,
            wristRest.z + (wristGrip.z - wristRest.z) * grip
        };

        drawLimb(lShoulder, elbow, 0.115f, jacket);
        drawDriverLeftHand(elbow, wrist, skin, jacket, grip);

        // ---- лобовое стекло (полупрозрачное, рисуется последним) ----
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        glBegin(GL_QUADS);
        setColor({ 0.12f, 0.26f, 0.34f, 0.28f });
        glVertex3f(-half, beltY, windZ);
        glVertex3f(half, beltY, windZ);
        glVertex3f(half, wTopY, wTopZ);
        glVertex3f(-half, wTopY, wTopZ);
        glEnd();

        glBegin(GL_QUADS);
        setColor({ 0.80f, 0.92f, 1.0f, 0.14f });
        glVertex3f(half * 0.30f, beltY, windZ);
        glVertex3f(half * 0.65f, beltY, windZ);
        glVertex3f(half * 0.65f, wTopY, wTopZ);
        glVertex3f(half * 0.30f, wTopY, wTopZ);
        glEnd();

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    // боковые зеркала игрока: крупные, на ножке, с отражающим стеклом
    void drawPlayerMirrors(float width, float length, float bodyHeight, Color body) {
        const Color housing = mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.35f);
        const Color glassM = { 0.62f, 0.78f, 0.92f, 1.0f };
        const float my = bodyHeight + 0.15f;
        const float mz = -length * 0.15f;

        for (int side = -1; side <= 1; side += 2) {
            const float sgn = static_cast<float>(side);
            const float mx = sgn * (width * 0.5f + 0.17f);

            // ножка
            drawBox(sgn * (width * 0.5f + 0.05f), my - 0.05f, mz, 0.12f, 0.04f, 0.05f, housing);

            // корпус и стекло, развёрнутое назад к камере
            drawBox(mx, my, mz, 0.17f, 0.13f, 0.08f, housing);
            drawBox(mx, my, mz + 0.043f, 0.145f, 0.105f, 0.012f, glassM);

            // блик
            drawBox(mx - sgn * 0.03f, my + 0.02f, mz + 0.051f, 0.03f, 0.04f, 0.004f, { 1.0f, 1.0f, 1.0f, 1.0f });
        }
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
        bool player
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
            { 0.0f, 0.0f, 0.0f, 0.28f },
            0.025f
        );

        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);

        const float bodyHeight = height * 0.48f;

        drawBox(0.0f, bodyHeight * 0.55f, 0.0f, width, bodyHeight, length, body);

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
        else {
            const float cabinLength =
                type == TrafficType::Sport
                ? length * 0.38f
                : length * 0.44f;

            const float cabinHeight =
                type == TrafficType::Van
                ? height * 0.63f
                : height * 0.52f;
            if (player) {
                drawPlayerCabin(width, length, bodyHeight, body);
                drawPlayerMirrors(width, length, bodyHeight, body);
            }
            else {

            drawBox(
                0.0f,
                bodyHeight + cabinHeight * 0.45f,
                -length * 0.03f,
                width * 0.74f,
                cabinHeight,
                cabinLength,
                mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.18f)
            );

            drawBox(
                0.0f,
                bodyHeight + cabinHeight * 0.57f,
                -length * 0.13f,
                width * 0.58f,
                cabinHeight * 0.44f,
                cabinLength * 0.55f,
                { 0.05f, 0.13f, 0.19f, 1.0f }
            );

            // боковые окна
            const Color glass = { 0.05f, 0.13f, 0.19f, 1.0f };
            const float sideX = width * 0.37f + 0.012f;
            const float sideY = bodyHeight + cabinHeight * 0.58f;

            drawBox(-sideX, sideY, -length * 0.03f, 0.02f, cabinHeight * 0.50f, cabinLength * 0.82f, glass);
            drawBox(sideX, sideY, -length * 0.03f, 0.02f, cabinHeight * 0.50f, cabinLength * 0.82f, glass);

            // боковые зеркала
            const Color mirror = mixColor(body, { 0.0f, 0.0f, 0.0f, 1.0f }, 0.35f);

            drawBox(-(width * 0.5f + 0.10f), bodyHeight + 0.10f, -length * 0.14f, 0.18f, 0.10f, 0.12f, mirror);
            drawBox(width * 0.5f + 0.10f, bodyHeight + 0.10f, -length * 0.14f, 0.18f, 0.10f, 0.12f, mirror);
            }
            // задний спойлер у спортивных машин в потоке
            if (type == TrafficType::Sport && !player) {
                const Color dark = { 0.04f, 0.04f, 0.05f, 1.0f };

                drawBox(0.0f, bodyHeight + 0.20f, length * 0.43f, width * 0.80f, 0.06f, 0.22f, dark);
                drawBox(-width * 0.30f, bodyHeight + 0.08f, length * 0.43f, 0.06f, 0.16f, 0.12f, dark);
                drawBox(width * 0.30f, bodyHeight + 0.08f, length * 0.43f, 0.06f, 0.16f, 0.12f, dark);
            }

            // гоночные полосы на машине игрока
            if (player) {
                const Color stripe = { 0.95f, 0.96f, 1.0f, 1.0f };
                const float topY = bodyHeight * 1.05f + 0.006f;
                //const float roofY = bodyHeight + cabinHeight * 0.95f + 0.006f;

                drawBox(0.0f, topY, -length * 0.38f, width * 0.18f, 0.012f, length * 0.24f, stripe);
                //drawBox(0.0f, roofY, -length * 0.03f, width * 0.16f, 0.012f, cabinLength, stripe);
                drawBox(0.0f, topY, length * 0.35f, width * 0.18f, 0.012f, length * 0.28f, stripe);
            }
        }

        const float wheelY = 0.27f;
        const float wheelZ1 = -length * 0.28f;
        const float wheelZ2 = length * 0.28f;

        drawWheel(-width * 0.53f, wheelY, wheelZ1, 0.31f, 0.20f);
        drawWheel(width * 0.53f, wheelY, wheelZ1, 0.31f, 0.20f);
        drawWheel(-width * 0.53f, wheelY, wheelZ2, 0.31f, 0.20f);
        drawWheel(width * 0.53f, wheelY, wheelZ2, 0.31f, 0.20f);

        // диски колёс
        const Color rim = { 0.58f, 0.60f, 0.64f, 1.0f };
        const float rimX = width * 0.53f + 0.11f;

        drawBox(-rimX, wheelY, wheelZ1, 0.04f, 0.30f, 0.30f, rim);
        drawBox(rimX, wheelY, wheelZ1, 0.04f, 0.30f, 0.30f, rim);
        drawBox(-rimX, wheelY, wheelZ2, 0.04f, 0.30f, 0.30f, rim);
        drawBox(rimX, wheelY, wheelZ2, 0.04f, 0.30f, 0.30f, rim);

        // бамперы, планка стоп-сигналов и номер
        const Color bumper = { 0.07f, 0.07f, 0.08f, 1.0f };

        drawBox(0.0f, 0.28f, -length * 0.5f - 0.02f, width * 0.96f, 0.16f, 0.10f, bumper);
        drawBox(0.0f, 0.28f, length * 0.5f + 0.02f, width * 0.96f, 0.16f, 0.10f, bumper);
        drawBox(0.0f, 0.42f, length * 0.506f, width * 0.30f, 0.05f, 0.06f, { 0.35f, 0.02f, 0.02f, 1.0f });
        drawBox(0.0f, 0.32f, length * 0.512f, width * 0.22f, 0.10f, 0.03f, { 0.90f, 0.90f, 0.85f, 1.0f });

        const Color frontLight =
            player
            ? Color{ 1.0f, 0.95f, 0.72f, 1.0f }
        : Color{ 1.0f, 0.90f, 0.58f, 1.0f };

        const Color rearLight = { 1.0f, 0.04f, 0.03f, 1.0f };

        drawBox(-width * 0.27f, 0.42f, -length * 0.505f, width * 0.21f, 0.12f, 0.07f, frontLight);
        drawBox(width * 0.27f, 0.42f, -length * 0.505f, width * 0.21f, 0.12f, 0.07f, frontLight);

        drawBox(-width * 0.27f, 0.42f, length * 0.505f, width * 0.18f, 0.12f, 0.07f, rearLight);
        drawBox(width * 0.27f, 0.42f, length * 0.505f, width * 0.18f, 0.12f, 0.07f, rearLight);

        if (player) {
            drawBox(
                0.0f,
                bodyHeight + 0.12f,
                length * 0.37f,
                width * 0.72f,
                0.08f,
                length * 0.08f,
                { 0.04f, 0.04f, 0.05f, 1.0f }
            );
        }

        // свечение стоп-сигналов и свет фар игрока
        beginGlow();

        const float rg = 0.40f + 0.45f * gNightLevel;

        drawGlow(-width * 0.27f, 0.42f, length * 0.53f, 0.55f, { 1.0f, 0.10f, 0.05f, rg });
        drawGlow(width * 0.27f, 0.42f, length * 0.53f, 0.55f, { 1.0f, 0.10f, 0.05f, rg });

        if (gRain > 0.01f) {
            drawGroundGlow(-width * 0.27f, length * 0.5f + 2.2f, 0.4f, 2.6f, { 1.0f, 0.10f, 0.05f, 0.35f * gRain }, 0.05f);
            drawGroundGlow(width * 0.27f, length * 0.5f + 2.2f, 0.4f, 2.6f, { 1.0f, 0.10f, 0.05f, 0.35f * gRain }, 0.05f);
        }

        if (player) {
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

        endGlow();

        glPopMatrix();
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

    void drawRoad(float offset, float worldTime) {
        const float pulse = 0.5f + 0.5f * std::sin(worldTime * 0.08f);

        const Color grassA = { 0.028f, 0.11f, 0.060f, 1.0f };
        const Color grassB = { 0.022f, 0.075f, 0.045f, 1.0f };
        const Color grass = mixColor(grassA, grassB, pulse * 0.4f);

        drawGroundQuad(-90.0f, kWorldNearZ, 90.0f, kWorldFarZ, grass);

        drawGroundQuad(
            -kRoadWidth * 0.5f,
            kWorldNearZ,
            kRoadWidth * 0.5f,
            kWorldFarZ,
            { 0.065f, 0.070f, 0.077f, 1.0f },
            0.01f
        );

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

            drawCylinder(-11.5f + sway, 0.0f, z, 0.22f, 2.0f, 10, { 0.28f, 0.17f, 0.07f, 1.0f });
            drawCylinder(11.5f - sway, 0.0f, z - 11.0f, 0.22f, 2.0f, 10, { 0.28f, 0.17f, 0.07f, 1.0f });

            drawBox(-11.5f + sway, 2.45f, z, 2.8f, 2.3f, 2.8f, { 0.035f, 0.28f, 0.10f, 1.0f });
            drawBox(11.5f - sway, 2.45f, z - 11.0f, 2.8f, 2.3f, 2.8f, { 0.035f, 0.26f, 0.095f, 1.0f });
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

        for (int i = 0; i < 9; ++i) {
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
    unsigned int gSeed = 12345u;

    float rnd() {
        gSeed = gSeed * 1664525u + 1013904223u;
        return static_cast<float>(gSeed >> 8) * (1.0f / 16777216.0f);
    }

    void emitParticle(float x, float y, float z, float vx, float vy, float vz, float life, float size, Color c) {
        if (gParticles.size() > 700) {
            return;
        }
        gParticles.push_back({ x, y, z, vx, vy, vz, life, life, size, c });
    }

    void updateEffects(const Game& game) {
        const float t = game.worldTime();
        float dt = t - gLastTime;

        if (dt < 0.0f) {
            // перезапуск игры
            gParticles.clear();
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

        gFovExtra = std::clamp((speed - 150.0f) / 200.0f, 0.0f, 1.0f) * 5.0f + gBoostBlend * 7.0f;

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

        // пыль из-под колёс на высокой скорости
        gDustAcc += dt * 45.0f * std::clamp((speed - 100.0f) / 200.0f, 0.0f, 1.0f);
        while (gDustAcc >= 1.0f) {
            gDustAcc -= 1.0f;
            const float side = rnd() < 0.5f ? -0.75f : 0.75f;

            emitParticle(
                px + side, 0.15f + rnd() * 0.2f, pz + 1.2f,
                (rnd() - 0.5f) * 2.0f, 0.4f + rnd() * 0.8f, speedMs * 0.35f + rnd() * 3.0f,
                0.5f + rnd() * 0.3f, 0.18f,
                { 0.55f, 0.58f, 0.65f, 0.35f }
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
            p.vy -= 2.0f * dt;

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

    // линии скорости, расходящиеся от точки схода
    void drawSpeedLines(float w, float h, float t, float speedKmh) {
        const float intensity =
            std::clamp((speedKmh - 170.0f) / 150.0f, 0.0f, 1.0f) * 0.30f +
            gBoostBlend * 0.45f;

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
    gRain = game.rain();

    // грозовое небо: серое, темнее ночью
    const float nn = std::clamp(night / 0.78f, 0.0f, 1.0f);
    const Color stormTop = mixColor({ 0.30f, 0.33f, 0.38f, 1.0f }, { 0.02f, 0.03f, 0.05f, 1.0f }, nn);
    const Color stormHorizon = mixColor({ 0.50f, 0.53f, 0.57f, 1.0f }, { 0.06f, 0.08f, 0.11f, 1.0f }, nn);
    sky.top = mixColor(sky.top, stormTop, gRain * 0.75f);
    sky.horizon = mixColor(sky.horizon, stormHorizon, gRain * 0.75f);

    gNightLevel = std::clamp((night - 0.30f) / 0.35f, 0.0f, 1.0f);

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
    glFogf(GL_FOG_START, 85.0f - 45.0f * gRain);
    glFogf(GL_FOG_END, 205.0f - 90.0f * gRain);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

    const float cameraLean = game.playerYaw() * 0.035f;

    // тряска камеры: лёгкая при бусте, сильная при ударе
    const float tt = game.worldTime();
    const float shakeAmp = 0.035f * gBoostBlend + 0.35f * gShake;
    const float shakeX = std::sin(tt * 47.0f) * shakeAmp;
    const float shakeY = std::sin(tt * 61.0f + 1.3f) * shakeAmp * 0.7f;

    lookAt(
        { game.playerX() * 0.20f - cameraLean + shakeX, 6.3f + shakeY, 14.6f },
        { game.playerX() * 0.16f, 0.70f, -20.0f },
        { 0.0f, 1.0f, 0.0f }
    );

    const GLfloat lightPosition[] = { -8.0f, 18.0f, 6.0f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

    drawRoad(game.roadOffset(), game.worldTime());
    drawEnvironment(game.roadOffset(), game.worldTime());
    drawTunnel(game.playerZ());

    for (const auto& coin : game.pickups()) {
        drawCoin(coin.x, coin.z, coin.phase);
    }

    for (const auto& vehicle : game.traffic()) {
        drawCarModel(
            vehicle.x,
            vehicle.z,
            vehicle.width,
            vehicle.length,
            vehicle.height,
            vehicle.color,
            vehicle.type,
            0.0f,
            false
        );
    }

    drawCarModel(
        game.playerX(),
        game.playerZ(),
        1.62f,
        3.70f,
        1.02f,
        { 0.04f, 0.42f, 1.0f, 1.0f },
        TrafficType::Sport,
        game.playerYaw(),
        true
    );

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

    // импульс при росте комбо
    if (game.combo() > gPrevCombo) {
        gComboPulseStart = t;
    }
    gPrevCombo = game.combo();

    float since = t - gComboPulseStart;
    if (since < 0.0f) {
        since = 99.0f;
    }
    const float pulse = std::exp(-since * 7.0f);

    // линии скорости, дождь, молния и красная вспышка при ударе
    // (gRain уже равен 0 в туннеле, отдельной обработки не нужно)
    // затемнение по краям на большой скорости (эффект «туннельного зрения»)
    const float speedVignette =
        std::clamp((game.speedKmh() - 200.0f) / 250.0f, 0.0f, 1.0f) * 0.35f +
        gBoostBlend * 0.20f;

    if (speedVignette > 0.01f) {
        drawEdgeGlow(w, h, { 0.0f, 0.0f, 0.0f, speedVignette });
    }

    drawSpeedLines(w, h, t, game.speedKmh());

    drawRain(w, h, t, game.speedKmh());

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

    // ---- Счёт ----
    drawPanel(20.0f, 18.0f, 290.0f, 114.0f, panelBg, { 1.0f, 0.78f, 0.20f, 0.85f });

    drawText("SCORE", 36.0f, 30.0f, 0.38f, labelColor);
    drawText(std::to_string(game.score()), 36.0f, 57.0f, 0.62f, { 1.0f, 0.83f, 0.22f, 1.0f });
    drawText("COINS", 36.0f, 96.0f, 0.30f, { 0.95f, 0.84f, 0.35f, 1.0f });
    drawText(std::to_string(game.coins()), 142.0f, 93.0f, 0.36f, { 1.0f, 0.92f, 0.30f, 1.0f });

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

    if (gTunnelInside > 0.5f) {
        drawTextCentered("TUNNEL", centerX, 142.0f, 0.28f, { 1.0f, 0.35f, 0.80f, 0.65f + 0.35f * std::sin(t * 4.0f) });
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
            drawTextCentered("G GARAGE", centerX, h * 0.5f + 84.0f, 0.34f, { 0.42f, 0.85f, 1.0f, 1.0f });
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
        }
    }

    glLineWidth(1.0f);

    glDisable(GL_BLEND);

    glEnable(GL_FOG);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}