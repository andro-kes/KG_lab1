#include "application.hpp"
#include "animation.hpp"
#include "cone_mesh.hpp"
#include "graphics.hpp"

#include <exception>
#include <iostream>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

namespace application {
namespace {

struct Controls {
    int projection = 0;
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 color{1.0f};
    Animation animation;
};

Controls controls;
double previous_time = -1.0;

glm::mat4 makeMvp(VkExtent2D extent) {
    const AnimationPose pose = animationPose(controls.animation);
    const glm::vec3 offset(pose.position[0], pose.position[1], pose.position[2]);
    const glm::vec3 angles(pose.rotation[0], pose.rotation[1], pose.rotation[2]);
    const glm::vec3 position = controls.position + offset;
    const glm::vec3 rotation = controls.rotation + angles;

    // M: положение, поворот и масштаб конуса
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    model = glm::scale(model, controls.scale);

    // V: неподвижная камера смотрит на начало координат
    const glm::mat4 view = glm::lookAtRH(glm::vec3(3.2f, 2.2f, 4.4f), glm::vec3(0), glm::vec3(0, 1, 0));

    // P: выбранная проекция с учётом пропорций окна
    const float aspect = float(extent.width) / float(extent.height);
    glm::mat4 projection;
    if (controls.projection == 0) {
        projection = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    } else {
        constexpr float half_height = 2.4f;
        projection = glm::orthoRH_ZO(-half_height * aspect, half_height * aspect, -half_height, half_height, 0.1f, 100.0f);
    }
    // В Vulkan экранная ось Y направлена вниз
    projection[1][1] *= -1.0f;
    return projection * view * model;
}

} // namespace

bool initialize() {
    try {
        const ConeMesh mesh = makeConeMesh();
        return graphics::initializeCone(mesh);
    } catch (const std::exception& error) {
        std::cerr << "Application initialization failed: " << error.what() << '\n';
        shutdown();
        return false;
    }
}

void shutdown() {
    graphics::shutdownCone();
}

void update(double time) {
    double elapsed = 0.0;
    if (previous_time >= 0.0) {
        elapsed = time - previous_time;
    }
    previous_time = time;
    advanceAnimation(controls.animation, elapsed);

    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(355, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cone - Variant 9", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Lab 1 | Vulkan");
    ImGui::Text("Radius: 1   Height: 2   Segments: %u", cone_segments);
    ImGui::Separator();
    ImGui::Combo("Projection", &controls.projection, "Perspective\0Orthographic\0");
    ImGui::DragFloat3("Position", &controls.position.x, 0.02f, -3.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat3("Rotation", &controls.rotation.x, -180.0f, 180.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat3("Scale", &controls.scale.x, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Color", &controls.color.x);
    ImGui::TextDisabled("Vertex RGB x selected color (white = original).");
    ImGui::Separator();
    ImGui::TextUnformatted("Animation: 3D figure eight");
    if (ImGui::Button(controls.animation.playing ? "Pause" : "Play")) {
        controls.animation.playing = !controls.animation.playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset motion")) {
        controls.animation.phase = 0.0;
        controls.animation.playing = false;
    }
    ImGui::SliderFloat("Speed", &controls.animation.speed, 0.1f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Path radius", &controls.animation.radius, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Vertical range", &controls.animation.height, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Depth range", &controls.animation.depth, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Text("Motion: %s", controls.animation.playing ? "playing" : "paused");
    ImGui::Separator();
    if (ImGui::Button("Reset all")) {
        controls = {};
    }
    ImGui::TextDisabled("Rotation in degrees (X, Y, Z).");
    ImGui::End();
}

bool render(const graphics::internal::FrameData& frame) {
    const VkExtent2D size = graphics::internal::context.swapchain_extent;
    if (size.width == 0 || size.height == 0) {
        return false;
    }
    const glm::mat4 mvp = makeMvp(size);
    return graphics::renderCone(frame, mvp, controls.color);
}

} // namespace application
