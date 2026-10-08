#pragma once

#include "cone_mesh.hpp"
#include "graphics_internal.hpp"
#include <glm/glm.hpp>

namespace graphics {

// Создать ресурсы конуса, нарисовать кадр, освободить ресурсы.
bool initializeCone(const application::ConeMesh& mesh);
bool renderCone(const internal::FrameData& frame, const glm::mat4& mvp, const glm::vec3& color);
void shutdownCone();

} // namespace graphics
