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

#include "physics-scene.h"

namespace {

// glslc generates SPIR-V bytecode at build time, not on the phone.
static const uint32_t kPhysicsVertexSpirv[] =
#include "physics.vert.inc"
;

static const uint32_t kPhysicsFragmentSpirv[] =
#include "physics.frag.inc"
;

thread_local std::string last_error;

struct Renderer {
    explicit Renderer(ANativeWindow *native_window) : window(native_window) {}
    virtual ~Renderer() = default;
    virtual bool initialize(std::string &error) = 0;
    virtual bool draw(float elapsed_seconds, const lgp::PhysicsScene &scene,
                      std::string &error) = 0;
    virtual std::string describe() const = 0;
    ANativeWindow *window;
};

struct GlesRenderer final : Renderer {
    using Renderer::Renderer;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint shader_program = 0;
    GLint aspect_uniform = -1;
    GLint transform_uniform = -1;
    GLint material_uniform = -1;
    std::string gpu;

    ~GlesRenderer() override {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT) {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
            }
            if (shader_program) glDeleteProgram(shader_program);
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
            EGL_DEPTH_SIZE, 24,
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
        // Both renderers receive identical positions from the CPU simulation.
        // Objects are real 3D spheres, built procedurally on the GPU.
        static const char *vertex_source = R"(#version 300 es
            precision highp float;
            uniform float aspectRatio;
            uniform vec4 transform; // center.xyz and sphere radius
            uniform vec4 material;  // RGB and object kind (0/1/2)

            out vec3 worldNormal;
            out vec3 baseColor;
            out vec3 worldPosition;
            flat out int objectKind;

            void main() {
                int kind = int(material.w + 0.5);
                vec3 normal;
                vec3 world;

                if (kind == 1) {
                    const vec2 corners[6] = vec2[6](
                        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
                    );
                    vec2 uv = corners[gl_VertexID];
                    world = vec3(uv.x * 2.25, -1.90, -5.95 + uv.y * 2.35);
                    normal = vec3(0.0, 1.0, 0.0);
                } else {
                    int cell = gl_VertexID / 6;
                    int longitudeCell = cell % 16;
                    int latitudeCell = cell / 16;
                    int corner = gl_VertexID % 6;
                    float u = float(
                        longitudeCell + (
                            (corner == 1 || corner == 2 || corner == 4) ? 1 : 0)
                    ) / 16.0;
                    float v = float(
                        latitudeCell + (
                            (corner == 2 || corner == 4 || corner == 5) ? 1 : 0)
                    ) / 12.0;
                    float longitude = u * 6.28318530718;
                    float latitude = v * 3.14159265359;
                    normal = vec3(
                        sin(latitude) * cos(longitude),
                        cos(latitude),
                        sin(latitude) * sin(longitude)
                    );
                    world = transform.xyz + normal * transform.w;
                }

                float aspect = max(aspectRatio, 0.2);
                float focal = 1.8;
                float z = -1.002002 * world.z - 0.2002002;
                gl_Position = vec4(
                    focal * world.x / aspect,
                    focal * world.y,
                    z,
                    -world.z
                );
                worldNormal = normal;
                baseColor = material.rgb;
                worldPosition = world;
                objectKind = kind;
            }
        )";
        static const char *fragment_source = R"(#version 300 es
            precision highp float;
            in vec3 worldNormal;
            in vec3 baseColor;
            in vec3 worldPosition;
            flat in int objectKind;
            out vec4 color;

            void main() {
                vec3 lightDirection = normalize(vec3(-0.55, 0.9, 0.75));
                float diffuse = max(
                    dot(normalize(worldNormal), lightDirection), 0.0
                );
                vec3 shaded = baseColor * (0.28 + 0.72 * diffuse);
                if (objectKind == 1) {
                    vec2 grid = abs(fract(worldPosition.xz * 1.35) - 0.5);
                    float line = 1.0 - smoothstep(
                        0.465, 0.495, max(grid.x, grid.y)
                    );
                    shaded = mix(vec3(0.09, 0.17, 0.23),
                                 vec3(0.14, 0.23, 0.31), line);
                } else if (objectKind == 2) {
                    shaded += vec3(0.25, 0.22, 0.06);
                }
                color = vec4(shaded, 1.0);
            }
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
        aspect_uniform = glGetUniformLocation(shader_program, "aspectRatio");
        transform_uniform = glGetUniformLocation(shader_program, "transform");
        material_uniform = glGetUniformLocation(shader_program, "material");
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClearDepthf(1.0f);
        const auto *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        gpu = std::string(renderer ? renderer : "GPU desconocida") +
              " | " + (version ? version : "OpenGL ES 3");
        return true;
    }

    bool draw(float elapsed_seconds, const lgp::PhysicsScene &scene,
              std::string &error) override {
        EGLint width = 0, height = 0;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
            !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
            error = "OpenGL ES: superficie no disponible.";
            return false;
        }
        glViewport(0, 0, width, height);
        glClearColor(0.055f, 0.105f, 0.19f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(shader_program);
        glUniform1f(aspect_uniform,
                    static_cast<float>(width) / static_cast<float>(std::max(1, height)));

        glUniform4f(transform_uniform, 0.0f, 0.0f, 0.0f, 1.0f);
        glUniform4f(material_uniform, 0.14f, 0.23f, 0.31f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6); // Floor.

        const auto &bodies = scene.get_bodies();
        for (int i = 0; i < lgp::PhysicsScene::kBodyCount; ++i) {
            const lgp::Body &body = bodies[i];
            glUniform4f(transform_uniform,
                        body.position.x, body.position.y, body.position.z, body.radius);
            glUniform4f(material_uniform,
                        body.color.x, body.color.y, body.color.z,
                        i == scene.selected_index() ? 2.0f : 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 16 * 12 * 6);
        }
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
    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
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
            if (depth_view) vkDestroyImageView(device, depth_view, nullptr);
            if (depth_image) vkDestroyImage(device, depth_image, nullptr);
            if (depth_memory) vkFreeMemory(device, depth_memory, nullptr);
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
        // Select a supported hardware depth format for hidden-surface removal.
        for (const auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM,
                                     VK_FORMAT_D24_UNORM_S8_UINT}) {
            VkFormatProperties depth_properties{};
            vkGetPhysicalDeviceFormatProperties(physical_device, candidate, &depth_properties);
            if (depth_properties.optimalTilingFeatures &
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depth_format = candidate;
                break;
            }
        }
        if (depth_format == VK_FORMAT_UNDEFINED) {
            error = "Vulkan: la GPU no ofrece un formato de profundidad.";
            return false;
        }
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
        // A real depth buffer enables perspective 3D rendering.
        VkImageCreateInfo depth_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depth_info.imageType = VK_IMAGE_TYPE_2D;
        depth_info.extent = {extent.width, extent.height, 1};
        depth_info.mipLevels = 1;
        depth_info.arrayLayers = 1;
        depth_info.format = depth_format;
        depth_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_info.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &depth_info, nullptr, &depth_image) != VK_SUCCESS) {
            error = "Vulkan: no se pudo reservar la imagen de profundidad.";
            return false;
        }
        VkMemoryRequirements depth_requirements{};
        vkGetImageMemoryRequirements(device, depth_image, &depth_requirements);
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        uint32_t memory_index = UINT32_MAX;
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            const bool supported = (depth_requirements.memoryTypeBits & (1u << i)) != 0;
            const bool local = (memory_properties.memoryTypes[i].propertyFlags &
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            if (supported && local) {
                memory_index = i;
                break;
            }
        }
        if (memory_index == UINT32_MAX) {
            error = "Vulkan: no se encontró memoria GPU para profundidad.";
            return false;
        }
        VkMemoryAllocateInfo memory_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memory_info.allocationSize = depth_requirements.size;
        memory_info.memoryTypeIndex = memory_index;
        if (vkAllocateMemory(device, &memory_info, nullptr, &depth_memory) != VK_SUCCESS ||
            vkBindImageMemory(device, depth_image, depth_memory, 0) != VK_SUCCESS) {
            error = "Vulkan: no se pudo asociar memoria a la imagen de profundidad.";
            return false;
        }
        VkImageViewCreateInfo depth_view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        depth_view_info.image = depth_image;
        depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format = depth_format;
        depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_view_info.subresourceRange.levelCount = 1;
        depth_view_info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &depth_view_info, nullptr, &depth_view) != VK_SUCCESS) {
            error = "Vulkan: no se pudo crear la vista de profundidad.";
            return false;
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
        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentDescription attachments[] = {attachment, depth_attachment};
        VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass_info.attachmentCount = 2;
        pass_info.pAttachments = attachments;
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
        vertex_module_info.codeSize = sizeof(kPhysicsVertexSpirv);
        vertex_module_info.pCode = kPhysicsVertexSpirv;
        VkShaderModule vertex_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &vertex_module_info, nullptr, &vertex_module) != VK_SUCCESS) {
            error = "Vulkan: el controlador rechazó el shader de vértices SPIR-V.";
            return false;
        }

        VkShaderModuleCreateInfo fragment_module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        fragment_module_info.codeSize = sizeof(kPhysicsFragmentSpirv);
        fragment_module_info.pCode = kPhysicsFragmentSpirv;
        VkShaderModule fragment_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &fragment_module_info, nullptr, &fragment_module) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertex_module, nullptr);
            error = "Vulkan: el controlador rechazó el shader de fragmentos SPIR-V.";
            return false;
        }

        VkPushConstantRange time_range{};
        time_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        time_range.offset = 0;
        // Three vec4: camera / position and radius / material and selection.
        time_range.size = sizeof(float) * 12;
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

        VkPipelineDepthStencilStateCreateInfo depth_state{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_state.depthTestEnable = VK_TRUE;
        depth_state.depthWriteEnable = VK_TRUE;
        depth_state.depthCompareOp = VK_COMPARE_OP_LESS;

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
        graphics_info.pDepthStencilState = &depth_state;
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
            VkImageView frame_attachments[] = {view, depth_view};
            fb_info.renderPass = render_pass;
            fb_info.attachmentCount = 2;
            fb_info.pAttachments = frame_attachments;
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

    bool draw(float elapsed_seconds, const lgp::PhysicsScene &scene,
              std::string &error) override {
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
        clear.color.float32[1] = 0.105f;
        clear.color.float32[2] = 0.19f;
        clear.color.float32[3] = 1.0f;
        VkClearValue clears[2]{};
        clears[0] = clear;
        clears[1].depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[index];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 2;
        pass.pClearValues = clears;
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
        // glslc push constant layout uses three vec4 = 12 floats.
        // Background/floor and all dynamic bodies share one GPU pipeline.
        float object_state[12] = {
            elapsed_seconds,
            static_cast<float>(extent.width) /
                static_cast<float>(std::max(1u, extent.height)),
            0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
            0.14f, 0.23f, 0.31f, 1.0f
        };
        vkCmdPushConstants(commands[index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(object_state), object_state);
        vkCmdDraw(commands[index], 6, 1, 0, 0);

        const auto &bodies = scene.get_bodies();
        for (int i = 0; i < lgp::PhysicsScene::kBodyCount; ++i) {
            const lgp::Body &body = bodies[i];
            object_state[4] = body.position.x;
            object_state[5] = body.position.y;
            object_state[6] = body.position.z;
            object_state[7] = body.radius;
            object_state[8] = body.color.x;
            object_state[9] = body.color.y;
            object_state[10] = body.color.z;
            object_state[11] = i == scene.selected_index() ? 2.0f : 0.0f;
            vkCmdPushConstants(commands[index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(object_state), object_state);
            vkCmdDraw(commands[index], 16 * 12 * 6, 1, 0, 0);
        }

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
    lgp::PhysicsScene scene;
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
    const float aspect = static_cast<float>(
        std::max(1, ANativeWindow_getWidth(session->window))) /
        static_cast<float>(std::max(1, ANativeWindow_getHeight(session->window)));
    session->scene.set_aspect(aspect);
    session->scene.update(seconds);
    return session->renderer->draw(seconds, session->scene, last_error)
        ? JNI_TRUE : JNI_FALSE;
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

// All JNI calls below are dispatched from the same render thread.
// No physics object is read while another thread mutates it.
extern "C" JNIEXPORT void JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_touch(
    JNIEnv *, jobject, jlong handle, jint action,
    jfloat normalized_x, jfloat normalized_y,
    jfloat velocity_x, jfloat velocity_y) {
    if (!handle) return;
    auto *session = reinterpret_cast<Session *>(handle);
    lgp::PhysicsScene &scene = session->scene;
    const float aspect = static_cast<float>(
        std::max(1, ANativeWindow_getWidth(session->window))) /
        static_cast<float>(std::max(1, ANativeWindow_getHeight(session->window)));
    scene.set_aspect(aspect);
    switch (action) {
        case 0: scene.touch_down(normalized_x, normalized_y, aspect); break;
        case 1: scene.drag_to(normalized_x, normalized_y, aspect); break;
        case 2:
            scene.drag_to(normalized_x, normalized_y, aspect);
            scene.release(velocity_x, velocity_y);
            break;
        case 3: scene.cancel(); break;
        case 4: scene.reset(); break;
        default: break;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_stats(
    JNIEnv *env, jobject, jlong handle) {
    if (!handle) return env->NewStringUTF("Escena no disponible");
    const auto *session = reinterpret_cast<const Session *>(handle);
    const auto &scene = session->scene;
    std::ostringstream message;
    message.setf(std::ios::fixed);
    message.precision(1);
    message << lgp::PhysicsScene::kBodyCount
            << " esferas | gravedad 9.81 m/s2 | impacto "
            << scene.recent_impact() << " m/s";
    if (scene.selected_index() >= 0) {
        message << " | objeto " << (scene.selected_index() + 1) << " seleccionado";
    }
    return env->NewStringUTF(message.str().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_learngraphicsprogramming_app_graphics_GraphicsNative_destroy(
    JNIEnv *, jobject, jlong handle) {
    delete reinterpret_cast<Session *>(handle);
}
