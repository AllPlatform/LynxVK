#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <native_surface/extern_function.h>
#include "lynxvk.h"
#include <map>
#include <cstdint>
// --- GLOBAL VARIABLES ---

ANativeWindow* g_native_window = NULL;
static void* g_vulkan_library_handle = NULL;
static VkDevice g_vkDevice = VK_NULL_HANDLE;
static PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr_real = NULL;
static VkInstance g_last_instance = VK_NULL_HANDLE;
static PFN_vkEnumerateInstanceExtensionProperties g_vkEnumerateInstanceExtensionProperties_real = NULL;
static PFN_vkCreateInstance g_vkCreateInstance_real = NULL;
static PFN_vkGetDeviceProcAddr g_vkGetDeviceProcAddr_real = nullptr;
static std::map<VkSurfaceKHR, VkSurfaceKHR> g_surface_map;
static std::map<VkSwapchainKHR, VkSwapchainKHR> g_swapchain_map;
static PFN_vkEnumerateInstanceVersion g_vkEnumerateInstanceVersion_real = nullptr;
static PFN_vkQueueSubmit g_vkQueueSubmit_real = nullptr;
static PFN_vkQueuePresentKHR g_vkQueuePresentKHR_real = nullptr;
static PFN_vkResetFences g_vkResetFences_real = nullptr;
static PFN_vkAllocateMemory g_vkAllocateMemory_real = nullptr;
static PFN_vkFreeMemory g_vkFreeMemory_real = nullptr;
static PFN_vkCreateSwapchainKHR g_vkCreateSwapchainKHR_real = nullptr;
static PFN_vkAcquireNextImageKHR g_vkAcquireNextImageKHR_real = nullptr;

static uint64_t g_frame_counter = 0;
//record Allocated memory
static PFN_vkMapMemory g_vkMapMemory_real = nullptr;
static PFN_vkUnmapMemory g_vkUnmapMemory_real = nullptr;
static uint64_t g_total_allocated_memory = 0; // Tổng số byte đã cấp phát
static std::map<VkDeviceMemory, VkDeviceSize> g_memory_allocation_map; // Map để nhớ kích thước của mỗi lần cấp phát
const char* VulkanResultToString(VkResult result);
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    printf("--- LYNXVK DEBUG: vkNegotiateLoaderICDInterfaceVersion được gọi ---\n");
    
    const uint32_t minimum_supported_version = 5;

    if (*pSupportedVersion < minimum_supported_version) {
        *pSupportedVersion = minimum_supported_version;
    }
    
    printf("    -> Báo cáo phiên bản hỗ trợ: %u\n", *pSupportedVersion);
    // fflush(stdout) đảm bảo log được in ra ngay lập tức, rất hữu ích khi debug.
    fflush(stdout);
    return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    printf("--- LYNXVK DEBUG: vk_icdGetInstanceProcAddr cho hàm: %s ---\n", pName);
    fflush(stdout);

    if (strcmp(pName, "vkNegotiateLoaderICDInterfaceVersion") == 0) {
        return (PFN_vkVoidFunction)vkNegotiateLoaderICDInterfaceVersion;
    }

    // Chuyển tiếp tới hàm vkGetInstanceProcAddr đã được intercept của chúng ta để xử lý.
    return vkGetInstanceProcAddr(instance, pName);
}

/*
 * vkGetPhysicalDeviceQueueFamilyProperties
 * ----------------------------------------
 * This function is exported directly to solve dynamic linking errors.
 * It queries the properties of available queue families for a given physical device.
 * This is a pass-through wrapper that uses the stored global instance handle
 * to retrieve the real function pointer.
 */
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice        physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties)
{
    printf("LynxVK: Intercepted vkGetPhysicalDeviceQueueFamilyProperties (direct export).\n");

    // We must have a valid instance handle stored from vkCreateInstance
    if (g_last_instance == VK_NULL_HANDLE) {
        fprintf(stderr, "LYNXVK ERROR: Cannot get queue family properties because instance handle is NULL.\n");
        // We can't return an error code, so we might have to just return
        // and hope the app handles the count being zero.
        if (pQueueFamilyPropertyCount) {
            *pQueueFamilyPropertyCount = 0;
        }
        return;
    }

    // Get the real function pointer from the driver using the stored instance.
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_real =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceQueueFamilyProperties");

    if (!vkGetPhysicalDeviceQueueFamilyProperties_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkGetPhysicalDeviceQueueFamilyProperties function pointer.\n");
        if (pQueueFamilyPropertyCount) {
            *pQueueFamilyPropertyCount = 0;
        }
        return;
    }

    // printf("LynxVK: --> Calling real vkGetPhysicalDeviceQueueFamilyProperties.\n");

    // Call the real function with the exact same arguments.
    vkGetPhysicalDeviceQueueFamilyProperties_real(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}



/*
 * vkEnumeratePhysicalDevices
 * --------------------------
 * This function is exported directly to solve dynamic linking errors.
 * Some applications link directly to this function instead of querying its
 * pointer via vkGetInstanceProcAddr. We must provide it.
 * This is a simple pass-through wrapper.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance          instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices)
{
    printf("LynxVK: Intercepted vkEnumeratePhysicalDevices (direct export).\n");

    // Get the real function pointer from the driver using the instance.
    // This is the standard, correct way to get instance-level functions.
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices_real =
        (PFN_vkEnumeratePhysicalDevices)g_vkGetInstanceProcAddr_real(instance, "vkEnumeratePhysicalDevices");

    if (!vkEnumeratePhysicalDevices_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkEnumeratePhysicalDevices function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    printf("LynxVK: --> Calling real vkEnumeratePhysicalDevices.\n");

    // Call the real function with the exact same arguments.
    return vkEnumeratePhysicalDevices_real(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

__attribute__((constructor))
void lynxvk_initialize() {
    printf("LYNXVK_LOG: initalizing...\n");
    if (g_vulkan_library_handle) {
        return;
    }
    ExternFunction externfunction; //init function to get AnativeWindow
    g_native_window = externfunction.createNativeWindow("Test VK", 720, 1280, false); //get Layer NativeWindow 720x1280 with testVk name
    ANativeWindow_acquire(g_native_window);
    if (g_native_window == NULL) {
        printf("LYNXVK FATAL: Could not get ANativeWindow\n");
        exit(1);
    }

    const char* vulkan_path = "/system/lib64/libvulkan.so";
    g_vulkan_library_handle = dlopen(vulkan_path, RTLD_NOW | RTLD_LOCAL);

    if (!g_vulkan_library_handle) {
        fprintf(stderr, "LYNXVK FATAL: Could not open original Vulkan library at '%s'\n", vulkan_path);
        exit(1);
    }

    g_vkCreateInstance_real = (PFN_vkCreateInstance)dlsym(g_vulkan_library_handle, "vkCreateInstance");
    if(!g_vkCreateInstance_real){
	fprintf(stderr, "LYNXVK FATAL: Could not find 'vkGetInstance' in '%s'\n", vulkan_path);
	dlclose(g_vulkan_library_handle);
	g_vulkan_library_handle = NULL;
	exit(1);
    }

    g_vkGetInstanceProcAddr_real = (PFN_vkGetInstanceProcAddr)dlsym(g_vulkan_library_handle, "vkGetInstanceProcAddr");
    if (!g_vkGetInstanceProcAddr_real) {
        fprintf(stderr, "LYNXVK FATAL: Could not find 'vkGetInstanceProcAddr' in '%s'\n", vulkan_path);
        dlclose(g_vulkan_library_handle);
        g_vulkan_library_handle = NULL;
        exit(1);
    }

    // NEW: Load the real vkEnumerateInstanceExtensionProperties function using dlsym.
    // This is an entry-point function, so we can get it before creating an instance.
    g_vkEnumerateInstanceExtensionProperties_real = (PFN_vkEnumerateInstanceExtensionProperties)dlsym(g_vulkan_library_handle, "vkEnumerateInstanceExtensionProperties");
    if(!g_vkEnumerateInstanceExtensionProperties_real) {
        fprintf(stderr, "LYNXVK FATAL: Could not find 'vkEnumerateInstanceExtensionProperties' in '%s'\n", vulkan_path);
        // Cleanup and exit if this essential function is missing
        dlclose(g_vulkan_library_handle);
        g_vulkan_library_handle = NULL;
        g_vkGetInstanceProcAddr_real = NULL;
        exit(1);
    }

    printf("LynxVK: Initialized and loaded libvulkan.so successfully.\n");
    // Thêm dòng này vào cuối hàm lynxvk_initialize() của bạn
    g_vkGetDeviceProcAddr_real = (PFN_vkGetDeviceProcAddr)dlsym(g_vulkan_library_handle, "vkGetDeviceProcAddr");
    if (!g_vkGetDeviceProcAddr_real) {
        // Đây không phải là lỗi nghiêm trọng, vì ứng dụng có thể lấy nó qua vkGetInstanceProcAddr
	printf("LynxVK INFO: Không tìm thấy 'vkGetDeviceProcAddr' qua dlsym, sẽ lấy sau.\n");
    }
    // Thêm dòng này vào cuối hàm lynxvk_initialize() của bạn
    g_vkEnumerateInstanceVersion_real = (PFN_vkEnumerateInstanceVersion)dlsym(g_vulkan_library_handle, "vkEnumerateInstanceVersion");
    if (!g_vkEnumerateInstanceVersion_real) {
        printf("LynxVK INFO: Không tìm thấy 'vkEnumerateInstanceVersion' qua dlsym. Có thể đây là hệ thống Vulkan 1.0.\n");
    }

    // Thêm đoạn mã này vào CUỐI hàm lynxvk_initialize() của bạn

// Lấy con trỏ tới hàm vkQueueSubmit gốc và lưu lại để sử dụng sau này.

}


__attribute__((destructor))
void lynxvk_cleanup() {
    if (g_vulkan_library_handle) {
        dlclose(g_vulkan_library_handle);
        g_vulkan_library_handle = NULL;
        printf("LynxVK: Cleaned up and closed libvulkan.so.\n");
    }

}

/*
 * vkGetInstanceProcAddr
 * ---------------------
 * This is the master interception function. It acts as a switchboard.
 * When the application asks for the address of a Vulkan function, this
 * wrapper checks the function's name.
 * - If it's a function we want to wrap (like creating a surface), we
 * return a pointer to our own wrapper function.
 * - For all other functions, we pass the request to the real driver.
 * This is the key that makes the entire library work.
 */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName)
{
    // First, check if the function name is valid
    if (pName == NULL) {
        return NULL;
    }

    // This log is very useful for debugging to see which functions an app requests.
    printf("LynxVK: Intercepted vkGetInstanceProcAddr for function '%s'\n", pName);

    // --- INTERCEPTION LOGIC ---
    // Compare the requested function name with the ones we want to wrap.

    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        // The application is asking for the address of this function itself.
        // We must return a pointer to our own wrapper to ensure future calls are also intercepted.
        printf("LynxVK: --> Providing hooked function for 'vkGetInstanceProcAddr'.\n");
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }

    if (strcmp(pName, "vkCreateInstance") == 0) {
        printf("LynxVK: --> Providing hooked function for 'vkCreateInstance'.\n");
        return (PFN_vkVoidFunction)vkCreateInstance;
    }

    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        printf("LynxVK: --> Providing hooked function for 'vkEnumerateInstanceExtensionProperties'.\n");
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }

    if (strcmp(pName, "vkCreateXlibSurfaceKHR") == 0) {
        // This is the main target for redirection!
        printf("LynxVK: --> Providing hooked function for 'vkCreateXlibSurfaceKHR'.\n");
        return (PFN_vkVoidFunction)vkCreateXlibSurfaceKHR;
    }

    if (strcmp(pName, "vkDestroySurfaceKHR") == 0) {
        printf("LynxVK: --> Providing hooked function for 'vkDestroySurfaceKHR'.\n");
        return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    }
// Bên trong hàm vkGetInstanceProcAddr của bạn

// ... các else if khác ...

    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        printf("LynxVK: Đã chuyển hướng yêu cầu lấy hàm vkEnumerateDeviceExtensionProperties.\n");
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
	return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }

    //vk_icdGetInstanceProcAddr
    if (strcmp(pName, "vk_icdGetInstanceProcAddr") == 0) {
	printf("LynxVK: --> vk_icdGetInstanceProcAddr\n");
	return (PFN_vkVoidFunction)vk_icdGetInstanceProcAddr;
    }

    //vkCreateDevice
    if (strcmp(pName, "vkCreateDevice") == 0) {
	fprintf(stderr,"LynxVK: vkCreateDevice --> call on getInstanceProcAddr\n");
	return (PFN_vkVoidFunction)vkCreateDevice;
    }
    //vkAcquireNextImageKHR
    if (strcmp(pName, "vkAcquireNextImageKHR") == 0) {
	fprintf(stderr,"LynxVK: vkAcquireNextImageKHR --> call on getInstanceProcAddr\n");
	return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    }
  // ... các else if khác ...

    //vkGetSwapchainImagesKHR
    if (strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        fprintf(stderr,"LynxVK: vkGetSwapchainImagesKHR --> call on getInstanceProcAddr\n");
        return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    }
    //vkCreateSwapchainKHR
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        fprintf(stderr,"LynxVK: vkCreateSwapchainKHR --> call on getInstanceProcAddr\n");
        return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    }
    // --- PASS-THROUGH LOGIC ---
    // If the function is not one of the ones we're wrapping,
    // just call the real vkGetInstanceProcAddr and return its result.

    //vkGetPhysicalDeviceXlibPresentationSupportKHR
    if (strcmp(pName, "vkGetPhysicalDeviceXlibPresentationSupportKHR") == 0) {
        fprintf(stderr,"LynxVK: vkGetPhysicalDeviceXlibPresentationSupportKHR --> call on getInstanceProcAddr\n");
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceXlibPresentationSupportKHR;
    }
    //vkGetPhysicalDeviceFeatures2
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0) {
        fprintf(stderr,"LynxVK: vkGetPhysicalDeviceFeatures2 --> call on getInstanceProcAddr\n");
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2;
    }
/*
    if (strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        fprintf(stderr,"LynxVK: vkGetPhysicalDeviceFeatures2 --> call on getInstanceProcAddr\n");
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2;
    }
*/
	//check
    if (g_vkGetInstanceProcAddr_real == NULL) {
        fprintf(stderr, "LYNXVK ERROR: Real vkGetInstanceProcAddr is NULL during pass-through.\n");
        return NULL;
    }
    // printf("LynxVK: --> Passing call to real vkGetInstanceProcAddr.\n");
    return g_vkGetInstanceProcAddr_real(instance, pName);
}


/*
 * vkCreateInstance
 * ----------------
 * This function intercepts the instance creation process.
 * It filters out the unsupported VK_KHR_xlib_surface extension and replaces it
 * with the required VK_KHR_android_surface extension.
 * It also stores the created VkInstance handle for later use by other wrappers.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    printf("LynxVK: Intercepted vkCreateInstance.\n");

    bool needsFiltering = false;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], "VK_KHR_xlib_surface") == 0) {
            needsFiltering = true;
            break;
        }
    }

    const VkInstanceCreateInfo* pCurrentCreateInfo = pCreateInfo;

    VkInstanceCreateInfo modifiedCreateInfo;
    const char** newExtensions = nullptr;

    if (needsFiltering) {
        printf("LynxVK: Filtering extensions for vkCreateInstance.\n");
        uint32_t newExtensionCount = pCreateInfo->enabledExtensionCount;

        // Allocate memory for the new extension list.
        // We add 1 for VK_KHR_android_surface. VK_KHR_xlib_surface is removed,
        // so the final count should be the same, but we allocate one extra just in case.
        newExtensions = new const char*[pCreateInfo->enabledExtensionCount + 1];

        uint32_t currentExt = 0;
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], "VK_KHR_xlib_surface") != 0) {
                newExtensions[currentExt++] = pCreateInfo->ppEnabledExtensionNames[i];
            }
        }
        // Add the mandatory Android surface extension
        newExtensions[currentExt++] = "VK_KHR_android_surface";
        newExtensionCount = currentExt;

        // Now, setup the modified create info struct.
        modifiedCreateInfo = *pCreateInfo;
        modifiedCreateInfo.enabledExtensionCount = newExtensionCount;
        modifiedCreateInfo.ppEnabledExtensionNames = newExtensions;

        // Point to our modified struct. This is now safe because modifiedCreateInfo
        // exists in the outer scope.
        pCurrentCreateInfo = &modifiedCreateInfo;
    }

    // Get the real function pointer
    PFN_vkCreateInstance vkCreateInstance_real = (PFN_vkCreateInstance)g_vkGetInstanceProcAddr_real(VK_NULL_HANDLE, "vkCreateInstance");
    if (!vkCreateInstance_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkCreateInstance function pointer.\n");
        // Cleanup allocated memory before returning
        if (newExtensions) {
            delete[] newExtensions;
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    printf("LynxVK: --> Calling real vkCreateInstance.\n");
    VkResult result = vkCreateInstance_real(pCurrentCreateInfo, pAllocator, pInstance);

    if (result == VK_SUCCESS) {
        printf("LynxVK: --> Real vkCreateInstance succeeded. Storing instance handle for later use.\n");
        // This is your line 286. It is correct.
        g_last_instance = *pInstance;
        fprintf(stderr, "  [INFO] Đã lưu trữ VkInstance handle (%p) vào biến toàn cục.\n", (void*)g_last_instance);
        // Bây giờ chúng ta đã có một g_last_instance hợp lệ,
        // đây là thời điểm hoàn hảo để lấy các con trỏ hàm cốt lõi khác.
        fprintf(stderr, "  [INFO] Lấy các con trỏ hàm cốt lõi sau khi tạo instance...\n");

        g_vkQueueSubmit_real = (PFN_vkQueueSubmit)g_vkGetInstanceProcAddr_real(g_last_instance, "vkQueueSubmit");

        if (!g_vkQueueSubmit_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkQueueSubmit gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkQueueSubmit gốc.\n");
        }

	g_vkQueuePresentKHR_real = (PFN_vkQueuePresentKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkQueuePresentKHR");
        if (!g_vkQueuePresentKHR_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkQueuePresentKHR gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkQueuePresentKHR gốc.\n");
        }
        // g_vkWaitForFences_real = (PFN_vkWaitForFences)g_vkGetInstanceProcAddr_real(g_last_instance, "vkWaitForFences")
	g_vkResetFences_real = (PFN_vkResetFences)g_vkGetInstanceProcAddr_real(g_last_instance, "vkResetFences");
        if (!g_vkResetFences_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkResetFences gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkResetFences gốc.\n");
        }
	g_vkAllocateMemory_real = (PFN_vkAllocateMemory)g_vkGetInstanceProcAddr_real(g_last_instance, "vkAllocateMemory");
        if (!g_vkAllocateMemory_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkAllocateMemory gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkAllocateMemory gốc.\n");
        }

        g_vkFreeMemory_real = (PFN_vkFreeMemory)g_vkGetInstanceProcAddr_real(g_last_instance, "vkFreeMemory");
        if (!g_vkFreeMemory_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkFreeMemory gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkFreeMemory gốc.\n");
        }
	g_vkMapMemory_real = (PFN_vkMapMemory)g_vkGetInstanceProcAddr_real(g_last_instance, "vkMapMemory");
        if (!g_vkMapMemory_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkMapMemory gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkMapMemory gốc.\n");
        }

        g_vkUnmapMemory_real = (PFN_vkUnmapMemory)g_vkGetInstanceProcAddr_real(g_last_instance, "vkUnmapMemory");
        if (!g_vkUnmapMemory_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkUnmapMemory gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkUnmapMemory gốc.\n");
        }
	g_vkCreateSwapchainKHR_real = (PFN_vkCreateSwapchainKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateSwapchainKHR");
        if (!g_vkCreateSwapchainKHR_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkCreateSwapchainKHR gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkCreateSwapchainKHR gốc.\n");
        }
	g_vkAcquireNextImageKHR_real = (PFN_vkAcquireNextImageKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkAcquireNextImageKHR");
	if (!g_vkAcquireNextImageKHR_real) {
            fprintf(stderr, "  [CRITICAL ERROR] LYNXVK: Không thể lấy con trỏ hàm vkAcquireNextImageKHR gốc!\n");
        } else {
            fprintf(stderr, "  [INFO] LYNXVK: Đã lấy và lưu trữ thành công con trỏ hàm vkAcquireNextImageKHR gốc.\n");
        }
    } else {
            fprintf(stderr, "LYNXVK WARNING: Real vkCreateInstance failed with error %d. Instance handle not stored.\n", result);
        }

    // Cleanup the memory we allocated.
    if (needsFiltering) {
        printf("LynxVK: Cleaning up allocated extension list.\n");
        // We delete newExtensions, which we allocated.
        delete[] newExtensions;
    }

    return result;
}

/*
 * vkGetDeviceProcAddr
 * -------------------
 * Chặn lời gọi để lấy địa chỉ các hàm cấp thiết bị.
 * Đây là "tổng đài chuyển mạch" thứ hai và cuối cùng, hoạt động sau khi
 * một VkDevice đã được tạo.
 *
 * Nó kiểm tra tên hàm được yêu cầu. Nếu đó là một hàm mà chúng ta đã viết
 * wrapper (ví dụ: các hàm swapchain), nó sẽ trả về địa chỉ của wrapper đó.
 * Nếu không, nó sẽ gọi hàm gốc của driver để lấy địa chỉ thật.
 */
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice        device,
    const char* pName)
{
    // Hàm này có thể được gọi rất thường xuyên, nên chúng ta sẽ tắt log mặc định
    // printf("LynxVK: Đã chặn vkGetDeviceProcAddr cho hàm '%s'.\n", pName);

    // Kiểm tra và trả về các wrapper của chúng ta cho các hàm cấp thiết bị
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (strcmp(pName, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)vkDestroyDevice; // Bạn sẽ cần thêm hàm này
    if (strcmp(pName, "vkGetDeviceQueue") == 0) return (PFN_vkVoidFunction)vkGetDeviceQueue;

    // --- Các hàm Swapchain ---
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    if (strcmp(pName, "vkDestroySwapchainKHR") == 0) return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    if (strcmp(pName, "vkGetSwapchainImagesKHR") == 0) return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    if (strcmp(pName, "vkAcquireNextImageKHR") == 0) return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)vkQueuePresentKHR;

    // --- Các hàm tài nguyên & render ---
    if (strcmp(pName, "vkCreateImageView") == 0) return (PFN_vkVoidFunction)vkCreateImageView;
    if (strcmp(pName, "vkCreateRenderPass") == 0) return (PFN_vkVoidFunction)vkCreateRenderPass;
    if (strcmp(pName, "vkCreateFramebuffer") == 0) return (PFN_vkVoidFunction)vkCreateFramebuffer;
    if (strcmp(pName, "vkCreateSemaphore") == 0) return (PFN_vkVoidFunction)vkCreateSemaphore;
    if (strcmp(pName, "vkCreateCommandPool") == 0) return (PFN_vkVoidFunction)vkCreateCommandPool;
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) return (PFN_vkVoidFunction)vkAllocateCommandBuffers;

    // --- Các hàm Command Buffer ---
    //if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (strcmp(pName, "vkEndCommandBuffer") == 0) return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if (strcmp(pName, "vkCmdBeginRenderPass") == 0) return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    if (strcmp(pName, "vkCmdEndRenderPass") == 0) return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    //vkCreateDevice
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)vkCreateDevice;
    //vkWaitForFences
    if (strcmp(pName, "vkWaitForFences") == 0) return (PFN_vkVoidFunction)vkWaitForFences;
    //vkQueueSubmit
    if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)vkQueueSubmit;
    //vkQueuePresentKHR
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)vkQueuePresentKHR;
    //vkResetFences
    if (strcmp(pName, "vkResetFences") == 0) return (PFN_vkVoidFunction)vkResetFences;
    //memory
    if (strcmp(pName, "vkAllocateMemory") == 0) return (PFN_vkVoidFunction)vkAllocateMemory;
    if (strcmp(pName, "vkFreeMemory") == 0) return (PFN_vkVoidFunction)vkFreeMemory;
    //vkmapmemory
    if (strcmp(pName, "vkMapMemory") == 0) return (PFN_vkVoidFunction)vkMapMemory;
    if (strcmp(pName, "vkUnmapMemory") == 0) return (PFN_vkVoidFunction)vkUnmapMemory;

    // Nếu không có con trỏ hàm gốc, hãy thử lấy nó ngay bây giờ
    if (!g_vkGetDeviceProcAddr_real) {
        g_vkGetDeviceProcAddr_real = (PFN_vkGetDeviceProcAddr)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetDeviceProcAddr");
        if (!g_vkGetDeviceProcAddr_real) {
            fprintf(stderr, "LYNXVK CRITICAL ERROR: Không thể lấy con trỏ hàm vkGetDeviceProcAddr gốc.\n");
            return nullptr;
        }
    }

    // Nếu không phải là hàm chúng ta quan tâm, hãy gọi hàm gốc
    return g_vkGetDeviceProcAddr_real(device, pName);
}

/*
 * vkGetPhysicalDeviceFormatProperties
 * -----------------------------------
 * Chặn lời gọi để lấy các thuộc tính của một định dạng cụ thể trên thiết bị vật lý.
 *
 * Đây là một hàm cực kỳ quan trọng đối với DXVK, được dùng để xác định xem
 * GPU hỗ trợ các định dạng texture và buffer nào của Direct3D.
 * Việc thiếu hàm này có thể gây ra lỗi "ConvertFormat: Unknown format".
 */
extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice            physicalDevice,
    VkFormat                    format,
    VkFormatProperties* pFormatProperties)
{
    printf("LynxVK: Đã chặn vkGetPhysicalDeviceFormatProperties cho định dạng %d.\n", format);

    // Lấy con trỏ hàm gốc từ driver
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties_real =
        (PFN_vkGetPhysicalDeviceFormatProperties)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceFormatProperties");

    if (!vkGetPhysicalDeviceFormatProperties_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkGetPhysicalDeviceFormatProperties gốc.\n");
        // Nếu không có hàm gốc, chúng ta nên xóa các thuộc tính để báo hiệu không hỗ trợ
        if (pFormatProperties) {
            pFormatProperties->linearTilingFeatures = 0;
            pFormatProperties->optimalTilingFeatures = 0;
            pFormatProperties->bufferFeatures = 0;
        }
        return;
    }

    // Gọi hàm gốc để lấy thông tin thật
    vkGetPhysicalDeviceFormatProperties_real(physicalDevice, format, pFormatProperties);
}

/*
 * vkEnumerateDeviceExtensionProperties
 * ------------------------------------
 * Chặn lời gọi để lấy danh sách các extension được hỗ trợ bởi một thiết bị vật lý (GPU).
 *
 * Đây là một hàm cực kỳ quan trọng đối với DXVK. Hiện tại, chúng ta sẽ làm một
 * wrapper "truyền qua" đơn giản để ghi log và đảm bảo nó không bị lỗi liên kết.
 * Sau này, chúng ta có thể cần phải "lừa" nó bằng cách thêm các extension giả mạo ở đây.
 */
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice            physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    printf("LynxVK: Đã chặn vkEnumerateDeviceExtensionProperties cho thiết bị %p.\n", (void*)physicalDevice);

    // Lấy con trỏ hàm gốc từ driver
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties_real =
        (PFN_vkEnumerateDeviceExtensionProperties)g_vkGetInstanceProcAddr_real(g_last_instance, "vkEnumerateDeviceExtensionProperties");

    if (!vkEnumerateDeviceExtensionProperties_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkEnumerateDeviceExtensionProperties gốc.\n");
        if (pPropertyCount) *pPropertyCount = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Ghi log lần gọi đầu tiên (hỏi số lượng)
    if (pProperties == NULL) {
        printf("LynxVK: Ứng dụng đang hỏi số lượng extension của thiết bị.\n");
    }

    // Gọi hàm gốc để lấy thông tin thật
    VkResult result = vkEnumerateDeviceExtensionProperties_real(physicalDevice, pLayerName, pPropertyCount, pProperties);

    // Ghi log kết quả
    if (pProperties == NULL) {
        printf("LynxVK: Driver báo cáo thiết bị có %u extension.\n", *pPropertyCount);
    } else {
        printf("LynxVK: Đã điền thông tin extension của thiết bị.\n");
    }

    return result;
}



/*
 * vkEnumerateInstanceExtensionProperties (CHẾ ĐỘ GỠ LỖI)
 * -----------------------------------------------------
 * Phiên bản này được thêm rất nhiều log để tìm ra tại sao Wine/vulkaninfo báo lỗi.
 * Chúng ta sẽ theo dõi chi tiết cả hai lần gọi.
 */
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    printf("\n--- LYNXVK DEBUG: Đã chặn vkEnumerateInstanceExtensionProperties ---\n");
    printf("LYNXVK DEBUG: pLayerName = %s\n", pLayerName ? pLayerName : "NULL");

    // Lỗi nghiêm trọng nếu pPropertyCount là NULL
    if (!pPropertyCount) {
        fprintf(stderr, "LYNXVK CRITICAL: pPropertyCount là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Lấy con trỏ hàm gốc (chúng ta đã làm trong initialize, nhưng kiểm tra lại cho chắc)
    if (!g_vkEnumerateInstanceExtensionProperties_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm gốc là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Lần gọi 1: Ứng dụng muốn biết có bao nhiêu extension
    if (pProperties == NULL) {
        printf("LYNXVK DEBUG: (Lần gọi 1) Ứng dụng đang hỏi số lượng extension (pProperties là NULL).\n");
        
        // Hỏi driver gốc xem nó có bao nhiêu extension
        uint32_t real_count = 0;
        VkResult result = g_vkEnumerateInstanceExtensionProperties_real(pLayerName, &real_count, NULL);
        
        if (result != VK_SUCCESS) {
            fprintf(stderr, "LYNXVK ERROR: Lời gọi gốc (lần 1) thất bại với mã lỗi: %d\n", result);
            return result;
        }

        printf("LYNXVK DEBUG: Driver gốc báo cáo có %u extension.\n", real_count);
        
        // Chúng ta sẽ thêm 2 extension giả mạo
        uint32_t final_count = real_count + 2;
        *pPropertyCount = final_count;
        
        printf("LYNXVK DEBUG: Báo cáo lại cho ứng dụng tổng số là %u extension.\n", final_count);
        return VK_SUCCESS;
    }
    
    // Lần gọi 2: Ứng dụng cung cấp bộ đệm để chúng ta điền vào
    printf("LYNXVK DEBUG: (Lần gọi 2) Ứng dụng cung cấp bộ đệm để lấy dữ liệu (pProperties KHÔNG phải NULL).\n");
    uint32_t capacity_provided = *pPropertyCount;
    printf("LYNXVK DEBUG: Dung lượng bộ đệm được cung cấp: %u\n", capacity_provided);

    // Lấy danh sách extension thật từ driver
    // Chúng ta phải yêu cầu driver điền vào một bộ đệm tạm thời
    VkExtensionProperties* real_properties = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * capacity_provided);
    if (!real_properties) {
        fprintf(stderr, "LYNXVK CRITICAL: Không thể cấp phát bộ đệm tạm thời!\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    uint32_t real_count = capacity_provided; // Giả sử driver sẽ không điền nhiều hơn dung lượng chúng ta có
    VkResult result = g_vkEnumerateInstanceExtensionProperties_real(pLayerName, &real_count, real_properties);
    
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        fprintf(stderr, "LYNXVK ERROR: Lời gọi gốc (lần 2) thất bại với mã lỗi: %d\n", result);
        free(real_properties);
        return result;
    }
    printf("LYNXVK DEBUG: Driver gốc đã điền %u extension vào bộ đệm tạm thời.\n", real_count);

    // Bây giờ sao chép các extension thật vào bộ đệm của ứng dụng
    uint32_t copied_count = 0;
    for (uint32_t i = 0; i < real_count && i < capacity_provided; ++i) {
        pProperties[i] = real_properties[i];
        copied_count++;
    }

    free(real_properties); // Dọn dẹp bộ đệm tạm
    printf("LYNXVK DEBUG: Đã sao chép %u extension thật vào bộ đệm của ứng dụng.\n", copied_count);

    // Thêm các extension giả mạo của chúng ta vào cuối, nếu còn chỗ
    if (copied_count < capacity_provided) {
        strcpy(pProperties[copied_count].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
        pProperties[copied_count].specVersion = 6;
        copied_count++;
        printf("LYNXVK DEBUG: Đã thêm VK_KHR_surface. Tổng số đã sao chép: %u\n", copied_count);
    }
    if (copied_count < capacity_provided) {
        strcpy(pProperties[copied_count].extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        pProperties[copied_count].specVersion = 6;
        copied_count++;
        printf("LYNXVK DEBUG: Đã thêm VK_KHR_xlib_surface. Tổng số đã sao chép: %u\n", copied_count);
    }

    // Xác định kết quả cuối cùng
    uint32_t total_required = real_count + 2;
    if (copied_count < total_required) {
        printf("LYNXVK DEBUG: Bộ đệm không đủ lớn (%u/%u), trả về VK_INCOMPLETE.\n", copied_count, total_required);
        return VK_INCOMPLETE;
    } else {
        printf("LYNXVK DEBUG: Đã điền đầy đủ, trả về VK_SUCCESS.\n", copied_count);
        return VK_SUCCESS;
    }
}




/*
 * vkCreateXlibSurfaceKHR
 * ----------------------
 * This is the core redirection function for surfaces.
 * 1. It ignores the Xlib-specific information.
 * 2. It calls a private function to get a native Android ANativeWindow.
 * 3. It uses this ANativeWindow to call the REAL vkCreateAndroidSurfaceKHR.
 * 4. CRITICAL: It stores the newly created REAL surface handle in a map
 * so that other intercepted functions can look it up later.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance                          instance,
    const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface)
{
    printf("LynxVK: Intercepted vkCreateXlibSurfaceKHR.\n");
    printf("LynxVK: Ignoring Xlib Display and Window, creating Android Surface instead.\n");

    // --- BẮT ĐẦU SỬA LỖI LOGIC ---

    ExternFunction windowFactory;
    // THAY ĐỔI: Nhận giá trị trả về trực tiếp từ hàm, không dựa vào giả định ngầm.
    ANativeWindow* local_native_window = windowFactory.createNativeWindow("LynxVK Surface", 1280, 720, false);

    // THAY ĐỔI: Kiểm tra con trỏ cục bộ vừa nhận được.
    if (!local_native_window) {
        fprintf(stderr, "LYNXVK ERROR: createNativeWindow() returned a NULL pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    printf("LynxVK: Successfully created and received ANativeWindow pointer.\n");

    // THAY ĐỔI: Cập nhật biến toàn cục một cách tường minh để các phần khác của code có thể sử dụng nếu cần.
    g_native_window = local_native_window;

    // --- KẾT THÚC SỬA LỖI LOGIC ---

    // Chuẩn bị cấu trúc để tạo một Android surface
    VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = NULL;
    surfaceCreateInfo.flags = 0;
    // THAY ĐỔI: Sử dụng con trỏ cục bộ (an toàn hơn) để tạo surface.
    surfaceCreateInfo.window = local_native_window;

    // Lấy con trỏ hàm thật cho vkCreateAndroidSurfaceKHR
    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR_real =
        (PFN_vkCreateAndroidSurfaceKHR)g_vkGetInstanceProcAddr_real(instance, "vkCreateAndroidSurfaceKHR");

    if (!vkCreateAndroidSurfaceKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkCreateAndroidSurfaceKHR function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    printf("LynxVK: --> Calling real vkCreateAndroidSurfaceKHR.\n");
    VkResult result = vkCreateAndroidSurfaceKHR_real(instance, &surfaceCreateInfo, pAllocator, pSurface);

    if (result == VK_SUCCESS) {
        printf("LynxVK: --> Real vkCreateAndroidSurfaceKHR succeeded.\n");

        VkSurfaceKHR real_surface = *pSurface;
        g_surface_map[real_surface] = real_surface;
        printf("LynxVK: Mapped real surface handle for later lookup.\n");

    } else {
        fprintf(stderr, "LYNXVK WARNING: Real vkCreateAndroidSurfaceKHR failed with error %d.\n", result);
    }

    return result;
}

/*
 * vkGetPhysicalDeviceSurfaceSupportKHR
 * ------------------------------------
 * This function is critical. It checks if a specific queue family of a physical
 * device can present to a given surface.
 *
 * This CANNOT be a simple pass-through wrapper. The application provides a "fake"
 * surface handle (the one we returned from our vkCreateXlibSurfaceKHR). We must
 * look up this fake handle in our map to find the REAL Android surface handle,
 * and then pass the REAL handle to the actual driver function.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice        physicalDevice,
    uint32_t                queueFamilyIndex,
    VkSurfaceKHR            surface,
    VkBool32* pSupported)
{
    printf("LynxVK: Intercepted vkGetPhysicalDeviceSurfaceSupportKHR.\n");

    // Default to not supported in case of error
    if (pSupported) {
        *pSupported = VK_FALSE;
    }

    // Step 1: Look up the real surface handle from our map.
    VkSurfaceKHR real_surface = VK_NULL_HANDLE;
    auto it = g_surface_map.find(surface);

    if (it != g_surface_map.end()) {
        real_surface = it->second;
        // printf("LynxVK: Found real surface handle for the given fake one.\n");
    } else {
        fprintf(stderr, "LYNXVK ERROR: vkGetPhysicalDeviceSurfaceSupportKHR called with an unknown surface handle!\n");
        return VK_ERROR_SURFACE_LOST_KHR; // Return an appropriate error
    }

    // Step 2: Get the real function pointer from the driver.
    if (g_last_instance == VK_NULL_HANDLE) {
        fprintf(stderr, "LYNXVK ERROR: Cannot check surface support because instance handle is NULL.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR_real =
        (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceSurfaceSupportKHR");

    if (!vkGetPhysicalDeviceSurfaceSupportKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkGetPhysicalDeviceSurfaceSupportKHR function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Step 3: Call the real function, but with the REAL surface handle.
    // printf("LynxVK: --> Calling real vkGetPhysicalDeviceSurfaceSupportKHR with the real surface handle.\n");
    return vkGetPhysicalDeviceSurfaceSupportKHR_real(physicalDevice, queueFamilyIndex, real_surface, pSupported);
}

/*
 * vkGetPhysicalDeviceSurfaceCapabilitiesKHR
 * -----------------------------------------
 * Chặn lời gọi để lấy các khả năng của một surface.
 * Đây là một hàm "truyền qua có ánh xạ" (pass-through with mapping).
 * 1. Nhận handle surface từ ứng dụng.
 * 2. Tra cứu handle này trong map `g_surface_map` để lấy ra handle thật sự.
 * 3. Gọi hàm gốc của driver với handle thật đó để lấy thông tin.
 * 4. Trả về thông tin cho ứng dụng.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice            physicalDevice,
    VkSurfaceKHR                surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
    printf("LynxVK: Intercepted vkGetPhysicalDeviceSurfaceCapabilitiesKHR.\n");

    // Tra cứu handle surface được cung cấp trong map của chúng ta.
    auto it = g_surface_map.find(surface);
    if (it == g_surface_map.end()) {
        // Nếu chúng ta không quản lý surface này, đây là một lỗi.
        fprintf(stderr, "LYNXVK ERROR: vkGetPhysicalDeviceSurfaceCapabilitiesKHR called with an unknown surface handle.\n");
        // Trả về một lỗi rõ ràng thay vì để ứng dụng crash.
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSurfaceKHR real_surface = it->second;
    printf("LynxVK: Found matching surface in map. Forwarding to real implementation.\n");

    // Lấy con trỏ hàm thật từ driver
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_real =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    if (!vkGetPhysicalDeviceSurfaceCapabilitiesKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkGetPhysicalDeviceSurfaceCapabilitiesKHR function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với handle surface thật
    return vkGetPhysicalDeviceSurfaceCapabilitiesKHR_real(physicalDevice, real_surface, pSurfaceCapabilities);
}

/*
 * vkGetPhysicalDeviceSurfaceFormatsKHR
 * ------------------------------------
 * Chặn lời gọi để lấy danh sách các định dạng (format) và không gian màu (color space)
 * mà một surface hỗ trợ. Đây cũng là một "wrapper truyền qua có ánh xạ".
 * 1. Nhận handle surface từ ứng dụng.
 * 2. Tra cứu handle này trong map `g_surface_map` để lấy ra handle thật sự.
 * 3. Gọi hàm gốc của driver với handle thật đó để lấy danh sách định dạng.
 * 4. Trả về danh sách cho ứng dụng.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice            physicalDevice,
    VkSurfaceKHR                surface,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats)
{
    printf("LynxVK: Intercepted vkGetPhysicalDeviceSurfaceFormatsKHR.\n");

    // Tra cứu handle surface được cung cấp trong map của chúng ta.
    auto it = g_surface_map.find(surface);
    if (it == g_surface_map.end()) {
        // Nếu chúng ta không quản lý surface này, đây là một lỗi.
        fprintf(stderr, "LYNXVK ERROR: vkGetPhysicalDeviceSurfaceFormatsKHR called with an unknown surface handle.\n");
        // Đặt số lượng format về 0 để ứng dụng biết không có gì khả dụng.
        if (pSurfaceFormatCount) {
            *pSurfaceFormatCount = 0;
        }
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSurfaceKHR real_surface = it->second;
    printf("LynxVK: Found matching surface in map. Forwarding to real implementation.\n");

    // Lấy con trỏ hàm thật từ driver
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR_real =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");

    if (!vkGetPhysicalDeviceSurfaceFormatsKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkGetPhysicalDeviceSurfaceFormatsKHR function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với handle surface thật
    return vkGetPhysicalDeviceSurfaceFormatsKHR_real(physicalDevice, real_surface, pSurfaceFormatCount, pSurfaceFormats);
}

/*
 * vkGetPhysicalDeviceSurfacePresentModesKHR
 * -----------------------------------------
 * Chặn lời gọi để lấy danh sách các chế độ hiển thị (present modes) mà một surface hỗ trợ.
 * Đây là hàm cuối cùng trong chuỗi ba hàm "hỏi thông tin".
 * 1. Nhận handle surface từ ứng dụng.
 * 2. Tra cứu handle này trong map `g_surface_map` để lấy ra handle thật sự.
 * 3. Gọi hàm gốc của driver với handle thật đó để lấy danh sách chế độ.
 * 4. Trả về danh sách cho ứng dụng.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice            physicalDevice,
    VkSurfaceKHR                surface,
    uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes)
{
    printf("LynxVK: Đã chặn vkGetPhysicalDeviceSurfacePresentModesKHR.\n");

    // Tra cứu handle surface được cung cấp trong map của chúng ta.
    auto it = g_surface_map.find(surface);
    if (it == g_surface_map.end()) {
        // Nếu chúng ta không quản lý surface này, đây là một lỗi.
        fprintf(stderr, "LYNXVK ERROR: vkGetPhysicalDeviceSurfacePresentModesKHR được gọi với một surface handle không xác định.\n");
        if (pPresentModeCount) {
            *pPresentModeCount = 0;
        }
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSurfaceKHR real_surface = it->second;
    printf("LynxVK: Đã tìm thấy surface khớp trong map. Chuyển tiếp đến hàm thực thi gốc.\n");

    // Lấy con trỏ hàm thật từ driver
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR_real =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");

    if (!vkGetPhysicalDeviceSurfacePresentModesKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkGetPhysicalDeviceSurfacePresentModesKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với handle surface thật
    return vkGetPhysicalDeviceSurfacePresentModesKHR_real(physicalDevice, real_surface, pPresentModeCount, pPresentModes);
}



/*
 * vkCreateSwapchainKHR
 * --------------------
 * Chặn lời gọi quan trọng nhất: tạo swapchain.
 * Đây là nơi "album ảnh" để hiển thị được tạo ra.
 * 1. Nhận struct thông tin tạo swapchain từ ứng dụng.
 * 2. Tạo một bản sao của struct này để có thể chỉnh sửa.
 * 3. Tra cứu `surface` trong `g_surface_map` để lấy handle thật.
 * 4. Cập nhật `surface` trong struct bản sao với handle thật.
 * 5. (Nâng cao) Tra cứu `oldSwapchain` trong `g_swapchain_map` và cập nhật nếu có.
 * 6. Gọi hàm gốc của driver với struct đã được sửa đổi.
 * 7. Nếu thành công, lưu lại handle swapchain mới vào `g_swapchain_map`.
 */

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain)
{
    printf("LynxVK: Đã chặn vkCreateSwapchainKHR.\n");

    if (!pCreateInfo) {
        fprintf(stderr, "LYNXVK ERROR: pCreateInfo trong vkCreateSwapchainKHR là NULL.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // -- Bước 1: Tra cứu và chuẩn bị thông tin --
    printf("LynxVK: Đang tra cứu surface handle trong map...\n");
    auto it = g_surface_map.find(pCreateInfo->surface);
    if (it == g_surface_map.end()) {
        fprintf(stderr, "LYNXVK ERROR: vkCreateSwapchainKHR được gọi với một surface handle không xác định.\n");
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    VkSurfaceKHR real_surface = it->second;
    printf("LynxVK: Đã tìm thấy surface thật. Sửa đổi thông tin tạo swapchain.\n");

    // -- Bước 2: Tạo bản sao và sửa đổi --
    // Chúng ta phải tạo một bản sao vì pCreateInfo là const (không thể thay đổi).
    VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.surface = real_surface; // Đây là bước sửa đổi quan trọng nhất!

    // Xử lý trường hợp tái tạo swapchain (nâng cao nhưng quan trọng)
    if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE) {
        printf("LynxVK: Đang tra cứu oldSwapchain handle...\n");
        auto old_it = g_swapchain_map.find(pCreateInfo->oldSwapchain);
        if (old_it != g_swapchain_map.end()) {
            modifiedCreateInfo.oldSwapchain = old_it->second; // Sửa cả oldSwapchain
        } else {
            // Nếu không tìm thấy, có thể là một lỗi, nhưng chúng ta cứ thử tiếp với handle cũ
             fprintf(stderr, "LYNXVK WARNING: oldSwapchain không được tìm thấy trong map.\n");
        }
    }


    // -- Bước 3: Gọi hàm gốc --
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_real =
        (PFN_vkCreateSwapchainKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateSwapchainKHR");

    if (!vkCreateSwapchainKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCreateSwapchainKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    printf("LynxVK: Gọi hàm vkCreateSwapchainKHR gốc với thông tin đã sửa đổi.\n");
    VkResult result = vkCreateSwapchainKHR_real(device, &modifiedCreateInfo, pAllocator, pSwapchain);


    // -- Bước 4: Lưu kết quả --
    if (result == VK_SUCCESS) {
        printf("LynxVK: vkCreateSwapchainKHR gốc thành công. Lưu lại handle swapchain mới.\n");
        // Lưu lại mối quan hệ. *pSwapchain chứa handle mới do driver tạo ra.
        g_swapchain_map[*pSwapchain] = *pSwapchain;
    }

    return result;
}

/*
 * vkGetSwapchainImagesKHR
 * -----------------------
 * Chặn lời gọi để lấy danh sách các "tấm ảnh" (VkImage) từ bên trong một swapchain.
 * Ứng dụng cần các handle ảnh này để tạo VkImageView và sau đó là Framebuffer để vẽ.
 * 1. Nhận handle swapchain từ ứng dụng.
 * 2. Tra cứu handle này trong map `g_swapchain_map` để lấy ra handle thật sự.
 * 3. Gọi hàm gốc của driver với handle thật đó để lấy danh sách ảnh.
 * 4. Các handle ảnh trả về không cần ánh xạ, chúng ta có thể đưa thẳng cho ứng dụng.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice            device,
    VkSwapchainKHR      swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages)
{
    printf("LynxVK: Đã chặn vkGetSwapchainImagesKHR.\n");

    // Tra cứu handle swapchain được cung cấp trong map của chúng ta.
    auto it = g_swapchain_map.find(swapchain);
    if (it == g_swapchain_map.end()) {
        // Nếu chúng ta không quản lý swapchain này, đây là một lỗi.
        fprintf(stderr, "LYNXVK ERROR: vkGetSwapchainImagesKHR được gọi với một swapchain handle không xác định.\n");
        if (pSwapchainImageCount) {
            *pSwapchainImageCount = 0;
        }
        return VK_ERROR_INITIALIZATION_FAILED; // Sử dụng lỗi này để báo hiệu vấn đề
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSwapchainKHR real_swapchain = it->second;
    printf("LynxVK: Đã tìm thấy swapchain khớp trong map. Chuyển tiếp đến hàm thực thi gốc.\n");

    // Lấy con trỏ hàm thật từ driver
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_real =
        (PFN_vkGetSwapchainImagesKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetSwapchainImagesKHR");

    if (!vkGetSwapchainImagesKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkGetSwapchainImagesKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với handle swapchain thật
    // Hàm này sẽ điền vào pSwapchainImages các handle ảnh thật sự.
    return vkGetSwapchainImagesKHR_real(device, real_swapchain, pSwapchainImageCount, pSwapchainImages);
}


// Hàm trợ giúp: Dịch VkResult thành chuỗi ký tự
// Bạn có thể đặt hàm này ở đâu đó gần đầu file LynxVK.cpp
const char* VulkanResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR"; // <<-- KẺ TÌNH NGHI
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
        default: return "MÃ LỖI VULKAN KHÔNG XÁC ĐỊNH";
    }
}

// ==========================================================================================
// Wrapper cho vkAcquireNextImageKHR (Theo dõi Semaphore)
// Hãy thay thế hàm cũ của bạn bằng hàm này.
// ==========================================================================================
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice            device,
    VkSwapchainKHR      swapchain,
    uint64_t            timeout,
    VkSemaphore         semaphore, // Semaphore này sẽ được BÁO HIỆU
    VkFence             fence,
    uint32_t* pImageIndex)
{
    auto it = g_swapchain_map.find(swapchain);
    if (it == g_swapchain_map.end()) {
        fprintf(stderr, "LYNXVK ERROR: vkAcquireNextImageKHR_real được gọi với một swapchain handle không xác định.\n");
        return VK_ERROR_DEVICE_LOST;
    }
    VkSwapchainKHR real_swapchain = it->second;

    //PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_real = 
      //  (PFN_vkAcquireNextImageKHR)g_vkGetDeviceProcAddr_real(device, "vkAcquireNextImageKHR");

    if (!g_vkAcquireNextImageKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkAcquireNextImageKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    // Ghi log chi tiết VÀO ĐÂY
    fprintf(stderr, "[KHUNG HÌNH %llu] vkAcquireNextImageKHR:\n", (unsigned long long)g_frame_counter);
    fprintf(stderr, "  |-- Sẽ báo hiệu Semaphore : %p (khi ảnh sẵn sàng)\n", (void*)semaphore);
    fprintf(stderr, "  |-- Sẽ báo hiệu Fence      : %p\n", (void*)fence);

    VkResult result = g_vkAcquireNextImageKHR_real(device, real_swapchain, timeout, semaphore, fence, pImageIndex);

    fprintf(stderr, "  |-- TRẢ VỀ: %s (%d)\n", VulkanResultToString(result), result);
    return result;
}

// ==========================================================================================
// Wrapper cho vkQueueSubmit
// ==========================================================================================
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue             queue,
    uint32_t            submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence             fence)
{
    fprintf(stderr, "[KHUNG HÌNH %llu] vkQueueSubmit:\n", (unsigned long long)g_frame_counter);
    fprintf(stderr, "  |-- Sẽ báo hiệu Fence : %p (khi hoàn thành)\n", (void*)fence);

    if (pSubmits) {
        for (uint32_t i = 0; i < submitCount; ++i) {
            const VkSubmitInfo* submit_info = &pSubmits[i];
            fprintf(stderr, "  |-- Lô #%u:\n", i);
            fprintf(stderr, "  |   |-- Đợi Semaphore    : %p\n", (submit_info->waitSemaphoreCount > 0 ? (void*)submit_info->pWaitSemaphores[0] : NULL));
            fprintf(stderr, "  |   |-- Sẽ báo hiệu Semaphore : %p\n", (submit_info->signalSemaphoreCount > 0 ? (void*)submit_info->pSignalSemaphores[0] : NULL));
        }
    }

    VkResult result = g_vkQueueSubmit_real(queue, submitCount, pSubmits, fence);

    fprintf(stderr, "  |-- TRẢ VỀ: %s (%d)\n", VulkanResultToString(result), result);
    return result;
}

// Wrapper cho vkResetFences (Đã sửa lỗi)
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice            device,
    uint32_t            fenceCount,
    const VkFence* pFences)
{
    fprintf(stderr, "--> LYNXVK: Đã vào hàm vkResetFences\n");
    fprintf(stderr, "  |-- Đang reset %u fence.\n", fenceCount);
    if (pFences && fenceCount > 0) {
        fprintf(stderr, "  |-- Fence đầu tiên để reset: %p\n", (void*)pFences[0]);
    }

    // Bây giờ, chỉ cần gọi trực tiếp con trỏ hàm đã được lưu sẵn.
    if (!g_vkResetFences_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkResetFences gốc là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkResult result = g_vkResetFences_real(device, fenceCount, pFences);

    fprintf(stderr, "<-- LYNXVK: Rời khỏi hàm vkResetFences với kết quả: %s (%d)\n", VulkanResultToString(result), result);
    return result;
}



extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice            device,
    uint32_t            fenceCount,
    const VkFence* pFences,
    VkBool32            waitAll,
    uint64_t            timeout)
{
    // Chúng ta sẽ luôn ghi log ở đây vì nó rất quan trọng
    fprintf(stderr, "--> LYNXVK: Đã vào hàm vkWaitForFences\n");
    fprintf(stderr, "  |-- Đang đợi %u fence.\n", fenceCount);
    if (pFences && fenceCount > 0) {
        // Chỉ in ra fence đầu tiên để log ngắn gọn
        fprintf(stderr, "  |-- Fence đầu tiên: %p\n", (void*)pFences[0]);
    }
    fprintf(stderr, "  |-- Chế độ đợi tất cả (Wait All): %s\n", waitAll ? "VK_TRUE" : "VK_FALSE");
    fprintf(stderr, "  |-- Thời gian chờ tối đa (Timeout): %llu ns\n", (unsigned long long)timeout);


    PFN_vkWaitForFences vkWaitForFences_real =
        (PFN_vkWaitForFences)g_vkGetDeviceProcAddr_real(device, "vkWaitForFences");

    if (!vkWaitForFences_real) {
        fprintf(stderr, "  [ERROR] LYNXVK: Không thể lấy con trỏ hàm vkWaitForFences gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vkWaitForFences_real(device, fenceCount, pFences, waitAll, timeout);

    fprintf(stderr, "<-- LYNXVK: Rời khỏi hàm vkWaitForFences với kết quả: %s (%d)\n", VulkanResultToString(result), result);
    return result;
}

/*
 * vkQueuePresentKHR
 * -----------------
 * Chặn lời gọi cuối cùng trong vòng lặp render: hiển thị một frame lên màn hình.
 * Đây là hàm phức tạp nhất trong chuỗi swapchain vì nó nhận vào một mảng các swapchain.
 * 1. Nhận struct VkPresentInfoKHR từ ứng dụng.
 * 2. Tạo một bản sao của struct để chỉnh sửa.
 * 3. Tạo một vector mới (mảng động) để lưu các handle swapchain thật.
 * 4. Lặp qua mảng swapchain của ứng dụng, tra cứu từng handle trong `g_swapchain_map`
 * và điền handle thật vào vector mới của chúng ta.
 * 5. Cập nhật con trỏ trong struct bản sao để nó trỏ đến dữ liệu của vector mới.
 * 6. Gọi hàm gốc của driver với struct đã được sửa đổi.
 */
// Wrapper cho vkQueuePresentKHR (Đã sửa lỗi)
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue                 queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    fprintf(stderr, "[KHUNG HÌNH %llu] vkQueuePresentKHR:\n", (unsigned long long)g_frame_counter);

    // --- BẮT ĐẦU LOGIC ÁNH XẠ SWAPCHAIN ---
    // Chúng ta không thể sửa đổi pPresentInfo, vì vậy hãy tạo một bản sao nếu cần.
    VkPresentInfoKHR modifiedPresentInfo = *pPresentInfo;
    std::vector<VkSwapchainKHR> realSwapchains;
    
    // Chỉ thực hiện ánh xạ nếu có swapchain để trình bày
    if (pPresentInfo && pPresentInfo->swapchainCount > 0) {
        realSwapchains.resize(pPresentInfo->swapchainCount);
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
            auto it = g_swapchain_map.find(pPresentInfo->pSwapchains[i]);
            if (it == g_swapchain_map.end()) {
                fprintf(stderr, "  [ERROR] vkQueuePresentKHR được gọi với một swapchain handle không xác định!\n");
                return VK_ERROR_DEVICE_LOST; 
            }
            realSwapchains[i] = it->second;
        }
        // Trỏ bản sao của chúng ta đến mảng các swapchain thật
        modifiedPresentInfo.pSwapchains = realSwapchains.data();
    }
    // --- KẾT THÚC LOGIC ÁNH XẠ SWAPCHAIN ---

    // Bây giờ, chỉ cần gọi trực tiếp con trỏ hàm đã được lưu sẵn.
    if (!g_vkQueuePresentKHR_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkQueuePresentKHR gốc là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    // Gọi hàm gốc với BẢN SAO đã được sửa đổi
    VkResult result = g_vkQueuePresentKHR_real(queue, &modifiedPresentInfo);

    fprintf(stderr, "  |-- TRẢ VỀ: %s (%d)\n", VulkanResultToString(result), result);

    g_frame_counter++;
    fprintf(stderr, "\n <<<<<<<<<< KẾT THÚC KHUNG HÌNH %llu >>>>>>>>>> \n\n", (unsigned long long)g_frame_counter - 1);
    
    return result;
}

// Wrapper cho vkGetPhysicalDeviceXlibPresentationSupportKHR
// Chúng ta sẽ "lừa" ứng dụng bằng cách luôn trả về VK_TRUE.
extern "C" VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice        physicalDevice,
    uint32_t                queueFamilyIndex,
    Display* dpy,
    VisualID                visualID)
{
    fprintf(stderr, "--> LYNXVK: Đã chặn vkGetPhysicalDeviceXlibPresentationSupportKHR.\n");
    fprintf(stderr, "  |-- Bỏ qua các tham số X11 và trả về VK_TRUE để tiếp tục.\n");
    return VK_TRUE;
}



/*_________________________Vulkan_Destroy_____________________*/

/*
 * vkDestroySurfaceKHR
 * -------------------
 * Chặn lời gọi để phá hủy một VkSurfaceKHR.
 * Wrapper này tìm kiếm handle thật trong map, gọi hàm phá hủy gốc,
 * và sau đó xóa bỏ mục tương ứng khỏi map của chúng ta.
 */
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance                   instance,
    VkSurfaceKHR                 surface,
    const VkAllocationCallbacks* pAllocator)
{
    printf("LynxVK: Đã chặn vkDestroySurfaceKHR.\n");

    // Tra cứu handle surface được cung cấp trong map của chúng ta.
    auto it = g_surface_map.find(surface);
    if (it == g_surface_map.end()) {
        // Nếu chúng ta không quản lý surface này, đây là một lỗi.
        // Ghi log cảnh báo và thoát để tránh crash.
        fprintf(stderr, "LYNXVK WARNING: vkDestroySurfaceKHR được gọi với một surface handle không xác định.\n");
        return;
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSurfaceKHR real_surface = it->second;

    // Lấy con trỏ hàm thật từ driver
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR_real =
        (PFN_vkDestroySurfaceKHR)g_vkGetInstanceProcAddr_real(instance, "vkDestroySurfaceKHR");

    if (vkDestroySurfaceKHR_real) {
        // Gọi hàm thật với handle surface thật
        vkDestroySurfaceKHR_real(instance, real_surface, pAllocator);
        printf("LynxVK: Đã gọi hàm vkDestroySurfaceKHR gốc.\n");
    } else {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkDestroySurfaceKHR gốc.\n");
    }

    // Sau khi đã phá hủy, xóa bỏ mục khỏi map của chúng ta.
    g_surface_map.erase(it);
    printf("LynxVK: Đã xóa surface khỏi map nội bộ.\n");
}


/*
 * vkDestroySwapchainKHR
 * ---------------------
 * Chặn lời gọi để phá hủy một VkSwapchainKHR.
 * Tương tự như vkDestroySurfaceKHR, nó tìm kiếm, gọi hàm gốc,
 * và sau đó dọn dẹp map nội bộ của chúng ta.
 */
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice                     device,
    VkSwapchainKHR               swapchain,
    const VkAllocationCallbacks* pAllocator)
{
    printf("LynxVK: Đã chặn vkDestroySwapchainKHR.\n");

    // Tra cứu handle swapchain được cung cấp trong map của chúng ta.
    auto it = g_swapchain_map.find(swapchain);
    if (it == g_swapchain_map.end()) {
        fprintf(stderr, "LYNXVK WARNING: vkDestroySwapchainKHR được gọi với một swapchain handle không xác định.\n");
        return;
    }

    VkSwapchainKHR real_swapchain = it->second;

    // Lấy con trỏ hàm thật từ driver
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR_real =
        (PFN_vkDestroySwapchainKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkDestroySwapchainKHR");

    if (vkDestroySwapchainKHR_real) {
        // Gọi hàm thật với handle swapchain thật
        vkDestroySwapchainKHR_real(device, real_swapchain, pAllocator);
        printf("LynxVK: Đã gọi hàm vkDestroySwapchainKHR gốc.\n");
    } else {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkDestroySwapchainKHR gốc.\n");
    }

    // Xóa bỏ mục khỏi map swapchain.
    g_swapchain_map.erase(it);
    printf("LynxVK: Đã xóa swapchain khỏi map nội bộ.\n");
}



/*
 * vkDestroyInstance
 * -----------------
 * Chặn lời gọi để phá hủy một VkInstance. Đây là một trong những hàm dọn dẹp
 * cuối cùng mà một ứng dụng gọi.
 *
 * Wrapper này có trách nhiệm quan trọng là dọn dẹp trạng thái nội bộ của
 * LynxVK (các map và handle đã lưu) trước khi gọi hàm gốc của driver.
 */
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance                   instance,
    const VkAllocationCallbacks* pAllocator)
{
    printf("LynxVK: Đã chặn vkDestroyInstance. Dọn dẹp tài nguyên của LynxVK...\n");

    // Lấy con trỏ hàm thật TRƯỚC KHI phá hủy instance,
    // vì chúng ta cần instance để lấy con trỏ.
    PFN_vkDestroyInstance vkDestroyInstance_real =
        (PFN_vkDestroyInstance)g_vkGetInstanceProcAddr_real(instance, "vkDestroyInstance");

    if (!vkDestroyInstance_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkDestroyInstance gốc.\n");
        // Dù thất bại, chúng ta vẫn nên cố gắng dọn dẹp trạng thái của mình.
    }

    // Dọn dẹp trạng thái nội bộ của LynxVK.
    // Vì instance sắp bị phá hủy, tất cả các surface và swapchain con cũng vậy.
    g_surface_map.clear();
    g_swapchain_map.clear();
    g_last_instance = VK_NULL_HANDLE;
    printf("LynxVK: Đã dọn dẹp các map nội bộ.\n");

    // Chỉ sau khi dọn dẹp xong, chúng ta mới gọi hàm gốc của driver.
    if (vkDestroyInstance_real) {
        vkDestroyInstance_real(instance, pAllocator);
        printf("LynxVK: Đã gọi hàm vkDestroyInstance gốc.\n");
    }
}

/*
 * vkDestroyDevice
 * ---------------
 * Chặn lời gọi để phá hủy một VkDevice.
 *
 * Wrapper này có trách nhiệm quan trọng là dọn dẹp các trạng thái nội bộ
 * của LynxVK liên quan đến thiết bị này (chủ yếu là map swapchain)
 * trước khi gọi hàm gốc của driver.
 */
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice                     device,
    const VkAllocationCallbacks* pAllocator)
{
    printf("LynxVK: Đã chặn vkDestroyDevice. Dọn dẹp tài nguyên liên quan đến thiết bị...\n");

    // Lấy con trỏ hàm thật từ driver.
    // Chúng ta có thể dùng g_last_instance vì vkDestroyDevice là một hàm cốt lõi.
    PFN_vkDestroyDevice vkDestroyDevice_real =
        (PFN_vkDestroyDevice)g_vkGetInstanceProcAddr_real(g_last_instance, "vkDestroyDevice");

    if (!vkDestroyDevice_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkDestroyDevice gốc.\n");
        // Dù thất bại, chúng ta vẫn nên cố gắng dọn dẹp.
    }

    // Dọn dẹp trạng thái nội bộ của LynxVK.
    // Khi device bị phá hủy, tất cả swapchain con cũng bị phá hủy.
    g_swapchain_map.clear();
    printf("LynxVK: Đã dọn dẹp map swapchain nội bộ.\n");

    // Gọi hàm gốc của driver để thực hiện việc phá hủy thực sự.
    if (vkDestroyDevice_real) {
        vkDestroyDevice_real(device, pAllocator);
        printf("LynxVK: Đã gọi hàm vkDestroyDevice gốc.\n");
    }
}



/*__________________________Vulkan_Draw_________________________*/

/*
 * vkEnumerateInstanceVersion
 * --------------------------
 * Chặn lời gọi để lấy phiên bản Vulkan API được hỗ trợ.
 *
 * Wrapper này gọi hàm gốc của driver để lấy thông tin.
 * Nếu hàm gốc không tồn tại (trên các hệ thống Vulkan 1.0 cũ),
 * nó sẽ trả về một phiên bản mặc định là 1.0.0.
 */
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(
    uint32_t* pApiVersion)
{
    printf("LynxVK: Đã chặn vkEnumerateInstanceVersion.\n");

    if (!pApiVersion) {
        return VK_ERROR_OUT_OF_HOST_MEMORY; // Theo spec, đây là lỗi
    }

    // Nếu chúng ta đã nạp được hàm gốc, hãy gọi nó.
    if (g_vkEnumerateInstanceVersion_real) {
        printf("LynxVK: Chuyển tiếp tới hàm vkEnumerateInstanceVersion gốc.\n");
        return g_vkEnumerateInstanceVersion_real(pApiVersion);
    }

    // Nếu không, đây có thể là một driver Vulkan 1.0.
    // Trả về một phiên bản hợp lệ để ứng dụng không bị lỗi.
    printf("LynxVK: Hàm gốc không tồn tại, trả về phiên bản Vulkan 1.0.0 mặc định.\n");
    *pApiVersion = VK_API_VERSION_1_0;
    
    return VK_SUCCESS;
}

//debug
// Wrapper cho vkCreateDevice (Ghi log ra stderr)
/*
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice            physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    fprintf(stderr, "--> LYNXVK: Đã vào hàm vkCreateDevice\n");

    // --- BẮT ĐẦU PHẦN GHI LOG CHI TIẾT ---
    if (pCreateInfo) {
        fprintf(stderr, "  [INFO] Phân tích VkDeviceCreateInfo...\n");
        fprintf(stderr, "  |-- Yêu cầu %u hàng đợi (queue families).\n", pCreateInfo->queueCreateInfoCount);
        fprintf(stderr, "  |-- Kích hoạt %u extension cấp thiết bị:\n", pCreateInfo->enabledExtensionCount);
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            fprintf(stderr, "  |   - %s\n", pCreateInfo->ppEnabledExtensionNames[i]);
        }

        // Đây là phần quan trọng nhất để gỡ lỗi res=-8
        if (pCreateInfo->pEnabledFeatures) {
            const VkPhysicalDeviceFeatures* features = pCreateInfo->pEnabledFeatures;
            fprintf(stderr, "  |-- [YÊU CẦU TÍNH NĂNG TỪ ỨNG DỤNG] --\n");
            fprintf(stderr, "  |   - robustBufferAccess: %s\n", features->robustBufferAccess ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - fullDrawIndexUint32: %s\n", features->fullDrawIndexUint32 ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - imageCubeArray: %s\n", features->imageCubeArray ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - independentBlend: %s\n", features->independentBlend ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - geometryShader: %s\n", features->geometryShader ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - tessellationShader: %s\n", features->tessellationShader ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sampleRateShading: %s\n", features->sampleRateShading ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - dualSrcBlend: %s\n", features->dualSrcBlend ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - logicOp: %s\n", features->logicOp ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - multiDrawIndirect: %s\n", features->multiDrawIndirect ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - drawIndirectFirstInstance: %s\n", features->drawIndirectFirstInstance ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - depthClamp: %s\n", features->depthClamp ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - depthBiasClamp: %s\n", features->depthBiasClamp ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - fillModeNonSolid: %s\n", features->fillModeNonSolid ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - depthBounds: %s\n", features->depthBounds ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - wideLines: %s\n", features->wideLines ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - largePoints: %s\n", features->largePoints ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - alphaToOne: %s\n", features->alphaToOne ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - multiViewport: %s\n", features->multiViewport ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - samplerAnisotropy: %s\n", features->samplerAnisotropy ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - textureCompressionETC2: %s\n", features->textureCompressionETC2 ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - textureCompressionASTC_LDR: %s\n", features->textureCompressionASTC_LDR ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - textureCompressionBC: %s\n", features->textureCompressionBC ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - occlusionQueryPrecise: %s\n", features->occlusionQueryPrecise ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - pipelineStatisticsQuery: %s\n", features->pipelineStatisticsQuery ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - vertexPipelineStoresAndAtomics: %s\n", features->vertexPipelineStoresAndAtomics ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - fragmentStoresAndAtomics: %s\n", features->fragmentStoresAndAtomics ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderTessellationAndGeometryPointSize: %s\n", features->shaderTessellationAndGeometryPointSize ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderImageGatherExtended: %s\n", features->shaderImageGatherExtended ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageImageExtendedFormats: %s\n", features->shaderStorageImageExtendedFormats ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageImageMultisample: %s\n", features->shaderStorageImageMultisample ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageImageReadWithoutFormat: %s\n", features->shaderStorageImageReadWithoutFormat ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageImageWriteWithoutFormat: %s\n", features->shaderStorageImageWriteWithoutFormat ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderUniformBufferArrayDynamicIndexing: %s\n", features->shaderUniformBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderSampledImageArrayDynamicIndexing: %s\n", features->shaderSampledImageArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageBufferArrayDynamicIndexing: %s\n", features->shaderStorageBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderStorageImageArrayDynamicIndexing: %s\n", features->shaderStorageImageArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderClipDistance: %s\n", features->shaderClipDistance ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderCullDistance: %s\n", features->shaderCullDistance ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderFloat64: %s\n", features->shaderFloat64 ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderInt64: %s\n", features->shaderInt64 ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderInt16: %s\n", features->shaderInt16 ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderResourceResidency: %s\n", features->shaderResourceResidency ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - shaderResourceMinLod: %s\n", features->shaderResourceMinLod ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseBinding: %s\n", features->sparseBinding ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidencyBuffer: %s\n", features->sparseResidencyBuffer ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidencyImage2D: %s\n", features->sparseResidencyImage2D ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidencyImage3D: %s\n", features->sparseResidencyImage3D ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidency2Samples: %s\n", features->sparseResidency2Samples ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidency4Samples: %s\n", features->sparseResidency4Samples ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidency8Samples: %s\n", features->sparseResidency8Samples ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidency16Samples: %s\n", features->sparseResidency16Samples ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - sparseResidencyAliased: %s\n", features->sparseResidencyAliased ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - variableMultisampleRate: %s\n", features->variableMultisampleRate ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |   - inheritedQueries: %s\n", features->inheritedQueries ? "VK_TRUE" : "VK_FALSE");
            fprintf(stderr, "  |-- [KẾT THÚC DANH SÁCH TÍNH NĂNG] --\n");
        }
    } else {
        fprintf(stderr, "  [WARNING] pCreateInfo là NULL.\n");
    }
    // --- KẾT THÚC PHẦN GHI LOG CHI TIẾT ---

    // Lấy con trỏ hàm gốc
    PFN_vkCreateDevice vkCreateDevice_real = (PFN_vkCreateDevice)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateDevice");

    if (!vkCreateDevice_real) {
        fprintf(stderr, "  [ERROR] Không thể lấy con trỏ hàm vkCreateDevice gốc!\n");
        fprintf(stderr, "<-- LYNXVK: Rời khỏi hàm vkCreateDevice với kết quả: %d (VK_ERROR_INITIALIZATION_FAILED)\n", VK_ERROR_INITIALIZATION_FAILED);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm gốc
    VkResult result = vkCreateDevice_real(physicalDevice, pCreateInfo, pAllocator, pDevice);

    fprintf(stderr, "<-- LYNXVK: Rời khỏi hàm vkCreateDevice với kết quả: %d\n", result);
    return result;
}
*/

// Wrapper cho vkGetPhysicalDeviceFeatures2 (Bật tính năng giả mạo)

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice            physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures)
{
    fprintf(stderr, "--> LYNXVK: Đã chặn vkGetPhysicalDeviceFeatures2.\n");

    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2_real =
        (PFN_vkGetPhysicalDeviceFeatures2)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetPhysicalDeviceFeatures2");
    
    if (!vkGetPhysicalDeviceFeatures2_real) {
        fprintf(stderr, "  [ERROR] LYNXVK: Không thể lấy con trỏ hàm vkGetPhysicalDeviceFeatures2 gốc.\n");
        return;
    }

    // **QUAN TRỌNG**: Gọi hàm gốc trước tiên!
    // Điều này sẽ điền vào tất cả các tính năng *thực sự* được hỗ trợ.
    vkGetPhysicalDeviceFeatures2_real(physicalDevice, pFeatures);

    fprintf(stderr, "  |-- Bắt đầu tìm kiếm và bật các tính năng giả mạo...\n");

    // Bắt đầu duyệt chuỗi pNext để tìm các cấu trúc tính năng mở rộng
    void* pNext = pFeatures->pNext;
    while (pNext != NULL) {
        // Lấy sType để xác định loại cấu trúc
        VkStructureType sType = *(VkStructureType*)pNext;

        // Tìm cấu trúc cho VK_EXT_robustness2
        if (sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT) {
            fprintf(stderr, "  |-- Đã tìm thấy VkPhysicalDeviceRobustness2FeaturesEXT. Bật các tính năng.\n");
            // Ép kiểu con trỏ sang đúng loại
            VkPhysicalDeviceRobustness2FeaturesEXT* pRobustness2Features = (VkPhysicalDeviceRobustness2FeaturesEXT*)pNext;
            
            // BẬT TÍNH NĂNG GIẢ MẠO
            pRobustness2Features->robustBufferAccess2 = VK_TRUE;
            pRobustness2Features->robustImageAccess2 = VK_TRUE;
            pRobustness2Features->nullDescriptor = VK_TRUE;
        }
        
        // Tiếp tục duyệt đến cấu trúc tiếp theo trong chuỗi
        pNext = (*(VkBaseOutStructure**)pNext)->pNext;
    }
}

// Wrapper cho vkCreateDevice (Can thiệp và Sửa đổi)
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice            physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    fprintf(stderr, "--> LYNXVK: Đã vào hàm vkCreateDevice (Chế độ Can thiệp)\n");

    // Chúng ta không thể sửa đổi pCreateInfo trực tiếp vì nó là const.
    // Vì vậy, chúng ta tạo một bản sao cục bộ để có thể thay đổi.
    VkDeviceCreateInfo modifiedCreateInfo = *pCreateInfo;
    VkPhysicalDeviceFeatures modifiedFeatures;

    // Chỉ thực hiện sửa đổi nếu pEnabledFeatures tồn tại.
    if (pCreateInfo->pEnabledFeatures) {
        // Tạo một bản sao của các tính năng để có thể sửa đổi chúng.
        modifiedFeatures = *pCreateInfo->pEnabledFeatures;

        fprintf(stderr, "  [INFO] Bắt đầu can thiệp vào các yêu cầu tính năng...\n");

        // --- BẮT ĐẦU CUỘC "PHẪU THUẬT" ---
        // Dựa trên log, chúng ta biết rằng driver không hỗ trợ một số tính năng này.
        // Chúng ta sẽ tắt chúng đi trong bản sao của mình.

        if (modifiedFeatures.geometryShader) {
            fprintf(stderr, "  [CAN THIỆP] Tắt yêu cầu 'geometryShader'.\n");
            modifiedFeatures.geometryShader = VK_FALSE;
        }

        if (modifiedFeatures.textureCompressionBC) {
            fprintf(stderr, "  [CAN THIỆP] Tắt yêu cầu 'textureCompressionBC'.\n");
            modifiedFeatures.textureCompressionBC = VK_FALSE;
        }
        // Bạn có thể thêm các if khác ở đây để tắt các tính năng khác nếu cần
        // ví dụ:
        // if (modifiedFeatures.robustBufferAccess) {
        //     fprintf(stderr, "  [CAN THIỆP] Tắt yêu cầu 'robustBufferAccess'.\n");
        //     modifiedFeatures.robustBufferAccess = VK_FALSE;
        // }


        // --- KẾT THÚC CUỘC "PHẪU THUẬT" ---

        // Bây giờ, trỏ cấu trúc thông tin tạo thiết bị của chúng ta đến
        // "bản danh sách yêu cầu" đã được sửa đổi.
        modifiedCreateInfo.pEnabledFeatures = &modifiedFeatures;
    }

    // Lấy con trỏ hàm gốc
    PFN_vkCreateDevice vkCreateDevice_real = (PFN_vkCreateDevice)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateDevice");

    if (!vkCreateDevice_real) {
        fprintf(stderr, "  [ERROR] Không thể lấy con trỏ hàm vkCreateDevice gốc!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    fprintf(stderr, "  [INFO] Gọi hàm vkCreateDevice gốc với các tính năng đã được sửa đổi...\n");

    // Gọi hàm gốc, nhưng với cấu trúc thông tin ĐÃ ĐƯỢC SỬA ĐỔI của chúng ta.
    g_vkDevice = *pDevice;
    VkResult result = vkCreateDevice_real(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

    fprintf(stderr, "<-- LYNXVK: Rời khỏi hàm vkCreateDevice với kết quả: %d\n", result);
    return result;
}



/*
 * vkGetDeviceQueue
 * ----------------
 * Chặn lời gọi để lấy một handle tới hàng đợi của thiết bị (device queue).
 * Hàm này được gọi sau khi vkCreateDevice thành công.
 * Đây là một "wrapper truyền qua" đơn giản, chủ yếu dùng để ghi log.
 * Chúng ta không cần ánh xạ các handle ở đây.
 */
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice         device,
    uint32_t         queueFamilyIndex,
    uint32_t         queueIndex,
    VkQueue* pQueue)
{
    printf("LynxVK: Intercepted vkGetDeviceQueue for queue family %u, index %u.\n",
           queueFamilyIndex, queueIndex);

    // Lấy con trỏ hàm thật từ driver.
    // Mặc dù đây là hàm cấp thiết bị, nó vẫn có thể được lấy qua vkGetInstanceProcAddr
    PFN_vkGetDeviceQueue vkGetDeviceQueue_real =
        (PFN_vkGetDeviceQueue)g_vkGetInstanceProcAddr_real(g_last_instance, "vkGetDeviceQueue");

    if (!vkGetDeviceQueue_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkGetDeviceQueue function pointer.\n");
        // Vì hàm này là void, chúng ta không thể trả về lỗi.
        // Ít nhất chúng ta nên đảm bảo không làm crash ứng dụng.
        if (pQueue) {
            *pQueue = VK_NULL_HANDLE;
        }
        return;
    }

    // Gọi hàm thật với các tham số gốc.
    // Driver sẽ điền handle queue thật vào pQueue.
    vkGetDeviceQueue_real(device, queueFamilyIndex, queueIndex, pQueue);

    printf("LynxVK: --> Real vkGetDeviceQueue called. Queue handle obtained.\n");
}

/*
 * vkCreateImageView
 * -----------------
 * Chặn lời gọi để tạo một "khung nhìn ảnh" (image view) vào một ảnh có sẵn.
 * Hàm này được gọi cho mỗi ảnh trong swapchain.
 *
 * Vì chúng ta đã đưa thẳng các handle VkImage thật cho ứng dụng ở bước
 * vkGetSwapchainImagesKHR, nên chúng ta không cần thực hiện bất kỳ phép ánh xạ nào ở đây.
 * Đây là một "wrapper truyền qua" đơn giản để ghi log và hoàn thiện API.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice                     device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView)
{
    printf("LynxVK: Đã chặn vkCreateImageView.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkCreateImageView vkCreateImageView_real =
        (PFN_vkCreateImageView)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateImageView");

    if (!vkCreateImageView_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCreateImageView gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Vì pCreateInfo->image đã là handle thật, chúng ta có thể gọi thẳng hàm gốc
    // mà không cần sửa đổi gì.
    printf("LynxVK: --> Gọi đến vkCreateImageView gốc...\n");
    return vkCreateImageView_real(device, pCreateInfo, pAllocator, pView);
}

/*
 * vkCreateRenderPass
 * ------------------
 * Chặn lời gọi để tạo một "render pass", thứ định nghĩa cấu trúc của một
 * thao tác render (ví dụ: có bao nhiêu attachment, chúng được xử lý như thế nào).
 *
 * Đây là một "wrapper truyền qua" đơn giản. Struct VkRenderPassCreateInfo
 * không chứa bất kỳ handle nào cần được ánh xạ. Chúng ta chỉ cần ghi log
 * và gọi hàm gốc.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice                     device,
    const VkRenderPassCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkRenderPass* pRenderPass)
{
    printf("LynxVK: Đã chặn vkCreateRenderPass.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkCreateRenderPass vkCreateRenderPass_real =
        (PFN_vkCreateRenderPass)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateRenderPass");

    if (!vkCreateRenderPass_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCreateRenderPass gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với các tham số gốc.
    printf("LynxVK: --> Gọi đến vkCreateRenderPass gốc...\n");
    return vkCreateRenderPass_real(device, pCreateInfo, pAllocator, pRenderPass);
}

/*
 * vkCreateFramebuffer
 * -------------------
 * Intercepts the call to create a framebuffer.
 * A framebuffer binds a RenderPass ("the recipe") with a set of
 * ImageViews ("the ingredients") to create a concrete render target.
 *
 * Like the previous functions, this is a simple "pass-through wrapper".
 * The VkFramebufferCreateInfo struct contains handles (renderPass, pAttachments)
 * that we have already passed through to the application, so no mapping is needed.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice                     device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer)
{
    printf("LynxVK: Intercepted vkCreateFramebuffer.\n");

    // Get the real function pointer from the driver.
    PFN_vkCreateFramebuffer vkCreateFramebuffer_real =
        (PFN_vkCreateFramebuffer)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateFramebuffer");

    if (!vkCreateFramebuffer_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkCreateFramebuffer function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the real function with the original parameters.
    printf("LynxVK: --> Calling real vkCreateFramebuffer...\n");
    return vkCreateFramebuffer_real(device, pCreateInfo, pAllocator, pFramebuffer);
}

/*
 * vkCreateCommandPool
 * -------------------
 * Chặn lời gọi để tạo một "command pool". Command pool quản lý bộ nhớ
 * được sử dụng để cấp phát các command buffer.
 *
 * Đây là một "wrapper truyền qua" đơn giản vì VkCommandPoolCreateInfo
 * không chứa handle nào cần ánh xạ.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice                     device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool)
{
    printf("LynxVK: Đã chặn vkCreateCommandPool.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkCreateCommandPool vkCreateCommandPool_real =
        (PFN_vkCreateCommandPool)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateCommandPool");

    if (!vkCreateCommandPool_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCreateCommandPool gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật.
    printf("LynxVK: --> Gọi đến vkCreateCommandPool gốc...\n");
    return vkCreateCommandPool_real(device, pCreateInfo, pAllocator, pCommandPool);
}

/*
 * vkAllocateCommandBuffers
 * --------------------------
 * Chặn lời gọi để cấp phát các "command buffer" từ một command pool có sẵn.
 * Command buffer là nơi các lệnh vẽ thực sự được ghi vào.
 *
 * Đây cũng là một "wrapper truyền qua" đơn giản. Các handle đầu vào (device, commandPool)
 * đều là handle thật mà chúng ta đã truyền qua ở các bước trước.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice                       device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers)
{
    printf("LynxVK: Đã chặn vkAllocateCommandBuffers.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers_real =
        (PFN_vkAllocateCommandBuffers)g_vkGetInstanceProcAddr_real(g_last_instance, "vkAllocateCommandBuffers");

    if (!vkAllocateCommandBuffers_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkAllocateCommandBuffers gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật.
    printf("LynxVK: --> Gọi đến vkAllocateCommandBuffers gốc...\n");
    return vkAllocateCommandBuffers_real(device, pAllocateInfo, pCommandBuffers);
}

/*
 * vkBeginCommandBuffer
 * --------------------
 * Chặn lời gọi để bắt đầu ghi một command buffer.
 * Hành động này đặt command buffer vào trạng thái "đang ghi", sẵn sàng
 * nhận các lệnh vẽ (vkCmd...).
 *
 * Đây là một "wrapper truyền qua" đơn giản. Handle VkCommandBuffer đã là
 * handle thật sự từ driver, vì vậy không cần ánh xạ.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer                  commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo)
{
    // Chú thích lại dòng này vì nó được gọi mỗi frame và có thể gây spam console.
    // Bỏ chú thích khi cần gỡ lỗi.
    // printf("LynxVK: Đã chặn vkBeginCommandBuffer.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer_real =
        (PFN_vkBeginCommandBuffer)g_vkGetInstanceProcAddr_real(g_last_instance, "vkBeginCommandBuffer");

    if (!vkBeginCommandBuffer_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkBeginCommandBuffer gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với các tham số gốc.
    return vkBeginCommandBuffer_real(commandBuffer, pBeginInfo);
}

/*
 * vkCmdBeginRenderPass
 * --------------------
 * Chặn lệnh để bắt đầu một render pass. Lệnh này là một trong những lệnh
 * đầu tiên được ghi vào command buffer trong mỗi khung hình. Nó thiết lập
 * các mục tiêu render (framebuffer) và cách chúng sẽ được sử dụng (render pass).
 *
 * Đây là một "wrapper truyền qua" đơn giản vì tất cả các handle bên trong
 * pRenderPassBeginInfo (renderPass, framebuffer) đều là handle thật.
 */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer              commandBuffer,
    const VkRenderPassBeginInfo* pRenderPassBeginInfo,
    VkSubpassContents            contents)
{
    // Chú thích lại dòng này vì nó được gọi mỗi frame và sẽ gây spam console.
    // Bỏ chú thích khi cần gỡ lỗi.
    // printf("LynxVK: Đã chặn vkCmdBeginRenderPass.\n");

    // Lấy con trỏ hàm thật từ driver.
    // Lưu ý: Các hàm vkCmd... thường được lấy qua vkGetDeviceProcAddr,
    // nhưng việc lấy qua vkGetInstanceProcAddr cho các hàm core vẫn hoạt động.
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass_real =
        (PFN_vkCmdBeginRenderPass)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCmdBeginRenderPass");

    if (!vkCmdBeginRenderPass_real) {
        // In lỗi ra một lần và tránh spam.
        static bool error_printed = false;
        if (!error_printed) {
            fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCmdBeginRenderPass gốc.\n");
            error_printed = true;
        }
        return;
    }

    // Gọi hàm thật với các tham số gốc.
    vkCmdBeginRenderPass_real(commandBuffer, pRenderPassBeginInfo, contents);
}

/*
 * vkCmdEndRenderPass
 * ------------------
 * Chặn lệnh để kết thúc một render pass hiện tại. Lệnh này phải được gọi
 * sau một vkCmdBeginRenderPass tương ứng.
 *
 * Đây là một "wrapper truyền qua" đơn giản. Handle VkCommandBuffer đã là
 * handle thật sự từ driver.
 */
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(
    VkCommandBuffer commandBuffer)
{
    // Chú thích lại dòng này vì nó được gọi mỗi frame và sẽ gây spam console.
    // Bỏ chú thích khi cần gỡ lỗi.
    // printf("LynxVK: Đã chặn vkCmdEndRenderPass.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass_real =
        (PFN_vkCmdEndRenderPass)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCmdEndRenderPass");

    if (!vkCmdEndRenderPass_real) {
        // In lỗi ra một lần và tránh spam.
        static bool error_printed = false;
        if (!error_printed) {
            fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCmdEndRenderPass gốc.\n");
            error_printed = true;
        }
        return;
    }

    // Gọi hàm thật với các tham số gốc.
    vkCmdEndRenderPass_real(commandBuffer);
}
/*
 * vkCreateSemaphore
 * -----------------
 * Chặn lời gọi để tạo một đối tượng Semaphore. Semaphore là một công cụ
 * đồng bộ hóa GPU-GPU, dùng để ra hiệu giữa các hoạt động trong hàng đợi
 * (ví dụ: ra hiệu rằng việc lấy ảnh đã xong để việc vẽ có thể bắt đầu).
 *
 * Đây là một "wrapper truyền qua" đơn giản vì nó được tạo trên một
 * VkDevice thật và không chứa thông tin cần ánh xạ.
 */
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice                         device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore)
{
    printf("LynxVK: Đã chặn vkCreateSemaphore.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkCreateSemaphore vkCreateSemaphore_real =
        (PFN_vkCreateSemaphore)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateSemaphore");

    if (!vkCreateSemaphore_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkCreateSemaphore gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với các tham số gốc.
    return vkCreateSemaphore_real(device, pCreateInfo, pAllocator, pSemaphore);
}

// Wrapper cho vkAllocateMemory
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory)
{
    if (!g_vkAllocateMemory_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkAllocateMemory gốc là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_vkAllocateMemory_real(device, pAllocateInfo, pAllocator, pMemory);

    if (result == VK_SUCCESS) {
        VkDeviceSize size = pAllocateInfo->allocationSize;
        g_total_allocated_memory += size;
        g_memory_allocation_map[*pMemory] = size;

        fprintf(stderr, "[MEMORY] Cấp phát: %llu bytes. Tổng cộng: %llu bytes (%f MB)\n",
                (unsigned long long)size,
                (unsigned long long)g_total_allocated_memory,
                g_total_allocated_memory / (1024.0 * 1024.0));
    } else {
        fprintf(stderr, "[MEMORY] Lỗi cấp phát bộ nhớ! Kết quả: %s\n", VulkanResultToString(result));
    }

    return result;
}

// Wrapper cho vkFreeMemory
extern "C" VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator)
{
    if (memory != VK_NULL_HANDLE) {
        auto it = g_memory_allocation_map.find(memory);
        if (it != g_memory_allocation_map.end()) {
            VkDeviceSize size = it->second;
            g_total_allocated_memory -= size;
            g_memory_allocation_map.erase(it);

            fprintf(stderr, "[MEMORY] Giải phóng: %llu bytes. Tổng cộng: %llu bytes (%f MB)\n",
                    (unsigned long long)size,
                    (unsigned long long)g_total_allocated_memory,
                    g_total_allocated_memory / (1024.0 * 1024.0));
        } else {
            fprintf(stderr, "[MEMORY] Cảnh báo: Cố gắng giải phóng một vùng nhớ không được theo dõi!\n");
        }
    }

    if (!g_vkFreeMemory_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkFreeMemory gốc là NULL!\n");
        return;
    }
    g_vkFreeMemory_real(device, memory, pAllocator);
}

// Wrapper cho vkMapMemory
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice            device,
    VkDeviceMemory      memory,
    VkDeviceSize        offset,
    VkDeviceSize        size,
    VkMemoryMapFlags    flags,
    void** ppData)
{
    fprintf(stderr, "[KHUNG HÌNH %llu] vkMapMemory:\n", (unsigned long long)g_frame_counter);
    fprintf(stderr, "  |-- Ánh xạ bộ nhớ: %p\n", (void*)memory);
    fprintf(stderr, "  |-- Kích thước      : %llu bytes\n", (unsigned long long)size);

    if (!g_vkMapMemory_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkMapMemory gốc là NULL!\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_vkMapMemory_real(device, memory, offset, size, flags, ppData);

    fprintf(stderr, "  |-- Con trỏ dữ liệu trả về: %p\n", (result == VK_SUCCESS ? *ppData : NULL));
    fprintf(stderr, "  |-- TRẢ VỀ: %s (%d)\n", VulkanResultToString(result), result);
    return result;
}

// Wrapper cho vkUnmapMemory (Ghi log ra stderr)
extern "C" VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice            device,
    VkDeviceMemory      memory)
{
    fprintf(stderr, "[KHUNG HÌNH %llu] vkUnmapMemory:\n", (unsigned long long)g_frame_counter);
    fprintf(stderr, "  |-- Hủy ánh xạ bộ nhớ: %p\n", (void*)memory);

    if (!g_vkUnmapMemory_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Con trỏ hàm vkUnmapMemory gốc là NULL!\n");
        return;
    }

    g_vkUnmapMemory_real(device, memory);
}
