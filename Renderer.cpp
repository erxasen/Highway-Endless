#include "Renderer.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <string>

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
        return {
            a.x - b.x,
            a.y - b.y,
            a.z - b.z
        };
    }

    Vec3 cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    Vec3 normalize(const Vec3& v) {
        const float len =
            std::sqrt(
                v.x * v.x +
                v.y * v.y +
                v.z * v.z
            );

        if (len < 0.0001f) {
            return {
                0.0f,
                0.0f,
                0.0f
            };
        }

        return {
            v.x / len,
            v.y / len,
            v.z / len
        };
    }

    Color mixColor(
        const Color& a,
        const Color& b,
        float t
    ) {
        t =
            std::clamp(
                t,
                0.0f,
                1.0f
            );

        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }

    void setColor(
        const Color& c
    ) {
        glColor4f(
            c.r,
            c.g,
            c.b,
            c.a
        );
    }

    void setPerspective(
        float fovYDegrees,
        float aspect,
        float nearPlane,
        float farPlane
    ) {
        const float top =
            std::tan(
                fovYDegrees *
                kPi /
                360.0f
            ) *
            nearPlane;

        const float right =
            top * aspect;

        glFrustum(
            -right,
            right,
            -top,
            top,
            nearPlane,
            farPlane
        );
    }

    void lookAt(
        const Vec3& eye,
        const Vec3& center,
        const Vec3& up
    ) {
        const Vec3 f =
            normalize(
                center - eye
            );

        const Vec3 s =
            normalize(
                cross(
                    f,
                    up
                )
            );

        const Vec3 u =
            cross(
                s,
                f
            );

        const GLfloat matrix[16] = {
            s.x, u.x, -f.x, 0.0f,
            s.y, u.y, -f.y, 0.0f,
            s.z, u.z, -f.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        glMultMatrixf(
            matrix
        );

        glTranslatef(
            -eye.x,
            -eye.y,
            -eye.z
        );
    }

    void drawBox(
        float x,
        float y,
        float z,
        float sx,
        float sy,
        float sz,
        Color color
    ) {
        const float x1 =
            x - sx * 0.5f;

        const float x2 =
            x + sx * 0.5f;

        const float y1 =
            y - sy * 0.5f;

        const float y2 =
            y + sy * 0.5f;

        const float z1 =
            z - sz * 0.5f;

        const float z2 =
            z + sz * 0.5f;

        setColor(
            color
        );

        glBegin(
            GL_QUADS
        );

        glNormal3f(
            0.0f,
            0.0f,
            1.0f
        );

        glVertex3f(
            x1,
            y1,
            z2
        );

        glVertex3f(
            x2,
            y1,
            z2
        );

        glVertex3f(
            x2,
            y2,
            z2
        );

        glVertex3f(
            x1,
            y2,
            z2
        );

        glNormal3f(
            0.0f,
            0.0f,
            -1.0f
        );

        glVertex3f(
            x2,
            y1,
            z1
        );

        glVertex3f(
            x1,
            y1,
            z1
        );

        glVertex3f(
            x1,
            y2,
            z1
        );

        glVertex3f(
            x2,
            y2,
            z1
        );

        glNormal3f(
            0.0f,
            1.0f,
            0.0f
        );

        glVertex3f(
            x1,
            y2,
            z2
        );

        glVertex3f(
            x2,
            y2,
            z2
        );

        glVertex3f(
            x2,
            y2,
            z1
        );

        glVertex3f(
            x1,
            y2,
            z1
        );

        glNormal3f(
            0.0f,
            -1.0f,
            0.0f
        );

        glVertex3f(
            x1,
            y1,
            z1
        );

        glVertex3f(
            x2,
            y1,
            z1
        );

        glVertex3f(
            x2,
            y1,
            z2
        );

        glVertex3f(
            x1,
            y1,
            z2
        );

        glNormal3f(
            -1.0f,
            0.0f,
            0.0f
        );

        glVertex3f(
            x1,
            y1,
            z1
        );

        glVertex3f(
            x1,
            y1,
            z2
        );

        glVertex3f(
            x1,
            y2,
            z2
        );

        glVertex3f(
            x1,
            y2,
            z1
        );

        glNormal3f(
            1.0f,
            0.0f,
            0.0f
        );

        glVertex3f(
            x2,
            y1,
            z2
        );

        glVertex3f(
            x2,
            y1,
            z1
        );

        glVertex3f(
            x2,
            y2,
            z1
        );

        glVertex3f(
            x2,
            y2,
            z2
        );

        glEnd();
    }

    void drawGroundQuad(
        float x1,
        float z1,
        float x2,
        float z2,
        Color color,
        float y = 0.0f
    ) {
        setColor(
            color
        );

        glBegin(
            GL_QUADS
        );

        glNormal3f(
            0.0f,
            1.0f,
            0.0f
        );

        glVertex3f(
            x1,
            y,
            z1
        );

        glVertex3f(
            x2,
            y,
            z1
        );

        glVertex3f(
            x2,
            y,
            z2
        );

        glVertex3f(
            x1,
            y,
            z2
        );

        glEnd();
    }

    void drawCylinder(
        float x,
        float y,
        float z,
        float radius,
        float height,
        int segments,
        Color color
    ) {
        setColor(
            color
        );

        glBegin(
            GL_QUAD_STRIP
        );

        for (
            int i = 0;
            i <= segments;
            ++i
            ) {
            const float a =
                static_cast<float>(
                    i
                    ) /
                static_cast<float>(
                    segments
                    ) *
                2.0f *
                kPi;

            const float cx =
                std::cos(
                    a
                );

            const float cz =
                std::sin(
                    a
                );

            glNormal3f(
                cx,
                0.0f,
                cz
            );

            glVertex3f(
                x + cx * radius,
                y,
                z + cz * radius
            );

            glVertex3f(
                x + cx * radius,
                y + height,
                z + cz * radius
            );
        }

        glEnd();
    }

    void drawWheel(
        float x,
        float y,
        float z,
        float radius,
        float width
    ) {
        setColor({
            0.025f,
            0.028f,
            0.032f,
            1.0f
            });

        glBegin(
            GL_QUAD_STRIP
        );

        constexpr int segments =
            16;

        for (
            int i = 0;
            i <= segments;
            ++i
            ) {
            const float a =
                static_cast<float>(
                    i
                    ) /
                static_cast<float>(
                    segments
                    ) *
                2.0f *
                kPi;

            const float cy =
                std::cos(
                    a
                );

            const float cz =
                std::sin(
                    a
                );

            glNormal3f(
                0.0f,
                cy,
                cz
            );

            glVertex3f(
                x - width * 0.5f,
                y + cy * radius,
                z + cz * radius
            );

            glVertex3f(
                x + width * 0.5f,
                y + cy * radius,
                z + cz * radius
            );
        }

        glEnd();
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

        glTranslatef(
            x,
            0.0f,
            z
        );

        glRotatef(
            yaw,
            0.0f,
            1.0f,
            0.0f
        );

        glDisable(
            GL_LIGHTING
        );

        glEnable(
            GL_BLEND
        );

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE_MINUS_SRC_ALPHA
        );

        drawGroundQuad(
            -width * 0.72f,
            -length * 0.63f,
            width * 0.72f,
            length * 0.63f,
            {
                0.0f,
                0.0f,
                0.0f,
                0.28f
            },
            0.025f
        );

        glDisable(
            GL_BLEND
        );

        glEnable(
            GL_LIGHTING
        );

        const float bodyHeight =
            height * 0.48f;

        drawBox(
            0.0f,
            bodyHeight * 0.55f,
            0.0f,
            width,
            bodyHeight,
            length,
            body
        );

        if (
            type ==
            TrafficType::Truck
            ) {
            drawBox(
                0.0f,
                1.55f,
                length * 0.12f,
                width * 0.94f,
                height * 0.78f,
                length * 0.55f,
                mixColor(
                    body,
                    {
                        0.95f,
                        0.95f,
                        0.95f,
                        1.0f
                    },
                    0.18f
                )
            );

            drawBox(
                0.0f,
                1.2f,
                -length * 0.31f,
                width * 0.82f,
                1.35f,
                length * 0.28f,
                mixColor(
                    body,
                    {
                        0.1f,
                        0.1f,
                        0.12f,
                        1.0f
                    },
                    0.18f
                )
            );
        }
        else {
            const float cabinLength =
                type ==
                TrafficType::Sport
                ? length * 0.38f
                : length * 0.44f;

            const float cabinHeight =
                type ==
                TrafficType::Van
                ? height * 0.63f
                : height * 0.52f;

            drawBox(
                0.0f,
                bodyHeight +
                cabinHeight * 0.45f,
                -length * 0.03f,
                width * 0.74f,
                cabinHeight,
                cabinLength,
                mixColor(
                    body,
                    {
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f
                    },
                    0.18f
                )
            );

            drawBox(
                0.0f,
                bodyHeight +
                cabinHeight * 0.57f,
                -length * 0.13f,
                width * 0.58f,
                cabinHeight * 0.44f,
                cabinLength * 0.55f,
                {
                    0.05f,
                    0.13f,
                    0.19f,
                    1.0f
                }
            );
        }

        const float wheelY =
            0.27f;

        const float wheelZ1 =
            -length * 0.28f;

        const float wheelZ2 =
            length * 0.28f;

        drawWheel(
            -width * 0.53f,
            wheelY,
            wheelZ1,
            0.31f,
            0.20f
        );

        drawWheel(
            width * 0.53f,
            wheelY,
            wheelZ1,
            0.31f,
            0.20f
        );

        drawWheel(
            -width * 0.53f,
            wheelY,
            wheelZ2,
            0.31f,
            0.20f
        );

        drawWheel(
            width * 0.53f,
            wheelY,
            wheelZ2,
            0.31f,
            0.20f
        );

        const Color frontLight =
            player
            ? Color{
                1.0f,
                0.95f,
                0.72f,
                1.0f
        }
            : Color{
                1.0f,
                0.90f,
                0.58f,
                1.0f
        };

        const Color rearLight = {
            1.0f,
            0.04f,
            0.03f,
            1.0f
        };

        drawBox(
            -width * 0.27f,
            0.42f,
            -length * 0.505f,
            width * 0.21f,
            0.12f,
            0.07f,
            frontLight
        );

        drawBox(
            width * 0.27f,
            0.42f,
            -length * 0.505f,
            width * 0.21f,
            0.12f,
            0.07f,
            frontLight
        );

        drawBox(
            -width * 0.27f,
            0.42f,
            length * 0.505f,
            width * 0.18f,
            0.12f,
            0.07f,
            rearLight
        );

        drawBox(
            width * 0.27f,
            0.42f,
            length * 0.505f,
            width * 0.18f,
            0.12f,
            0.07f,
            rearLight
        );

        if (
            player
            ) {
            drawBox(
                0.0f,
                bodyHeight + 0.12f,
                length * 0.37f,
                width * 0.72f,
                0.08f,
                length * 0.08f,
                {
                    0.04f,
                    0.04f,
                    0.05f,
                    1.0f
                }
            );
        }

        glPopMatrix();
    }

    void drawCoin(
        float x,
        float z,
        float phase
    ) {
        glPushMatrix();

        glTranslatef(
            x,
            1.0f +
            std::sin(
                phase
            ) *
            0.18f,
            z
        );

        glRotatef(
            phase * 65.0f,
            0.0f,
            1.0f,
            0.0f
        );

        glDisable(
            GL_LIGHTING
        );

        setColor({
            1.0f,
            0.78f,
            0.12f,
            1.0f
            });

        constexpr int segments =
            20;

        constexpr float radius =
            0.46f;

        glBegin(
            GL_TRIANGLE_FAN
        );

        glVertex3f(
            0.0f,
            0.0f,
            0.0f
        );

        for (
            int i = 0;
            i <= segments;
            ++i
            ) {
            const float a =
                static_cast<float>(
                    i
                    ) /
                static_cast<float>(
                    segments
                    ) *
                2.0f *
                kPi;

            glVertex3f(
                std::cos(a) * radius,
                std::sin(a) * radius,
                0.0f
            );
        }

        glEnd();

        setColor({
            1.0f,
            0.94f,
            0.36f,
            1.0f
            });

        glBegin(
            GL_LINE_LOOP
        );

        for (
            int i = 0;
            i < segments;
            ++i
            ) {
            const float a =
                static_cast<float>(
                    i
                    ) /
                static_cast<float>(
                    segments
                    ) *
                2.0f *
                kPi;

            glVertex3f(
                std::cos(a) *
                radius *
                0.72f,
                std::sin(a) *
                radius *
                0.72f,
                0.01f
            );
        }

        glEnd();

        glEnable(
            GL_LIGHTING
        );

        glPopMatrix();
    }

    void drawRoad(
        float offset,
        float worldTime
    ) {
        const float pulse =
            0.5f +
            0.5f *
            std::sin(
                worldTime *
                0.08f
            );

        const Color grassA = {
            0.028f,
            0.11f,
            0.060f,
            1.0f
        };

        const Color grassB = {
            0.022f,
            0.075f,
            0.045f,
            1.0f
        };

        const Color grass =
            mixColor(
                grassA,
                grassB,
                pulse * 0.4f
            );

        drawGroundQuad(
            -90.0f,
            kWorldNearZ,
            90.0f,
            kWorldFarZ,
            grass
        );

        drawGroundQuad(
            -kRoadWidth * 0.5f,
            kWorldNearZ,
            kRoadWidth * 0.5f,
            kWorldFarZ,
            {
                0.065f,
                0.070f,
                0.077f,
                1.0f
            },
            0.01f
        );

        drawGroundQuad(
            -kRoadWidth * 0.61f,
            kWorldNearZ,
            -kRoadWidth * 0.50f,
            kWorldFarZ,
            {
                0.92f,
                0.76f,
                0.12f,
                1.0f
            },
            0.025f
        );

        drawGroundQuad(
            kRoadWidth * 0.50f,
            kWorldNearZ,
            kRoadWidth * 0.61f,
            kWorldFarZ,
            {
                0.92f,
                0.76f,
                0.12f,
                1.0f
            },
            0.025f
        );

        for (
            int lane = 1;
            lane <= 2;
            ++lane
            ) {
            const float x =
                -kLaneWidth * 1.5f +
                kLaneWidth *
                static_cast<float>(
                    lane
                    );

            for (
                float z =
                kWorldNearZ +
                offset;
                z >
                kWorldFarZ;
                z -= 12.0f
                ) {
                drawBox(
                    x,
                    0.045f,
                    z,
                    0.13f,
                    0.045f,
                    5.0f,
                    {
                        0.87f,
                        0.88f,
                        0.84f,
                        1.0f
                    }
                );
            }
        }

        for (
            float z =
            kWorldNearZ +
            offset * 0.75f;
            z >
            kWorldFarZ;
            z -= 10.5f
            ) {
            drawBox(
                -6.35f,
                0.33f,
                z,
                0.15f,
                0.66f,
                0.15f,
                {
                    0.60f,
                    0.62f,
                    0.64f,
                    1.0f
                }
            );

            drawBox(
                6.35f,
                0.33f,
                z,
                0.15f,
                0.66f,
                0.15f,
                {
                    0.60f,
                    0.62f,
                    0.64f,
                    1.0f
                }
            );
        }
    }

    void drawEnvironment(
        float offset,
        float worldTime
    ) {
        for (
            float z =
            4.0f +
            offset * 0.55f;
            z >
            kWorldFarZ;
            z -= 26.0f
            ) {
            const float sway =
                std::sin(
                    worldTime *
                    0.6f +
                    z *
                    0.03f
                ) *
                0.05f;

            drawCylinder(
                -11.5f + sway,
                0.0f,
                z,
                0.22f,
                2.0f,
                10,
                {
                    0.28f,
                    0.17f,
                    0.07f,
                    1.0f
                }
            );

            drawCylinder(
                11.5f - sway,
                0.0f,
                z - 11.0f,
                0.22f,
                2.0f,
                10,
                {
                    0.28f,
                    0.17f,
                    0.07f,
                    1.0f
                }
            );

            drawBox(
                -11.5f + sway,
                2.45f,
                z,
                2.8f,
                2.3f,
                2.8f,
                {
                    0.035f,
                    0.28f,
                    0.10f,
                    1.0f
                }
            );

            drawBox(
                11.5f - sway,
                2.45f,
                z - 11.0f,
                2.8f,
                2.3f,
                2.8f,
                {
                    0.035f,
                    0.26f,
                    0.095f,
                    1.0f
                }
            );
        }

        for (
            float z =
            -6.0f +
            offset * 0.72f;
            z >
            kWorldFarZ;
            z -= 34.0f
            ) {
            drawBox(
                -8.0f,
                1.9f,
                z,
                0.16f,
                3.8f,
                0.16f,
                {
                    0.44f,
                    0.47f,
                    0.49f,
                    1.0f
                }
            );

            drawBox(
                -7.2f,
                3.65f,
                z,
                1.45f,
                0.14f,
                0.14f,
                {
                    0.44f,
                    0.47f,
                    0.49f,
                    1.0f
                }
            );

            drawBox(
                -6.48f,
                3.48f,
                z,
                0.32f,
                0.20f,
                0.32f,
                {
                    1.0f,
                    0.79f,
                    0.36f,
                    1.0f
                }
            );

            drawBox(
                8.0f,
                1.9f,
                z - 17.0f,
                0.16f,
                3.8f,
                0.16f,
                {
                    0.44f,
                    0.47f,
                    0.49f,
                    1.0f
                }
            );

            drawBox(
                7.2f,
                3.65f,
                z - 17.0f,
                1.45f,
                0.14f,
                0.14f,
                {
                    0.44f,
                    0.47f,
                    0.49f,
                    1.0f
                }
            );

            drawBox(
                6.48f,
                3.48f,
                z - 17.0f,
                0.32f,
                0.20f,
                0.32f,
                {
                    1.0f,
                    0.79f,
                    0.36f,
                    1.0f
                }
            );
        }

        for (
            int i = 0;
            i < 9;
            ++i
            ) {
            const float x =
                -65.0f +
                static_cast<float>(
                    i % 5
                    ) *
                31.0f;

            const float z =
                -105.0f -
                static_cast<float>(
                    i
                    ) *
                13.0f;

            const float h =
                8.0f +
                static_cast<float>(
                    (i * 7) % 5
                    ) *
                2.6f;

            drawBox(
                x,
                h * 0.5f,
                z,
                15.0f,
                h,
                8.0f,
                {
                    0.045f,
                    0.065f,
                    0.080f,
                    1.0f
                }
            );
        }
    }

    void drawOverlayRect(
        float x,
        float y,
        float w,
        float h,
        Color color
    ) {
        setColor(
            color
        );

        glBegin(
            GL_QUADS
        );

        glVertex2f(
            x,
            y
        );

        glVertex2f(
            x + w,
            y
        );

        glVertex2f(
            x + w,
            y + h
        );

        glVertex2f(
            x,
            y + h
        );

        glEnd();
    }

    const std::array<int, 10>
        kDigitMasks = {
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

    void segment(
        float x,
        float y,
        float w,
        float h,
        Color color
    ) {
        drawOverlayRect(
            x,
            y,
            w,
            h,
            color
        );
    }

    void drawDigit(
        char ch,
        float x,
        float y,
        float scale,
        Color color
    ) {
        if (
            ch < '0' ||
            ch > '9'
            ) {
            return;
        }

        const int mask =
            kDigitMasks[
                ch - '0'
            ];

        const float t =
            4.0f * scale;

        const float w =
            24.0f * scale;

        const float h =
            42.0f * scale;

        const float mid =
            y +
            (h - t) *
            0.5f;

        const float bottom =
            y +
            h -
            t;

        if (
            mask &
            0b1000000
            )
            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

        if (
            mask &
            0b0100000
            )
            segment(
                x + w - t,
                y + t,
                t,
                h * 0.5f - t,
                color
            );

        if (
            mask &
            0b0010000
            )
            segment(
                x + w - t,
                mid + t,
                t,
                h * 0.5f - t,
                color
            );

        if (
            mask &
            0b0001000
            )
            segment(
                x + t,
                bottom,
                w - 2.0f * t,
                t,
                color
            );

        if (
            mask &
            0b0000100
            )
            segment(
                x,
                mid + t,
                t,
                h * 0.5f - t,
                color
            );

        if (
            mask &
            0b0000010
            )
            segment(
                x,
                y + t,
                t,
                h * 0.5f - t,
                color
            );

        if (
            mask &
            0b0000001
            )
            segment(
                x + t,
                mid,
                w - 2.0f * t,
                t,
                color
            );
    }

    void drawLetter(
        char raw,
        float x,
        float y,
        float scale,
        Color color
    ) {
        const char ch =
            static_cast<char>(
                std::toupper(
                    static_cast<unsigned char>(
                        raw
                        )
                )
                );

        const float w =
            24.0f * scale;

        const float h =
            42.0f * scale;

        const float t =
            4.0f * scale;

        const float m =
            y +
            (h - t) *
            0.5f;

        switch (
            ch
            ) {
        case 'A':
            segment(
                x,
                y + t,
                t,
                h - t,
                color
            );

            segment(
                x + w - t,
                y + t,
                t,
                h - t,
                color
            );

            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + t,
                m,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'B':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + t,
                m,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + w - t,
                y + t,
                t,
                h * 0.5f - t,
                color
            );

            segment(
                x + w - t,
                m + t,
                t,
                h * 0.5f - t,
                color
            );

            break;

        case 'C':
            segment(
                x + t,
                y,
                w - t,
                t,
                color
            );

            segment(
                x,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - t,
                t,
                color
            );

            break;

        case 'D':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + w - t,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'E':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y,
                w - t,
                t,
                color
            );

            segment(
                x + t,
                m,
                w - t,
                t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - t,
                t,
                color
            );

            break;

        case 'G':
            segment(
                x + t,
                y,
                w - t,
                t,
                color
            );

            segment(
                x,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - t,
                t,
                color
            );

            segment(
                x + w * 0.5f,
                m,
                w * 0.5f,
                t,
                color
            );

            segment(
                x + w - t,
                m,
                t,
                h * 0.5f - t,
                color
            );

            break;

        case 'H':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                m,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'I':
            segment(
                x,
                y,
                w,
                t,
                color
            );

            segment(
                x + w * 0.5f -
                t * 0.5f,
                y,
                t,
                h,
                color
            );

            segment(
                x,
                y + h - t,
                w,
                t,
                color
            );

            break;

        case 'L':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - t,
                t,
                color
            );

            break;

        case 'M':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y + t,
                t,
                t * 4.0f,
                color
            );

            segment(
                x + w - 2.0f * t,
                y + t,
                t,
                t * 4.0f,
                color
            );

            break;

        case 'N':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            break;

        case 'O':
            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            segment(
                x + w - t,
                y + t,
                t,
                h - 2.0f * t,
                color
            );

            break;

        case 'P':
            segment(
                x,
                y,
                t,
                h,
                color
            );

            segment(
                x + t,
                y,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + w - t,
                y + t,
                t,
                h * 0.5f - t,
                color
            );

            segment(
                x + t,
                m,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'R':
            drawLetter(
                'P',
                x,
                y,
                scale,
                color
            );

            segment(
                x + w - t,
                m + t,
                t,
                h * 0.5f - t,
                color
            );

            break;

        case 'S':
            segment(
                x + t,
                y,
                w - t,
                t,
                color
            );

            segment(
                x,
                y + t,
                t,
                h * 0.5f - t,
                color
            );

            segment(
                x + t,
                m,
                w - 2.0f * t,
                t,
                color
            );

            segment(
                x + w - t,
                m + t,
                t,
                h * 0.5f - t,
                color
            );

            segment(
                x,
                y + h - t,
                w - t,
                t,
                color
            );

            break;

        case 'T':
            segment(
                x,
                y,
                w,
                t,
                color
            );

            segment(
                x + w * 0.5f -
                t * 0.5f,
                y,
                t,
                h,
                color
            );

            break;

        case 'U':
            segment(
                x,
                y,
                t,
                h - t,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h - t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'V':
            segment(
                x,
                y,
                t,
                h - t,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h - t,
                color
            );

            segment(
                x + t,
                y + h - t,
                w - 2.0f * t,
                t,
                color
            );

            break;

        case 'Y':
            segment(
                x,
                y,
                t,
                h * 0.5f,
                color
            );

            segment(
                x + w - t,
                y,
                t,
                h * 0.5f,
                color
            );

            segment(
                x + w * 0.5f -
                t * 0.5f,
                m,
                t,
                h * 0.5f,
                color
            );

            break;

        default:
            break;
        }
    }

    void drawText(
        const std::string& text,
        float x,
        float y,
        float scale,
        Color color
    ) {
        const float advance =
            31.0f *
            scale;

        float cursor =
            x;

        for (
            char ch :
        text
            ) {
            if (
                ch == ' '
                ) {
                cursor +=
                    advance;

                continue;
            }

            if (
                ch >= '0' &&
                ch <= '9'
                ) {
                drawDigit(
                    ch,
                    cursor,
                    y,
                    scale,
                    color
                );
            }
            else {
                drawLetter(
                    ch,
                    cursor,
                    y,
                    scale,
                    color
                );
            }

            cursor +=
                advance;
        }
    }

    void drawBar(
        float x,
        float y,
        float w,
        float h,
        float value,
        Color fill,
        Color bg
    ) {
        value =
            std::clamp(
                value,
                0.0f,
                1.0f
            );

        drawOverlayRect(
            x,
            y,
            w,
            h,
            bg
        );

        drawOverlayRect(
            x + 3.0f,
            y + 3.0f,
            (w - 6.0f) *
            value,
            h - 6.0f,
            fill
        );
    }

}

bool Renderer::initialize()
{
    glEnable(
        GL_DEPTH_TEST
    );

    glDepthFunc(
        GL_LEQUAL
    );

    glDisable(
        GL_CULL_FACE
    );

    glEnable(
        GL_LIGHTING
    );

    glEnable(
        GL_LIGHT0
    );

    glEnable(
        GL_COLOR_MATERIAL
    );

    glColorMaterial(
        GL_FRONT_AND_BACK,
        GL_AMBIENT_AND_DIFFUSE
    );

    glShadeModel(
        GL_SMOOTH
    );

    const GLfloat ambient[] = {
        0.28f,
        0.30f,
        0.34f,
        1.0f
    };

    const GLfloat diffuse[] = {
        0.94f,
        0.90f,
        0.78f,
        1.0f
    };

    const GLfloat specular[] = {
        0.25f,
        0.25f,
        0.22f,
        1.0f
    };

    glLightfv(
        GL_LIGHT0,
        GL_AMBIENT,
        ambient
    );

    glLightfv(
        GL_LIGHT0,
        GL_DIFFUSE,
        diffuse
    );

    glLightfv(
        GL_LIGHT0,
        GL_SPECULAR,
        specular
    );

    glEnable(
        GL_FOG
    );

    const GLfloat fogColor[] = {
        0.055f,
        0.080f,
        0.105f,
        1.0f
    };

    glFogfv(
        GL_FOG_COLOR,
        fogColor
    );

    glFogi(
        GL_FOG_MODE,
        GL_LINEAR
    );

    glFogf(
        GL_FOG_START,
        85.0f
    );

    glFogf(
        GL_FOG_END,
        205.0f
    );

    glClearColor(
        0.055f,
        0.080f,
        0.105f,
        1.0f
    );

    return true;
}

void Renderer::resize(
    int width,
    int height
)
{
    width_ =
        std::max(
            1,
            width
        );

    height_ =
        std::max(
            1,
            height
        );

    glViewport(
        0,
        0,
        width_,
        height_
    );
}

void Renderer::render(
    const Game& game
)
{
    const float night =
        0.5f +
        0.5f *
        std::sin(
            game.worldTime() *
            0.055f
        );

    const Color daySky = {
        0.18f,
        0.42f,
        0.62f,
        1.0f
    };

    const Color nightSky = {
        0.025f,
        0.045f,
        0.080f,
        1.0f
    };

    const Color sky =
        mixColor(
            daySky,
            nightSky,
            night * 0.78f
        );

    glClearColor(
        sky.r,
        sky.g,
        sky.b,
        1.0f
    );

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    renderWorld(
        game
    );

    renderHud(
        game
    );
}

void Renderer::renderWorld(
    const Game& game
)
{
    glMatrixMode(
        GL_PROJECTION
    );

    glLoadIdentity();

    setPerspective(
        60.0f,
        static_cast<float>(
            width_
            ) /
        static_cast<float>(
            height_
            ),
        0.15f,
        260.0f
    );

    glMatrixMode(
        GL_MODELVIEW
    );

    glLoadIdentity();

    const float cameraLean =
        game.playerYaw() *
        0.035f;

    lookAt(
        {
            game.playerX() *
            0.20f -
            cameraLean,
            6.3f,
            14.6f
        },
        {
            game.playerX() *
            0.16f,
            0.70f,
            -20.0f
        },
        {
            0.0f,
            1.0f,
            0.0f
        }
    );

    const GLfloat lightPosition[] = {
        -8.0f,
        18.0f,
        6.0f,
        1.0f
    };

    glLightfv(
        GL_LIGHT0,
        GL_POSITION,
        lightPosition
    );

    drawRoad(
        game.roadOffset(),
        game.worldTime()
    );

    drawEnvironment(
        game.roadOffset(),
        game.worldTime()
    );

    for (
        const auto& coin :
        game.pickups()
        ) {
        drawCoin(
            coin.x,
            coin.z,
            coin.phase
        );
    }

    for (
        const auto& vehicle :
        game.traffic()
        ) {
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
        {
            0.04f,
            0.42f,
            1.0f,
            1.0f
        },
        TrafficType::Sport,
        game.playerYaw(),
        true
    );

    if (
        game.boosting()
        ) {
        glDisable(
            GL_LIGHTING
        );

        glEnable(
            GL_BLEND
        );

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE_MINUS_SRC_ALPHA
        );

        setColor({
            0.15f,
            0.68f,
            1.0f,
            0.78f
            });

        glBegin(
            GL_TRIANGLES
        );

        glVertex3f(
            game.playerX() -
            0.34f,
            0.34f,
            game.playerZ() +
            1.95f
        );

        glVertex3f(
            game.playerX() -
            0.10f,
            0.34f,
            game.playerZ() +
            1.95f
        );

        glVertex3f(
            game.playerX() -
            0.22f,
            0.34f,
            game.playerZ() +
            3.20f
        );

        glVertex3f(
            game.playerX() +
            0.10f,
            0.34f,
            game.playerZ() +
            1.95f
        );

        glVertex3f(
            game.playerX() +
            0.34f,
            0.34f,
            game.playerZ() +
            1.95f
        );

        glVertex3f(
            game.playerX() +
            0.22f,
            0.34f,
            game.playerZ() +
            3.20f
        );

        glEnd();

        glDisable(
            GL_BLEND
        );

        glEnable(
            GL_LIGHTING
        );
    }
}

void Renderer::renderHud(
    const Game& game
)
{
    glMatrixMode(
        GL_PROJECTION
    );

    glLoadIdentity();

    glOrtho(
        0.0,
        static_cast<double>(
            width_
            ),
        static_cast<double>(
            height_
            ),
        0.0,
        -1.0,
        1.0
    );

    glMatrixMode(
        GL_MODELVIEW
    );

    glLoadIdentity();

    glDisable(
        GL_LIGHTING
    );

    glDisable(
        GL_DEPTH_TEST
    );

    glDisable(
        GL_FOG
    );

    glEnable(
        GL_BLEND
    );

    glBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA
    );

    drawOverlayRect(
        20.0f,
        18.0f,
        290.0f,
        114.0f,
        {
            0.015f,
            0.020f,
            0.027f,
            0.82f
        }
    );

    drawText(
        "SCORE",
        36.0f,
        30.0f,
        0.38f,
        {
            0.76f,
            0.84f,
            0.90f,
            1.0f
        }
    );

    drawText(
        std::to_string(
            game.score()
        ),
        36.0f,
        57.0f,
        0.62f,
        {
            1.0f,
            0.83f,
            0.22f,
            1.0f
        }
    );

    drawText(
        "COINS",
        36.0f,
        96.0f,
        0.30f,
        {
            0.95f,
            0.84f,
            0.35f,
            1.0f
        }
    );

    drawText(
        std::to_string(
            game.coins()
        ),
        142.0f,
        93.0f,
        0.36f,
        {
            1.0f,
            0.92f,
            0.30f,
            1.0f
        }
    );

    drawOverlayRect(
        static_cast<float>(
            width_
            ) -
        312.0f,
        18.0f,
        292.0f,
        114.0f,
        {
            0.015f,
            0.020f,
            0.027f,
            0.82f
        }
    );

    drawText(
        "HIGH",
        static_cast<float>(
            width_
            ) -
        292.0f,
        30.0f,
        0.38f,
        {
            0.76f,
            0.84f,
            0.90f,
            1.0f
        }
    );

    drawText(
        std::to_string(
            game.highScore()
        ),
        static_cast<float>(
            width_
            ) -
        292.0f,
        57.0f,
        0.62f,
        {
            0.42f,
            0.85f,
            1.0f,
            1.0f
        }
    );

    if (
        game.combo() >
        1
        ) {
        drawText(
            "COMBO",
            static_cast<float>(
                width_
                ) -
            292.0f,
            96.0f,
            0.30f,
            {
                1.0f,
                0.55f,
                0.20f,
                1.0f
            }
        );

        drawText(
            std::to_string(
                game.combo()
            ),
            static_cast<float>(
                width_
                ) -
            176.0f,
            93.0f,
            0.36f,
            {
                1.0f,
                0.40f,
                0.16f,
                1.0f
            }
        );
    }

    const float centerX =
        static_cast<float>(
            width_
            ) *
        0.5f;

    drawOverlayRect(
        centerX - 150.0f,
        18.0f,
        300.0f,
        114.0f,
        {
            0.015f,
            0.020f,
            0.027f,
            0.74f
        }
    );

    drawText(
        "SPEED",
        centerX - 106.0f,
        30.0f,
        0.38f,
        {
            0.76f,
            0.84f,
            0.90f,
            1.0f
        }
    );

    drawText(
        std::to_string(
            static_cast<int>(
                game.speedKmh()
                )
        ),
        centerX - 58.0f,
        57.0f,
        0.62f,
        game.boosting()
        ? Color{
            0.22f,
            0.78f,
            1.0f,
            1.0f
        }
        : Color{
            0.64f,
            1.0f,
            0.45f,
            1.0f
        }
    );

    drawText(
        "BOOST",
        centerX - 128.0f,
        96.0f,
        0.28f,
        {
            0.58f,
            0.84f,
            1.0f,
            1.0f
        }
    );

    drawBar(
        centerX - 28.0f,
        98.0f,
        150.0f,
        14.0f,
        game.boost() /
        100.0f,
        {
            0.15f,
            0.62f,
            1.0f,
            1.0f
        },
        {
            0.08f,
            0.11f,
            0.14f,
            0.95f
        }
    );

    drawText(
        "HEALTH",
        24.0f,
        static_cast<float>(
            height_
            ) -
        58.0f,
        0.30f,
        {
            0.82f,
            0.86f,
            0.90f,
            1.0f
        }
    );

    drawBar(
        132.0f,
        static_cast<float>(
            height_
            ) -
        55.0f,
        210.0f,
        18.0f,
        game.health() /
        100.0f,
        game.health() >
        45.0f
        ? Color{
            0.18f,
            0.85f,
            0.36f,
            1.0f
        }
        : Color{
            1.0f,
            0.24f,
            0.16f,
            1.0f
        },
        {
            0.08f,
            0.10f,
            0.12f,
            0.90f
        }
    );

    drawText(
        "A D LANES   W SPEED   S BRAKE   SHIFT BOOST   P PAUSE",
        24.0f,
        static_cast<float>(
            height_
            ) -
        28.0f,
        0.20f,
        {
            0.70f,
            0.76f,
            0.80f,
            0.92f
        }
    );

    if (
        game.state() ==
        GameState::Paused ||
        game.state() ==
        GameState::GameOver
        ) {
        drawOverlayRect(
            0.0f,
            0.0f,
            static_cast<float>(
                width_
                ),
            static_cast<float>(
                height_
                ),
            {
                0.0f,
                0.0f,
                0.0f,
                0.56f
            }
        );

        drawOverlayRect(
            centerX - 270.0f,
            static_cast<float>(
                height_
                ) *
            0.5f -
            120.0f,
            540.0f,
            240.0f,
            {
                0.015f,
                0.022f,
                0.030f,
                0.94f
            }
        );

        if (
            game.state() ==
            GameState::Paused
            ) {
            drawText(
                "PAUSED",
                centerX - 126.0f,
                static_cast<float>(
                    height_
                    ) *
                0.5f -
                72.0f,
                1.0f,
                {
                    1.0f,
                    0.82f,
                    0.22f,
                    1.0f
                }
            );

            drawText(
                "P RESUME",
                centerX - 126.0f,
                static_cast<float>(
                    height_
                    ) *
                0.5f +
                34.0f,
                0.60f,
                {
                    0.80f,
                    0.90f,
                    0.95f,
                    1.0f
                }
            );
        }
        else {
            drawText(
                "CRASH",
                centerX - 110.0f,
                static_cast<float>(
                    height_
                    ) *
                0.5f -
                72.0f,
                1.0f,
                {
                    1.0f,
                    0.24f,
                    0.18f,
                    1.0f
                }
            );

            drawText(
                "R RESTART",
                centerX - 154.0f,
                static_cast<float>(
                    height_
                    ) *
                0.5f +
                34.0f,
                0.60f,
                {
                    0.80f,
                    0.90f,
                    0.95f,
                    1.0f
                }
            );
        }
    }

    glDisable(
        GL_BLEND
    );

    glEnable(
        GL_FOG
    );

    glEnable(
        GL_DEPTH_TEST
    );

    glEnable(
        GL_LIGHTING
    );
}