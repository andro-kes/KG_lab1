#include "graphics.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Здесь находятся буферы, дескрипторы, загрузка шейдеров и команды Vulkan.
namespace graphics {
namespace {

using application::ConeVertex;

struct alignas(16) SceneUniform {
    glm::mat4 mvp{1.0f};
    glm::vec4 tint{1.0f};
};
static_assert(std::is_standard_layout_v<SceneUniform>);
static_assert(alignof(SceneUniform) == 16);
static_assert(sizeof(SceneUniform) == 80);
static_assert(offsetof(SceneUniform, mvp) == 0);
static_assert(offsetof(SceneUniform, tint) == 64);
static_assert(sizeof(glm::mat4) == 64);
static_assert(sizeof(glm::mat4::col_type) == 16); // std140 mat4 column stride.

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
};

Buffer vertex_buffer;
Buffer index_buffer;
Buffer uniform_buffer;
uint32_t index_count = 0;
VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;

bool check(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed (VkResult " << result << ")\n";
    return false;
}

std::filesystem::path shaderDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (length == 0 || length >= executable.size()) {
        throw std::runtime_error("Cannot determine the executable directory");
    }
    return std::filesystem::path(std::wstring(executable.data(), length)).parent_path() / L"shaders";
#else
    return std::filesystem::current_path() / "shaders";
#endif
}

bool loadShader(const std::filesystem::path& path, VkShaderModule& module) {
    const auto utf8 = path.u8string();
    const std::string printable(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "Cannot open shader: " << printable << ". Run build.cmd to compile shaders.\n";
        return false;
    }
    const auto bytes = input.tellg();
    if (bytes < 20 || bytes % 4 != 0 || bytes > 4 * 1024 * 1024) {
        std::cerr << "Invalid SPIR-V shader size: " << printable << '\n';
        return false;
    }
    std::vector<uint32_t> code(size_t(bytes) / sizeof(uint32_t));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(code.data()), std::streamsize(bytes)) || code[0] != 0x07230203u) {
        std::cerr << "Cannot read valid SPIR-V shader: " << printable << '\n';
        return false;
    }
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data(),
    };
    return check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module),
                 "Create shader module");
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, Buffer& buffer) {
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo allocation_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo result{};
    const auto allocator = graphics::internal::context.allocator;
    if (!check(vmaCreateBuffer(allocator, &info, &allocation_info, &buffer.handle, &buffer.allocation, &result),
               "Create mapped buffer")) {
        return false;
    }
    buffer.mapped = result.pMappedData;
    if (!buffer.mapped) {
        std::cerr << "Mapped buffer has no host address\n";
        return false;
    }
    if (data) {
        std::memcpy(buffer.mapped, data, size_t(size));
        // VMA handles atom-size alignment; this is a no-op on coherent memory.
        return check(vmaFlushAllocation(allocator, buffer.allocation, 0, size), "Flush buffer");
    }
    return true;
}

bool createDescriptors() {
    const auto device = graphics::internal::context.device;
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (!check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_layout), "Create descriptor layout")) {
        return false;
    }
    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    if (!check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool), "Create descriptor pool")) {
        return false;
    }
    const VkDescriptorSetAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_layout,
    };
    if (!check(vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set), "Allocate uniform descriptor")) {
        return false;
    }
    const VkDescriptorBufferInfo buffer_info{uniform_buffer.handle, 0, sizeof(SceneUniform)};
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer_info,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

bool createPipeline() {
    const auto& context = graphics::internal::context;
    const auto shader_directory = shaderDirectory();
    struct Shader {
        VkShaderModule handle = VK_NULL_HANDLE;
        ~Shader() {
            vkDestroyShaderModule(graphics::internal::context.device, handle, nullptr);
        }
    };
    Shader vertex;
    Shader fragment;
    if (!loadShader(shader_directory / "cone.vert.spv", vertex.handle)) {
        return false;
    }
    if (!loadShader(shader_directory / "cone.frag.spv", fragment.handle)) {
        return false;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex.handle, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment.handle, .pName = "main"},
    };
    const VkVertexInputBindingDescription vertex_binding{0, sizeof(ConeVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, color)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineColorBlendAttachmentState color_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &color_attachment,
    };
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
    };
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descriptor_layout,
    };
    bool success = check(vkCreatePipelineLayout(context.device, &layout, nullptr, &pipeline_layout), "Create pipeline layout");
    if (success) {
        const VkGraphicsPipelineCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages,
            .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
            .pViewportState = &viewport, .pRasterizationState = &rasterization,
            .pMultisampleState = &multisampling, .pDepthStencilState = &depth,
            .pColorBlendState = &blend, .pDynamicState = &dynamic,
            .layout = pipeline_layout, .renderPass = context.render_pass,
        };
        success = check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                        "Create cone graphics pipeline");
    }
    return success;
}

} // namespace

bool initializeCone(const application::ConeMesh& mesh) {
    try {
        index_count = uint32_t(mesh.indices.size());
        if (createBuffer(mesh.vertices.size() * sizeof(ConeVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         mesh.vertices.data(), vertex_buffer) &&
            createBuffer(mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         mesh.indices.data(), index_buffer) &&
            createBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, uniform_buffer) &&
            createDescriptors() && createPipeline()) {
            return true;
        }
    } catch (const std::exception& error) {
        std::cerr << "Cone resource initialization failed: " << error.what() << '\n';
    }
    shutdownCone(); 
    return false;
}

void shutdownCone() {
    const auto& context = graphics::internal::context;
    if (context.device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(context.device);
    vkDestroyPipeline(context.device, pipeline, nullptr);
    vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(context.device, descriptor_layout, nullptr);
    for (Buffer* buffer : {&uniform_buffer, &index_buffer, &vertex_buffer}) {
        if (buffer->handle != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context.allocator, buffer->handle, buffer->allocation);
        }
        *buffer = {};
    }
    pipeline = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    descriptor_pool = VK_NULL_HANDLE;
    descriptor_layout = VK_NULL_HANDLE;
    descriptor_set = VK_NULL_HANDLE;
    index_count = 0;
}

bool renderCone(const internal::FrameData& fd, const glm::mat4& mvp, const glm::vec3& color) {
    const auto& context = graphics::internal::context;
    if (fd.command_buffer == VK_NULL_HANDLE || fd.framebuffer == VK_NULL_HANDLE ||
        context.swapchain_extent.width == 0 || context.swapchain_extent.height == 0) {
        std::cerr << "Cannot render an unavailable frame\n";
        return false;
    }
   
    SceneUniform uniform;
    uniform.mvp = mvp;
    uniform.tint = glm::vec4(color, 1.0f);
    std::memcpy(uniform_buffer.mapped, &uniform, sizeof(uniform));
    if (!check(vmaFlushAllocation(context.allocator, uniform_buffer.allocation, 0, sizeof(uniform)), "Flush uniform")) {
        return false;
    }
    if (!check(vkResetCommandBuffer(fd.command_buffer, 0), "Reset cone command buffer")) {
        return false;
    }
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin cone command buffer")) {
        return false;
    }
    const VkClearValue clear[] = {
        {.color = {{0.025f, 0.035f, 0.055f, 1.0f}}},
        {.depthStencil = {1.0f, 0}},
    };
    const VkRenderPassBeginInfo render_pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = {.extent = context.swapchain_extent},
        .clearValueCount = 2, .pClearValues = clear,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &render_pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, 0.0f, float(context.swapchain_extent.width), float(context.swapchain_extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, context.swapchain_extent};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    return check(vkEndCommandBuffer(fd.command_buffer), "End cone command buffer");
}

} // namespace graphics
