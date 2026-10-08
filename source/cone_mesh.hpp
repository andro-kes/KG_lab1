#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace application {

struct ConeVertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
};

struct ConeMesh {
    std::vector<ConeVertex> vertices;
    std::vector<uint32_t> indices;
};

inline constexpr uint32_t cone_segments = 64;
inline constexpr float cone_radius = 1.0f;
inline constexpr float cone_height = 2.0f;

inline std::array<float, 3> vertexColor(const std::array<float, 3>& position) {
    const float red = 0.5f + position[0] / (2.0f * cone_radius);
    const float green = 0.5f + position[1] / cone_height;
    const float blue = 0.5f + position[2] / (2.0f * cone_radius);

    return {std::clamp(red, 0.0f, 1.0f),
            std::clamp(green, 0.0f, 1.0f),
            std::clamp(blue, 0.0f, 1.0f)};
}

inline ConeMesh makeConeMesh() {
    ConeMesh mesh;
    mesh.vertices.reserve(cone_segments + 2);
    mesh.indices.reserve(6 * cone_segments);

    // 1. Верхушка получает индекс 0.
    ConeVertex top{};
    top.position = {0.0f, cone_height / 2.0f, 0.0f};
    mesh.vertices.push_back(top);

    // 2. Точки окружности основания получают индексы от 1 до 64.
    const float pi = std::numbers::pi_v<float>;
    for (uint32_t i = 0; i < cone_segments; ++i) {
        const float angle = 2.0f * pi * float(i) / float(cone_segments);
        const float x = cone_radius * std::cos(angle);
        const float y = -cone_height / 2.0f;
        const float z = cone_radius * std::sin(angle);

        ConeVertex vertex{};
        vertex.position = {x, y, z};
        mesh.vertices.push_back(vertex);
    }

    // 3. Сохраняем индекс центра основания, затем добавляем его вершину.
    const uint32_t base_center = uint32_t(mesh.vertices.size());
    ConeVertex center{};
    center.position = {0.0f, -cone_height / 2.0f, 0.0f};
    mesh.vertices.push_back(center);

    // 4. Цвет каждой вершины зависит от её координат.
    for (ConeVertex& vertex : mesh.vertices) {
        vertex.color = vertexColor(vertex.position);
    }

    // 5. На каждый участок окружности добавляем два треугольника.
    for (uint32_t i = 0; i < cone_segments; ++i) {
        const uint32_t current = 1 + i;
        const uint32_t next = 1 + (i + 1) % cone_segments;

        mesh.indices.insert(mesh.indices.end(), {0, next, current});           // боковой
        mesh.indices.insert(mesh.indices.end(), {base_center, current, next}); // основание
    }
    return mesh;
}

} // namespace application
