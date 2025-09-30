#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>

// Headers cho Xlib và Vulkan Xlib integration
#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

// --- Định nghĩa con trỏ cho các hàm Vulkan sẽ sử dụng (với tiền tố pfn_) ---

// Global/Instance level functions
PFN_vkCreateInstance pfn_vkCreateInstance;
PFN_vkDestroyInstance pfn_vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceQueueFamilyProperties pfn_vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkCreateDevice pfn_vkCreateDevice;
PFN_vkGetDeviceProcAddr pfn_vkGetDeviceProcAddr;
// THAY ĐỔI: Chuyển từ Android Surface sang Xlib Surface
PFN_vkCreateXlibSurfaceKHR pfn_vkCreateXlibSurfaceKHR;
PFN_vkDestroySurfaceKHR pfn_vkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pfn_vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR pfn_vkGetPhysicalDeviceSurfaceSupportKHR;

// Device level functions
PFN_vkDestroyDevice pfn_vkDestroyDevice;
PFN_vkGetDeviceQueue pfn_vkGetDeviceQueue;
PFN_vkCreateSwapchainKHR pfn_vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR pfn_vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR pfn_vkGetSwapchainImagesKHR;
PFN_vkCreateImageView pfn_vkCreateImageView;
PFN_vkDestroyImageView pfn_vkDestroyImageView;
PFN_vkCreateRenderPass pfn_vkCreateRenderPass;
PFN_vkDestroyRenderPass pfn_vkDestroyRenderPass;
PFN_vkCreateFramebuffer pfn_vkCreateFramebuffer;
PFN_vkDestroyFramebuffer pfn_vkDestroyFramebuffer;
PFN_vkCreateCommandPool pfn_vkCreateCommandPool;
PFN_vkDestroyCommandPool pfn_vkDestroyCommandPool;
PFN_vkAllocateCommandBuffers pfn_vkAllocateCommandBuffers;
PFN_vkBeginCommandBuffer pfn_vkBeginCommandBuffer;
PFN_vkEndCommandBuffer pfn_vkEndCommandBuffer;
PFN_vkCmdBeginRenderPass pfn_vkCmdBeginRenderPass;
PFN_vkCmdEndRenderPass pfn_vkCmdEndRenderPass;
PFN_vkCreateSemaphore pfn_vkCreateSemaphore;
PFN_vkDestroySemaphore pfn_vkDestroySemaphore;
PFN_vkAcquireNextImageKHR pfn_vkAcquireNextImageKHR;
PFN_vkQueueSubmit pfn_vkQueueSubmit;
PFN_vkQueuePresentKHR pfn_vkQueuePresentKHR;
PFN_vkDeviceWaitIdle pfn_vkDeviceWaitIdle;

// Hàm trợ giúp
void vk_check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Lỗi: %s thất bại với mã lỗi %d\n", operation, result);
        exit(1);
    }
}

// Hàm Vulkan chính
void run_vulkan(Display* display, Window window) {
    // THAY ĐỔI: Tên thư viện trên Linux thường là libvulkan.so.1
    void* libvulkan = dlopen("/data/data/com.termux/files/home/LynxVK/libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!libvulkan) {
        libvulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL); // Thử tên khác
        if (!libvulkan) {
             fprintf(stderr, "Lỗi: Không thể mở libvulkan.so.1 hoặc libvulkan.so\n");
             return;
        }
    }

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
        fprintf(stderr, "Lỗi: Không thể tìm thấy vkGetInstanceProcAddr\n");
        dlclose(libvulkan);
        return;
    }

    pfn_vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Vulkan Xlib", .apiVersion = VK_API_VERSION_1_0 };
    // THAY ĐỔI: Yêu cầu extension cho Xlib
    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo instanceCreateInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions };
    VkInstance instance;
    vk_check(pfn_vkCreateInstance(&instanceCreateInfo, NULL, &instance), "vkCreateInstance");

    // Tải các hàm Instance-Level
    pfn_vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    pfn_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    pfn_vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    pfn_vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    pfn_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    pfn_vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    pfn_vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    // THAY ĐỔI: Tải hàm tạo surface cho Xlib
    pfn_vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");


    // THAY ĐỔI: Tạo Surface từ Display* và Window của Xlib
    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = window,
    };
    VkSurfaceKHR surface;
    vk_check(pfn_vkCreateXlibSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface), "vkCreateXlibSurfaceKHR");


    // --- Phần còn lại của logic Vulkan gần như không đổi ---

    // Chọn Physical Device
    uint32_t physicalDeviceCount = 0;
    pfn_vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
    VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
    pfn_vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
    VkPhysicalDevice physicalDevice = physicalDevices[0]; // Chọn GPU đầu tiên
    free(physicalDevices);

    // Tìm queue family hỗ trợ đồ họa và present
    uint32_t queueFamilyCount = 0;
    pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies);
    
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = VK_FALSE;
        pfn_vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (presentSupport && (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            graphicsQueueFamilyIndex = i;
            break;
        }
    }
    free(queueFamilies);

    // Tạo Logical Device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamilyIndex, .queueCount = 1, .pQueuePriorities = &queuePriority };
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueCreateInfo, .enabledExtensionCount = 1, .ppEnabledExtensionNames = deviceExtensions };
    VkDevice device;
    vk_check(pfn_vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device), "vkCreateDevice");

    // Tải các hàm Device-Level
    // (Danh sách hàm này không đổi so với phiên bản trước)
    pfn_vkDestroyDevice = (PFN_vkDestroyDevice)pfn_vkGetDeviceProcAddr(device, "vkDestroyDevice");
    pfn_vkGetDeviceQueue = (PFN_vkGetDeviceQueue)pfn_vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
    pfn_vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)pfn_vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    pfn_vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)pfn_vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    pfn_vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)pfn_vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
    pfn_vkCreateImageView = (PFN_vkCreateImageView)pfn_vkGetDeviceProcAddr(device, "vkCreateImageView");
    pfn_vkDestroyImageView = (PFN_vkDestroyImageView)pfn_vkGetDeviceProcAddr(device, "vkDestroyImageView");
    pfn_vkCreateRenderPass = (PFN_vkCreateRenderPass)pfn_vkGetDeviceProcAddr(device, "vkCreateRenderPass");
    pfn_vkDestroyRenderPass = (PFN_vkDestroyRenderPass)pfn_vkGetDeviceProcAddr(device, "vkDestroyRenderPass");
    pfn_vkCreateFramebuffer = (PFN_vkCreateFramebuffer)pfn_vkGetDeviceProcAddr(device, "vkCreateFramebuffer");
    pfn_vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)pfn_vkGetDeviceProcAddr(device, "vkDestroyFramebuffer");
    pfn_vkCreateCommandPool = (PFN_vkCreateCommandPool)pfn_vkGetDeviceProcAddr(device, "vkCreateCommandPool");
    pfn_vkDestroyCommandPool = (PFN_vkDestroyCommandPool)pfn_vkGetDeviceProcAddr(device, "vkDestroyCommandPool");
    pfn_vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)pfn_vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
    pfn_vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)pfn_vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
    pfn_vkEndCommandBuffer = (PFN_vkEndCommandBuffer)pfn_vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
    pfn_vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)pfn_vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass");
    pfn_vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)pfn_vkGetDeviceProcAddr(device, "vkCmdEndRenderPass");
    pfn_vkCreateSemaphore = (PFN_vkCreateSemaphore)pfn_vkGetDeviceProcAddr(device, "vkCreateSemaphore");
    pfn_vkDestroySemaphore = (PFN_vkDestroySemaphore)pfn_vkGetDeviceProcAddr(device, "vkDestroySemaphore");
    pfn_vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)pfn_vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
    pfn_vkQueueSubmit = (PFN_vkQueueSubmit)pfn_vkGetDeviceProcAddr(device, "vkQueueSubmit");
    pfn_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)pfn_vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
    pfn_vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)pfn_vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
    

    VkQueue graphicsQueue;
    pfn_vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    
    printf("Bắt đầu vẽ lên cửa sổ Xlib. Chương trình sẽ tắt sau 10 giây.\n");

    // Đoạn code từ đây để tạo swapchain, render pass, vẽ... là y hệt
    // ... (sao chép từ phiên bản trước) ...
    VkSurfaceCapabilitiesKHR capabilities;
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    VkSurfaceFormatKHR surfaceFormat;
    uint32_t formatCount;
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);
    VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats);
    surfaceFormat = formats[0];
    free(formats);
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = capabilities.minImageCount,
        .imageFormat = surfaceFormat.format, .imageColorSpace = surfaceFormat.colorSpace, .imageExtent = capabilities.currentExtent,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE, .oldSwapchain = VK_NULL_HANDLE,
    };
    VkSwapchainKHR swapchain;
    vk_check(pfn_vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain), "vkCreateSwapchainKHR");
    uint32_t swapchainImageCount;
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, NULL);
    VkImage* swapchainImages = (VkImage*)malloc(sizeof(VkImage) * swapchainImageCount);
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages);
    VkImageView* swapchainImageViews = (VkImageView*)malloc(sizeof(VkImageView) * swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; i++) {
        VkImageViewCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchainImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surfaceFormat.format, .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1, .subresourceRange.layerCount = 1,
        };
        vk_check(pfn_vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]), "vkCreateImageView");
    }
    VkAttachmentDescription colorAttachment = {
        .format = surfaceFormat.format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference colorAttachmentRef = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };
    VkRenderPassCreateInfo renderPassInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorAttachment, .subpassCount = 1, .pSubpasses = &subpass };
    VkRenderPass renderPass;
    vk_check(pfn_vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass), "vkCreateRenderPass");
    VkFramebuffer* swapchainFramebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer) * swapchainImageCount);
    for (size_t i = 0; i < swapchainImageCount; i++) {
        VkImageView attachments[] = { swapchainImageViews[i] };
        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = renderPass, .attachmentCount = 1,
            .pAttachments = attachments, .width = capabilities.currentExtent.width, .height = capabilities.currentExtent.height, .layers = 1,
        };
        vk_check(pfn_vkCreateFramebuffer(device, &framebufferInfo, NULL, &swapchainFramebuffers[i]), "vkCreateFramebuffer");
    }
    VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamilyIndex };
    VkCommandPool commandPool;
    vk_check(pfn_vkCreateCommandPool(device, &poolInfo, NULL, &commandPool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer commandBuffer;
    vk_check(pfn_vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkSemaphore imageAvailableSemaphore, renderFinishedSemaphore;
    VkSemaphoreCreateInfo semaphoreInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vk_check(pfn_vkCreateSemaphore(device, &semaphoreInfo, NULL, &imageAvailableSemaphore), "vkCreateSemaphore");
    vk_check(pfn_vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphore), "vkCreateSemaphore");
    uint32_t imageIndex;
    pfn_vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    pfn_vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkClearValue clearColor = {{{0.0f, 0.0f, 1.0f, 1.0f}}};
    VkRenderPassBeginInfo renderPassBeginInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = renderPass, .framebuffer = swapchainFramebuffers[imageIndex] };
    renderPassBeginInfo.renderArea.extent = capabilities.currentExtent;
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &clearColor;
    pfn_vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    pfn_vkCmdEndRenderPass(commandBuffer);
    pfn_vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    vk_check(pfn_vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
    VkPresentInfoKHR presentInfo = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = signalSemaphores, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &imageIndex };
    vk_check(pfn_vkQueuePresentKHR(graphicsQueue, &presentInfo), "vkQueuePresentKHR");

    // Chờ và dọn dẹp
    sleep(10);
    printf("Đã hết 10 giây. Bắt đầu dọn dẹp tài nguyên.\n");

    pfn_vkDeviceWaitIdle(device);
    pfn_vkDestroySemaphore(device, renderFinishedSemaphore, NULL);
    pfn_vkDestroySemaphore(device, imageAvailableSemaphore, NULL);
    pfn_vkDestroyCommandPool(device, commandPool, NULL);
    for (size_t i = 0; i < swapchainImageCount; i++) pfn_vkDestroyFramebuffer(device, swapchainFramebuffers[i], NULL);
    free(swapchainFramebuffers);
    pfn_vkDestroyRenderPass(device, renderPass, NULL);
    for (uint32_t i = 0; i < swapchainImageCount; i++) pfn_vkDestroyImageView(device, swapchainImageViews[i], NULL);
    free(swapchainImageViews);
    free(swapchainImages);
    pfn_vkDestroySwapchainKHR(device, swapchain, NULL);
    
    pfn_vkDestroyDevice(device, NULL);
    pfn_vkDestroySurfaceKHR(instance, surface, NULL);
    pfn_vkDestroyInstance(instance, NULL);

    dlclose(libvulkan);
}

// Hàm main tạo cửa sổ Xlib
int main() {
    Display* display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Không thể mở X Display\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                        10, 10, 800, 600, 1,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));

    XStoreName(display, window, "Vulkan Xlib Window");
    XMapWindow(display, window);
    XFlush(display);

    run_vulkan(display, window);

    // Dọn dẹp Xlib
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}
