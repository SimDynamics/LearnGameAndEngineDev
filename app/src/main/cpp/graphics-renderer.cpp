#include <jni.h>

#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// glslc generates SPIR-V bytecode at build time, not on the phone.
static const uint32_t kTriangleVertexSpirv[] =
#include "triangle.vert.inc"
;

static const uint32_t kTriangleFragmentSpirv[] =
#include "triangle.frag.inc"
;

thread_local std::string last_error;

struct Renderer {
    explicit Renderer(ANativeWindow *native_window) : window(native_window) {}
    virtual ~Renderer() = default;
    virtual bool initialize(std::string &error) = 0;
    virtual bool draw(float elapsed_seconds, std::string &error) = 0;
    virtual std::string describe() const = 0;
    ANativeWindow *window;
};

struct GlesRenderer final : Renderer {
    using Renderer::Renderer;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint shader_program = 0;
    GLuint vertices = 0;
    GLint time_uniform = -1;
    std::string gpu;

    ~GlesRenderer() override {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT) {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
            }
            if (shader_program) glDeleteProgram(shader_program);
            if (vertices) glDeleteBuffers(1, &vertices);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
    }

    static GLuint compile_shader(GLenum type, const char *source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint status = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool initialize(std::string &error) override {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
            error = "EGL: no se pudo inicializar el controlador OpenGL ES.";
            return false;
        }
        constexpr EGLint attributes[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!eglChooseConfig(display, attributes, &config, 1, &count) || count < 1) {
            error = "EGL: OpenGL ES 3 no disponible.";
            return false;
        }
        constexpr EGLint context_attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (context == EGL_NO_CONTEXT) {
            error = "EGL: no se pudo crear el contexto OpenGL ES 3.";
            return false;
        }
        surface = eglCreateWindowSurface(display, config, window, nullptr);
        if (surface == EGL_NO_SURFACE ||
            eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            error = "EGL: no se pudo usar la superficie Android.";
            return false;
        }
        static const char *vertex_source = R"(#version 300 es
            layout(location = 0) in vec2 position;
            uniform float elapsed;
            void main() {
                float angle = elapsed * 0.8;
                mat2 rotation = mat2(cos(angle), sin(angle),
                                     -sin(angle), cos(angle));
                gl_Position = vec4(rotation * position, 0.0, 1.0);
            }
        )";
        static const char *fragment_source = R"(#version 300 es
            precision mediump float;
            out vec4 color;
            void main() { color = vec4(0.98, 0.58, 0.16, 1.0); }
        )";
        const GLuint vert = compile_shader(GL_VERTEX_SHADER, vertex_source);
        const GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
        if (!vert || !frag) {
            if (vert) glDeleteShader(vert);
            if (frag) glDeleteShader(frag);
            error = "OpenGL ES: fallo al compilar los shaders.";
            return false;
        }
        shader_program = glCreateProgram();
        glAttachShader(shader_program, vert);
        glAttachShader(shader_program, frag);
        glLinkProgram(shader_program);
        glDeleteShader(vert);
        glDeleteShader(frag);
        GLint linked = GL_FALSE;
        glGetProgramiv(shader_program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            error = "OpenGL ES: fallo al enlazar el programa de shaders.";
            return false;
        }
        const GLfloat points[] = {0.0f, 0.7f, -0.7f, -0.5f, 0.7f, -0.5f};
        glGenBuffers(1, &vertices);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);
        time_uniform = glGetUniformLocation(shader_program, "elapsed");
        const auto *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        gpu = std::string(renderer ? renderer : "GPU desconocida") +
              " | " + (version ? version : "OpenGL ES 3");
        return true;
    }

    bool draw(float elapsed_seconds, std::string &error) override {
        EGLint width = 0, height = 0;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
            !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
            error = "OpenGL ES: superficie no disponible.";
            return false;
        }
        glViewport(0, 0, width, height);
        glClearColor(0.045f, 0.075f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shader_program);
        glUniform1f(time_uniform, elapsed_seconds);
        glBindBuffer(GL_ARRAY_BUFFER, vertices);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (eglSwapBuffers(display, surface) != EGL_TRUE) {
            error = "OpenGL ES: falló eglSwapBuffers.";
            return false;
        }
        return true;
    }

    std::string describe() const override { return "OpenGL ES | " + gpu; }
};

struct VulkanRenderer final : Renderer {
    using Renderer::Renderer;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore finished = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commands;
    std::string gpu;

    ~VulkanRenderer() override {
        if (device) {
            vkDeviceWaitIdle(device);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (finished) vkDestroySemaphore(device, finished, nullptr);
            if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            for (auto framebuffer : framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
            for (auto view : views) vkDestroyImageView(device, view, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    bool initialize(std::string &error) override {
        const char *extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
        };
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "LearnGraphicsProgramming";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = 2;
        create.ppEnabledExtensionNames = extensions;
        if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear VkInstance.";
            return false;
        }
        VkAndroidSurfaceCreateInfoKHR surface_info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        surface_info.window = window;
        if (vkCreateAndroidSurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear VkSurfaceKHR.";
            return false;
        }
        uint32_t device_count = 0;
        if (vkEnumeratePhysicalDevices(instance, &device_count, nullptr) != VK_SUCCESS ||
            device_count == 0) {
            error = "Vulkan: no se encontró una GPU compatible.";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
        for (auto candidate : devices) {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (uint32_t i = 0; i < count; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical_device = candidate;
                    queue_family = i;
                    break;
                }
            }
            if (physical_device) break;
        }
        if (!physical_device) {
            error = "Vulkan: no existe una cola compatible con gráficos y presentación.";
            return false;
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = device_extensions;
        if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear VkDevice con VK_KHR_swapchain.";
            return false;
        }
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        gpu = properties.deviceName;

        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities)
            != VK_SUCCESS) {
            error = "Vulkan: error al consultar la superficie.";
            return false;
        }
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            error = "Vulkan: la superficie no admite adjuntos de color.";
            return false;
        }
        uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr)
                != VK_SUCCESS || count == 0) {
            error = "Vulkan: no hay formatos de superficie.";
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, formats.data());
        VkSurfaceFormatKHR selected = formats[0];
        for (const auto &item : formats) {
            if (item.format == VK_FORMAT_R8G8B8A8_UNORM &&
                item.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                selected = item;
                break;
            }
        }
        if (selected.format == VK_FORMAT_UNDEFINED) {
            selected.format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        format = selected.format;
        if (capabilities.currentExtent.width != UINT32_MAX) {
            extent = capabilities.currentExtent;
        } else {
            extent.width = std::clamp(
                static_cast<uint32_t>(std::max(1, ANativeWindow_getWidth(window))),
                capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(
                static_cast<uint32_t>(std::max(1, ANativeWindow_getHeight(window))),
                capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & alpha)) {
            for (const auto choice : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                                      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
                if (capabilities.supportedCompositeAlpha & choice) {
                    alpha = choice;
                    break;
                }
            }
        }
        VkSwapchainCreateInfoKHR swapchain_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swapchain_info.surface = surface;
        swapchain_info.minImageCount = image_count;
        swapchain_info.imageFormat = selected.format;
        swapchain_info.imageColorSpace = selected.colorSpace;
        swapchain_info.imageExtent = extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.preTransform = capabilities.currentTransform;
        swapchain_info.compositeAlpha = alpha;
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain) != VK_SUCCESS) {
            error = "Vulkan: error al crear la cadena de presentación.";
            return false;
        }
        count = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        images.resize(count);
        vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
        views.reserve(count);
        for (auto image : images) {
            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.layerCount = 1;
            VkImageView view = VK_NULL_HANDLE;
            if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
                error = "Vulkan: no se pudo crear VkImageView.";
                return false;
            }
            views.push_back(view);
        }
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = &attachment;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 1;
        pass_info.pDependencies = &dependency;
        if (vkCreateRenderPass(device, &pass_info, nullptr, &render_pass) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear VkRenderPass.";
            return false;
        }
        // Real Vulkan graphics pipeline: vertex + fragment shaders.
        // gl_VertexIndex provides the three vertices; a push constant animates
        // their rotation without allocating a new buffer on every frame.
        VkShaderModuleCreateInfo vertex_module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        vertex_module_info.codeSize = sizeof(kTriangleVertexSpirv);
        vertex_module_info.pCode = kTriangleVertexSpirv;
        VkShaderModule vertex_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &vertex_module_info, nullptr, &vertex_module) != VK_SUCCESS) {
            error = "Vulkan: el controlador rechazó el shader de vértices SPIR-V.";
            return false;
        }

        VkShaderModuleCreateInfo fragment_module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        fragment_module_info.codeSize = sizeof(kTriangleFragmentSpirv);
        fragment_module_info.pCode = kTriangleFragmentSpirv;
        VkShaderModule fragment_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &fragment_module_info, nullptr, &fragment_module) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertex_module, nullptr);
            error = "Vulkan: el controlador rechazó el shader de fragmentos SPIR-V.";
            return false;
        }

        VkPushConstantRange time_range{};
        time_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        time_range.offset = 0;
        time_range.size = sizeof(float);
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &time_range;
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, fragment_module, nullptr);
            vkDestroyShaderModule(device, vertex_module, nullptr);
            error = "Vulkan: no se pudo crear el layout de constantes de animación.";
            return false;
        }

        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}
        };
        shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shader_stages[0].module = vertex_module;
        shader_stages[0].pName = "main";
        shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shader_stages[1].module = fragment_module;
        shader_stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo input_assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterization.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState attachment_blend{};
        attachment_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &attachment_blend;

        VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamic_info{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic_info.dynamicStateCount = 2;
        dynamic_info.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo graphics_info{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        graphics_info.stageCount = 2;
        graphics_info.pStages = shader_stages;
        graphics_info.pVertexInputState = &vertex_input;
        graphics_info.pInputAssemblyState = &input_assembly;
        graphics_info.pViewportState = &viewport_state;
        graphics_info.pRasterizationState = &rasterization;
        graphics_info.pMultisampleState = &multisampling;
        graphics_info.pColorBlendState = &blending;
        graphics_info.pDynamicState = &dynamic_info;
        graphics_info.layout = pipeline_layout;
        graphics_info.renderPass = render_pass;
        graphics_info.subpass = 0;
        const VkResult pipeline_result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &graphics_info, nullptr, &pipeline);
        vkDestroyShaderModule(device, fragment_module, nullptr);
        vkDestroyShaderModule(device, vertex_module, nullptr);
        if (pipeline_result != VK_SUCCESS) {
            error = "Vulkan: falló la creación del pipeline de triángulos.";
            return false;
        }

        framebuffers.reserve(views.size());
        for (auto view : views) {
            VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb_info.renderPass = render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = &view;
            fb_info.width = extent.width;
            fb_info.height = extent.height;
            fb_info.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffer) != VK_SUCCESS) {
                error = "Vulkan: no se pudo crear VkFramebuffer.";
                return false;
            }
            framebuffers.push_back(framebuffer);
        }
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family;
        if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear VkCommandPool.";
            return false;
        }
        commands.resize(images.size());
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_info.commandPool = command_pool;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = static_cast<uint32_t>(commands.size());
        if (vkAllocateCommandBuffers(device, &command_info, commands.data()) != VK_SUCCESS) {
            error = "Vulkan: no se pudieron crear comandos de renderizado.";
            return false;
        }
        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateSemaphore(device, &sem_info, nullptr, &acquired) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sem_info, nullptr, &finished) != VK_SUCCESS ||
            vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
            error = "Vulkan: no se pudieron crear los objetos de sincronización.";
            return false;
        }
        return true;
    }

    bool draw(float elapsed_seconds, std::string &error) override {
        if (vkWaitForFences(device, 1, &fence, VK_TRUE, 1000000000ULL) != VK_SUCCESS) {
            error = "Vulkan: tiempo de espera de GPU agotado.";
            return false;
        }
        uint32_t index = 0;
        const VkResult acquired_result =
            vkAcquireNextImageKHR(device, swapchain, 1000000000ULL, acquired, VK_NULL_HANDLE, &index);
        if (acquired_result != VK_SUCCESS && acquired_result != VK_SUBOPTIMAL_KHR) {
            error = "Vulkan: hay que recrear la superficie de presentación.";
            return false;
        }
        if (vkResetCommandBuffer(commands[index], 0) != VK_SUCCESS) {
            error = "Vulkan: falló el reinicio de comandos.";
            return false;
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(commands[index], &begin) != VK_SUCCESS) {
            error = "Vulkan: no se pudo comenzar el fotograma.";
            return false;
        }
        VkClearValue clear{};
        clear.color.float32[0] = 0.055f;
        clear.color.float32[1] = 0.11f + 0.05f * std::sin(elapsed_seconds);
        clear.color.float32[2] = 0.19f;
        clear.color.float32[3] = 1.0f;
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[index];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(commands[index], &pass, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport{
            0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f
        };
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commands[index], 0, 1, &viewport);
        vkCmdSetScissor(commands[index], 0, 1, &scissor);
        vkCmdBindPipeline(commands[index], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(commands[index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float), &elapsed_seconds);
        vkCmdDraw(commands[index], 3, 1, 0, 0);

        vkCmdEndRenderPass(commands[index]);
        if (vkEndCommandBuffer(commands[index]) != VK_SUCCESS) {
            error = "Vulkan: no se pudo terminar el comando.";
            return false;
        }
        vkResetFences(device, 1, &fence);
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands[index];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &finished;
        if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) {
            error = "Vulkan: falló el envío a la GPU.";
            return false;
        }
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &index;
        const VkResult result = vkQueuePresentKHR(queue, &present);
        // Prueba de integración: evita reutilizar semáforos mientras presentan.
        // Sustituiremos esto por sincronización por fotograma al ampliar el renderer.
        vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            error = "Vulkan: la superficie cambió; reinicia el renderizador.";
            return false;
        }
        return true;
    }

    std::string describe() const override { return "Vulkan | " + gpu; }
};

struct Session {
    explicit Session(ANativeWindow *native_window) : window(native_window) {}
    ~Session() {
        renderer.reset();
        if (window) ANativeWindow_release(window);
    }
    ANativeWindow *window;
    std::unique_ptr<Renderer> renderer;
};

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_create(
    JNIEnv *env, jobject, jobject surface, jint preferred_backend) {
    last_error.clear();
    if (!surface) {
        last_error = "No hay superficie Android.";
        return 0;
    }
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        last_error = "No se pudo obtener ANativeWindow.";
        return 0;
    }
    auto session = std::make_unique<Session>(window);
    std::string vulkan_error;
    if (preferred_backend == 0 || preferred_backend == 1) {
        auto renderer = std::make_unique<VulkanRenderer>(window);
        if (renderer->initialize(vulkan_error)) {
            session->renderer = std::move(renderer);
        }
    }
    if (!session->renderer && (preferred_backend == 0 || preferred_backend == 2)) {
        auto renderer = std::make_unique<GlesRenderer>(window);
        std::string gles_error;
        if (renderer->initialize(gles_error)) {
            session->renderer = std::move(renderer);
        } else {
            last_error = gles_error;
        }
    }
    if (!session->renderer) {
        if (!vulkan_error.empty()) {
            last_error = vulkan_error + (last_error.empty() ? "" : " | " + last_error);
        }
        if (last_error.empty()) last_error = "Motor gráfico no disponible.";
        return 0;
    }
    return reinterpret_cast<jlong>(session.release());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_draw(
    JNIEnv *, jobject, jlong handle, jfloat seconds) {
    if (!handle) return JNI_FALSE;
    auto *session = reinterpret_cast<Session *>(handle);
    last_error.clear();
    return session->renderer->draw(seconds, last_error) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_describe(
    JNIEnv *env, jobject, jlong handle) {
    const auto *session = reinterpret_cast<const Session *>(handle);
    const auto description = session ? session->renderer->describe() : "Sin motor gráfico";
    return env->NewStringUTF(description.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_lastError(
    JNIEnv *env, jobject) {
    return env->NewStringUTF(last_error.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_destroy(
    JNIEnv *, jobject, jlong handle) {
    delete reinterpret_cast<Session *>(handle);
}
