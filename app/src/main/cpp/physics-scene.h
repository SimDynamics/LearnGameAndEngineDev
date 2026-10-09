#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Deterministic fixed-step CPU physics shared by Vulkan and OpenGL ES.
// GPU rendering is intentionally independent from the collision solver.
namespace lgp {

struct Vec3 {
    float x{}, y{}, z{};
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float scalar) { return {a.x * scalar, a.y * scalar, a.z * scalar}; }
inline Vec3 operator/(Vec3 a, float scalar) { return a * (1.0f / scalar); }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float norm(Vec3 a) { return std::sqrt(dot(a, a)); }

struct Body {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 color{};
    float radius = 0.30f;
    float inverse_mass = 1.0f;
};

class PhysicsScene {
public:
    static constexpr int kBodyCount = 14;
    static constexpr float kFocal = 1.8f;
    static constexpr float kFloorY = -1.90f;
    static constexpr float kMinX = -2.25f;
    static constexpr float kMaxX = 2.25f;
    static constexpr float kMinZ = -8.3f;
    static constexpr float kMaxZ = -3.6f;
    static constexpr float kCeilingY = 3.0f;

    void reset() {
        static constexpr Vec3 palette[] = {
            {0.96f, 0.36f, 0.25f}, {0.22f, 0.65f, 0.96f},
            {0.99f, 0.73f, 0.20f}, {0.39f, 0.89f, 0.66f},
            {0.76f, 0.43f, 0.98f}, {0.96f, 0.55f, 0.72f},
            {0.22f, 0.83f, 0.86f}
        };
        for (int i = 0; i < kBodyCount; ++i) {
            Body &body = bodies[i];
            body.radius = 0.26f + 0.035f * static_cast<float>(i % 4);
            body.inverse_mass = 1.0f / (body.radius * body.radius * body.radius * 16.0f);
            body.position = {
                -1.55f + 0.77f * static_cast<float>(i % 5),
                0.30f + 0.79f * static_cast<float>(i / 5),
                -5.1f - 0.62f * static_cast<float>((i * 3) % 5)
            };
            body.velocity = {};
            body.color = palette[i % 7];
        }
        selected = -1;
        largest_recent_impact = 0.0f;
        simulated_time = 0.0f;
        accumulator = 0.0f;
    }

    PhysicsScene() { reset(); }

    void update(float elapsed_seconds) {
        if (!std::isfinite(elapsed_seconds)) return;
        if (!have_time) {
            previous_time = elapsed_seconds;
            have_time = true;
            return;
        }
        const float frame_dt = std::clamp(elapsed_seconds - previous_time, 0.0f, 0.05f);
        previous_time = elapsed_seconds;
        accumulator += frame_dt;
        constexpr float fixed_dt = 1.0f / 120.0f;
        int steps = 0;
        while (accumulator >= fixed_dt && steps < 8) {
            step(fixed_dt);
            accumulator -= fixed_dt;
            ++steps;
        }
    }

    void touch_down(float u, float v, float aspect) {
        if (!valid(u, v, aspect)) return;
        selected = -1;
        float closest_distance = 100000.0f;
        const float x = 2.0f * u - 1.0f;
        const float y = 1.0f - 2.0f * v;
        for (int i = 0; i < kBodyCount; ++i) {
            const Body &body = bodies[i];
            const float distance = -body.position.z;
            const float px = (kFocal / aspect) * body.position.x / distance;
            const float py = kFocal * body.position.y / distance;
            const float projected_radius = (kFocal / distance) * body.radius;
            const float projected_diff_x = (x - px) * aspect;
            const float projected_diff_y = y - py;
            const float d2 = projected_diff_x * projected_diff_x +
                             projected_diff_y * projected_diff_y;
            const float hit_radius = std::max(projected_radius * 1.35f, 0.11f);
            if (d2 <= hit_radius * hit_radius && distance < closest_distance) {
                selected = i;
                closest_distance = distance;
            }
        }
        if (selected >= 0) {
            bodies[selected].velocity = {};
            drag_to(u, v, aspect);
        }
    }

    void drag_to(float u, float v, float aspect) {
        if (selected < 0 || !valid(u, v, aspect)) return;
        Body &body = bodies[selected];
        const float depth = -body.position.z;
        const float half_fov_height = depth / kFocal;
        body.position.x = std::clamp((2.0f*u - 1.0f) * half_fov_height * aspect,
                                     kMinX + body.radius, kMaxX - body.radius);
        body.position.y = std::clamp((1.0f - 2.0f*v) * half_fov_height,
                                     kFloorY + body.radius, kCeilingY - body.radius);
        body.velocity = {};
    }

    void release(float velocity_u, float velocity_v) {
        if (selected < 0) return;
        Body &body = bodies[selected];
        const float depth = -body.position.z;
        // Kotlin supplies normalized surface units/second. Translate into
        // world-space velocity at the picked sphere's actual camera depth.
        body.velocity = {
            2.0f * velocity_u * depth * drag_aspect / kFocal,
            -2.0f * velocity_v * depth / kFocal,
            0.0f
        };
        const float speed = norm(body.velocity);
        if (speed > 13.0f) body.velocity = body.velocity * (13.0f / speed);
        selected = -1;
    }

    void cancel() {
        if (selected >= 0) bodies[selected].velocity = {};
        selected = -1;
    }

    void set_aspect(float aspect) {
        if (std::isfinite(aspect) && aspect > 0.1f) drag_aspect = aspect;
    }

    const std::array<Body, kBodyCount> &get_bodies() const { return bodies; }
    int selected_index() const { return selected; }
    float recent_impact() const { return largest_recent_impact; }
    float time() const { return simulated_time; }

private:
    std::array<Body, kBodyCount> bodies{};
    int selected = -1;
    float drag_aspect = 1.0f;
    float largest_recent_impact = 0.0f;
    float previous_time = 0.0f;
    float accumulator = 0.0f;
    float simulated_time = 0.0f;
    bool have_time = false;

    static bool valid(float u, float v, float aspect) {
        return std::isfinite(u) && std::isfinite(v) &&
               std::isfinite(aspect) && aspect > 0.1f &&
               u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
    }

    void walls(Body &body) {
        auto bounce = [&](float &position, float &velocity,
                          float minimum, float maximum) {
            if (position < minimum) {
                position = minimum;
                if (velocity < 0.0f) {
                    largest_recent_impact = std::max(largest_recent_impact, -velocity);
                    velocity *= -0.67f;
                }
            } else if (position > maximum) {
                position = maximum;
                if (velocity > 0.0f) {
                    largest_recent_impact = std::max(largest_recent_impact, velocity);
                    velocity *= -0.67f;
                }
            }
        };
        bounce(body.position.x, body.velocity.x,
               kMinX + body.radius, kMaxX - body.radius);
        bounce(body.position.z, body.velocity.z,
               kMinZ + body.radius, kMaxZ - body.radius);
        bounce(body.position.y, body.velocity.y,
               kFloorY + body.radius, kCeilingY - body.radius);
        if (body.position.y <= kFloorY + body.radius + 0.0001f) {
            body.velocity.x *= 0.975f;
            body.velocity.z *= 0.975f;
            if (std::abs(body.velocity.y) < 0.19f) body.velocity.y = 0.0f;
        }
    }

    void step(float dt) {
        simulated_time += dt;
        largest_recent_impact *= std::exp(-dt * 1.8f);

        for (int i = 0; i < kBodyCount; ++i) {
            if (i == selected) continue;
            Body &b = bodies[i];
            b.velocity.y -= 9.81f * dt;
            b.velocity = b.velocity * (1.0f - dt * 0.065f);
            b.position = b.position + b.velocity * dt;
            walls(b);
        }

        // Iterative contact resolution reduces persistent interpenetration.
        for (int iteration = 0; iteration < 3; ++iteration) {
            for (int i = 0; i < kBodyCount; ++i) {
                for (int j = i + 1; j < kBodyCount; ++j) {
                    Body &a = bodies[i];
                    Body &b = bodies[j];
                    const Vec3 delta = b.position - a.position;
                    const float squared = dot(delta, delta);
                    const float target = a.radius + b.radius;
                    if (squared >= target * target) continue;

                    const float distance = std::sqrt(std::max(squared, 1e-8f));
                    const Vec3 normal = squared > 1e-8f ?
                        delta / distance : Vec3{1.0f, 0.0f, 0.0f};
                    const float inv_a = i == selected ? 0.0f : a.inverse_mass;
                    const float inv_b = j == selected ? 0.0f : b.inverse_mass;
                    const float total_inv_mass = inv_a + inv_b;
                    if (total_inv_mass <= 0.0f) continue;
                    const float penetration = target - distance;
                    const Vec3 correction = normal *
                        (std::max(penetration - 0.001f, 0.0f) * 0.8f / total_inv_mass);
                    a.position = a.position - correction * inv_a;
                    b.position = b.position + correction * inv_b;

                    // Normal impulse depends on relative impact speed and mass.
                    const float relative_normal_speed = dot(b.velocity - a.velocity, normal);
                    if (relative_normal_speed < -0.03f) {
                        const float impact_speed = -relative_normal_speed;
                        largest_recent_impact = std::max(largest_recent_impact, impact_speed);
                        const float restitution = std::clamp(
                            0.38f + 0.032f * impact_speed, 0.38f, 0.78f);
                        const float magnitude = -(1.0f + restitution)
                                               * relative_normal_speed / total_inv_mass;
                        const Vec3 impulse = normal * magnitude;
                        a.velocity = a.velocity - impulse * inv_a;
                        b.velocity = b.velocity + impulse * inv_b;
                    }
                }
            }
        }
        for (int i = 0; i < kBodyCount; ++i) {
            if (i != selected) walls(bodies[i]);
        }
    }
};

}  // namespace lgp
