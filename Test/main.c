#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <dlfcn.h> // Để sử dụng dlopen, dlsym, dlclose

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define APP_NAME "Vulkan X11 Example"
#define ENGINE_NAME "My Engine"

// --- Con trỏ hàm Vulkan (đã đổi tên để tránh xung đột) ---
PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;
PFN_vkCreateInstance pfn_vkCreateInstance;
PFN_vkDestroyInstance pfn_vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceProperties pfn_vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties pfn_vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkCreateDevice pfn_vkCreateDevice;
PFN_vkDestroyDevice pfn_vkDestroyDevice;
PFN_vkGetDeviceQueue pfn_vkGetDeviceQueue;
PFN_vkCreateCommandPool pfn_vkCreateCommandPool;
PFN_vkDestroyCommandPool pfn_vkDestroyCommandPool;
PFN_vkAllocateCommandBuffers pfn_vkAllocateCommandBuffers;
PFN_vkFreeCommandBuffers pfn_vkFreeCommandBuffers;
PFN_vkBeginCommandBuffer pfn_vkBeginCommandBuffer;
PFN_vkEndCommandBuffer pfn_vkEndCommandBuffer;
PFN_vkCmdPipelineBarrier pfn_vkCmdPipelineBarrier;
PFN_vkCmdClearColorImage pfn_vkCmdClearColorImage;
PFN_vkQueueSubmit pfn_vkQueueSubmit;
PFN_vkQueueWaitIdle pfn_vkQueueWaitIdle;
PFN_vkDeviceWaitIdle pfn_vkDeviceWaitIdle;
PFN_vkCreateSemaphore pfn_vkCreateSemaphore;
PFN_vkDestroySemaphore pfn_vkDestroySemaphore;
PFN_vkCreateFence pfn_vkCreateFence;
PFN_vkDestroyFence pfn_vkDestroyFence;
PFN_vkWaitForFences pfn_vkWaitForFences;
PFN_vkResetFences pfn_vkResetFences;
PFN_vkResetCommandBuffer pfn_vkResetCommandBuffer;
// Extensions
PFN_vkCreateXlibSurfaceKHR pfn_vkCreateXlibSurfaceKHR;
PFN_vkDestroySurfaceKHR pfn_vkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR pfn_vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pfn_vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR pfn_vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkCreateSwapchainKHR pfn_vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR pfn_vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR pfn_vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR pfn_vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR pfn_vkQueuePresentKHR;
PFN_vkCreateImageView pfn_vkCreateImageView;
PFN_vkDestroyImageView pfn_vkDestroyImageView;


// --- Hàm nạp Vulkan API ---
void load_vulkan_functions(void* libvulkan) {
    pfn_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
    assert(pfn_vkGetInstanceProcAddr != NULL);

    pfn_vkCreateInstance = (PFN_vkCreateInstance)pfn_vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    assert(pfn_vkCreateInstance != NULL);
}

void load_instance_functions(VkInstance instance) {
    pfn_vkDestroyInstance = (PFN_vkDestroyInstance)pfn_vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    pfn_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)pfn_vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    pfn_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    pfn_vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    pfn_vkCreateDevice = (PFN_vkCreateDevice)pfn_vkGetInstanceProcAddr(instance, "vkCreateDevice");
    pfn_vkDestroyDevice = (PFN_vkDestroyDevice)pfn_vkGetInstanceProcAddr(instance, "vkDestroyDevice");
    pfn_vkGetDeviceQueue = (PFN_vkGetDeviceQueue)pfn_vkGetInstanceProcAddr(instance, "vkGetDeviceQueue");
    pfn_vkCreateCommandPool = (PFN_vkCreateCommandPool)pfn_vkGetInstanceProcAddr(instance, "vkCreateCommandPool");
    pfn_vkDestroyCommandPool = (PFN_vkDestroyCommandPool)pfn_vkGetInstanceProcAddr(instance, "vkDestroyCommandPool");
    pfn_vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)pfn_vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers");
    pfn_vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)pfn_vkGetInstanceProcAddr(instance, "vkFreeCommandBuffers");
    pfn_vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)pfn_vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer");
    pfn_vkEndCommandBuffer = (PFN_vkEndCommandBuffer)pfn_vkGetInstanceProcAddr(instance, "vkEndCommandBuffer");
    pfn_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)pfn_vkGetInstanceProcAddr(instance, "vkCmdPipelineBarrier");
    pfn_vkCmdClearColorImage = (PFN_vkCmdClearColorImage)pfn_vkGetInstanceProcAddr(instance, "vkCmdClearColorImage");
    pfn_vkQueueSubmit = (PFN_vkQueueSubmit)pfn_vkGetInstanceProcAddr(instance, "vkQueueSubmit");
    pfn_vkQueueWaitIdle = (PFN_vkQueueWaitIdle)pfn_vkGetInstanceProcAddr(instance, "vkQueueWaitIdle");
    pfn_vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)pfn_vkGetInstanceProcAddr(instance, "vkDeviceWaitIdle");
    pfn_vkCreateSemaphore = (PFN_vkCreateSemaphore)pfn_vkGetInstanceProcAddr(instance, "vkCreateSemaphore");
    pfn_vkDestroySemaphore = (PFN_vkDestroySemaphore)pfn_vkGetInstanceProcAddr(instance, "vkDestroySemaphore");
    pfn_vkCreateFence = (PFN_vkCreateFence)pfn_vkGetInstanceProcAddr(instance, "vkCreateFence");
    pfn_vkDestroyFence = (PFN_vkDestroyFence)pfn_vkGetInstanceProcAddr(instance, "vkDestroyFence");
    pfn_vkWaitForFences = (PFN_vkWaitForFences)pfn_vkGetInstanceProcAddr(instance, "vkWaitForFences");
    pfn_vkResetFences = (PFN_vkResetFences)pfn_vkGetInstanceProcAddr(instance, "vkResetFences");
    pfn_vkResetCommandBuffer = (PFN_vkResetCommandBuffer)pfn_vkGetInstanceProcAddr(instance, "vkResetCommandBuffer");
    pfn_vkCreateImageView = (PFN_vkCreateImageView)pfn_vkGetInstanceProcAddr(instance, "vkCreateImageView");
    pfn_vkDestroyImageView = (PFN_vkDestroyImageView)pfn_vkGetInstanceProcAddr(instance, "vkDestroyImageView");
    
    // Extensions
    pfn_vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)pfn_vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR");
    pfn_vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)pfn_vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    pfn_vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    pfn_vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    pfn_vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)pfn_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    pfn_vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)pfn_vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR");
    pfn_vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)pfn_vkGetInstanceProcAddr(instance, "vkDestroySwapchainKHR");
    pfn_vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)pfn_vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR");
    pfn_vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)pfn_vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR");
    pfn_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)pfn_vkGetInstanceProcAddr(instance, "vkQueuePresentKHR");
}


int main() {
    // --- 1. Mở thư viện Vulkan động ---
    void* libvulkan = dlopen("/data/data/com.termux/files/home/IMGUI_Android_Executable/libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!libvulkan) {
        printf("Không thể mở libvulkan.so.1\n");
        return EXIT_FAILURE;
    }
    
    // --- 2. Nạp các hàm Vulkan cơ bản ---
    load_vulkan_functions(libvulkan);

    // --- 3. Thiết lập cửa sổ X11 ---
    Display* display = XOpenDisplay(NULL);
    if (display == NULL) {
        printf("Không thể kết nối đến X server\n");
        return EXIT_FAILURE;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(display, root, 10, 10, WINDOW_WIDTH, WINDOW_HEIGHT, 1,
                                     BlackPixel(display, screen), WhitePixel(display, screen));

    XStoreName(display, window, APP_NAME);
    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteMessage, 1);
    
    // --- 4. Khởi tạo Vulkan ---
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = APP_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = ENGINE_NAME;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
    instanceCreateInfo.ppEnabledExtensionNames = extensions;

    VkInstance instance;
    assert(pfn_vkCreateInstance(&instanceCreateInfo, NULL, &instance) == VK_SUCCESS);
    
    load_instance_functions(instance);

    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.dpy = display;
    surfaceCreateInfo.window = window;
    
    VkSurfaceKHR surface;
    assert(pfn_vkCreateXlibSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface) == VK_SUCCESS);

    uint32_t deviceCount = 0;
    pfn_vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    assert(deviceCount > 0);
    VkPhysicalDevice physicalDevices[deviceCount];
    pfn_vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);
    
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;

    for (uint32_t i = 0; i < deviceCount; ++i) {
        uint32_t queueFamilyCount = 0;
        pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
        VkQueueFamilyProperties queueFamilies[queueFamilyCount];
        pfn_vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, queueFamilies);

        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            VkBool32 presentSupport = VK_FALSE;
            pfn_vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[i], j, surface, &presentSupport);
            if (queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                physicalDevice = physicalDevices[i];
                graphicsQueueFamilyIndex = j;
                break;
            }
        }
        if (physicalDevice != VK_NULL_HANDLE) break;
    }
    assert(physicalDevice != VK_NULL_HANDLE);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    VkDevice device;
    assert(pfn_vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device) == VK_SUCCESS);

    VkQueue graphicsQueue;
    pfn_vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    
    VkSurfaceCapabilitiesKHR capabilities;
    pfn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    VkSurfaceFormatKHR surfaceFormat = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkExtent2D swapchainExtent = {WINDOW_WIDTH, WINDOW_HEIGHT};
    
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = surface;
    swapchainCreateInfo.minImageCount = capabilities.minImageCount > 0 ? capabilities.minImageCount : 1;
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = swapchainExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform = capabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreateInfo.clipped = VK_TRUE;
    
    VkSwapchainKHR swapchain;
    assert(pfn_vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain) == VK_SUCCESS);

    uint32_t imageCount;
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
    VkImage swapchainImages[imageCount];
    pfn_vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages);
    
    VkImageView swapchainImageViews[imageCount];
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo ivCreateInfo = {};
        ivCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCreateInfo.image = swapchainImages[i];
        ivCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivCreateInfo.format = surfaceFormat.format;
        ivCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivCreateInfo.subresourceRange.levelCount = 1;
        ivCreateInfo.subresourceRange.layerCount = 1;
        assert(pfn_vkCreateImageView(device, &ivCreateInfo, NULL, &swapchainImageViews[i]) == VK_SUCCESS);
    }

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkCommandPool commandPool;
    assert(pfn_vkCreateCommandPool(device, &poolInfo, NULL, &commandPool) == VK_SUCCESS);
    
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    assert(pfn_vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) == VK_SUCCESS);

    VkSemaphore imageAvailableSemaphore, renderFinishedSemaphore;
    VkFence inFlightFence;

    VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};

    assert(pfn_vkCreateSemaphore(device, &semaphoreInfo, NULL, &imageAvailableSemaphore) == VK_SUCCESS);
    assert(pfn_vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphore) == VK_SUCCESS);
    assert(pfn_vkCreateFence(device, &fenceInfo, NULL, &inFlightFence) == VK_SUCCESS);


    // --- 5. Vòng lặp chính và vẽ ---
    printf("Bắt đầu vẽ trong 10 giây...\n");
    time_t startTime = time(NULL);
    int running = 1;

    while (running) {
        while (XPending(display)) {
            XEvent e;
            XNextEvent(display, &e);
            if (e.type == ClientMessage) {
                if ((Atom)e.xclient.data.l[0] == wmDeleteMessage) {
                    running = 0;
                }
            }
        }
        
        if (difftime(time(NULL), startTime) >= 10.0) {
            printf("Đã hết 10 giây.\n");
            running = 0;
        }

        if (!running) continue;

        pfn_vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        pfn_vkResetFences(device, 1, &inFlightFence);

        uint32_t imageIndex;
        pfn_vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        pfn_vkResetCommandBuffer(commandBuffer, 0);
        
        VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        pfn_vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        pfn_vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        
        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.5f, 1.0f}}; // Màu xanh lam
        VkImageSubresourceRange clearRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        pfn_vkCmdClearColorImage(commandBuffer, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        pfn_vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        
        pfn_vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
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
        
        assert(pfn_vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence) == VK_SUCCESS);
        
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapchains[] = {swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;
        
        pfn_vkQueuePresentKHR(graphicsQueue, &presentInfo);
    }
    
    // --- 6. Dọn dẹp ---
    printf("Dọn dẹp tài nguyên và thoát...\n");

    pfn_vkDeviceWaitIdle(device);

    pfn_vkDestroySemaphore(device, renderFinishedSemaphore, NULL);
    pfn_vkDestroySemaphore(device, imageAvailableSemaphore, NULL);
    pfn_vkDestroyFence(device, inFlightFence, NULL);
    pfn_vkDestroyCommandPool(device, commandPool, NULL);
    for (uint32_t i = 0; i < imageCount; i++) {
        pfn_vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }
    pfn_vkDestroySwapchainKHR(device, swapchain, NULL);
    pfn_vkDestroyDevice(device, NULL);
    pfn_vkDestroySurfaceKHR(instance, surface, NULL);
    pfn_vkDestroyInstance(instance, NULL);

    XDestroyWindow(display, window);
    XCloseDisplay(display);

    dlclose(libvulkan);

    printf("Hoàn tất.\n");
    return EXIT_SUCCESS;
}


