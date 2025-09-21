#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <native_surface/extern_function.h>
#include "lynxvk.h"
#include <map>
// --- GLOBAL VARIABLES ---

ANativeWindow* g_native_window = NULL;
static void* g_vulkan_library_handle = NULL;
static PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr_real = NULL;
static VkInstance g_last_instance = VK_NULL_HANDLE;
static PFN_vkEnumerateInstanceExtensionProperties g_vkEnumerateInstanceExtensionProperties_real = NULL;
static PFN_vkCreateInstance g_vkCreateInstance_real = NULL;
static PFN_vkGetDeviceProcAddr g_vkGetDeviceProcAddr_real = nullptr;
static std::map<VkSurfaceKHR, VkSurfaceKHR> g_surface_map;
static std::map<VkSwapchainKHR, VkSwapchainKHR> g_swapchain_map;
static PFN_vkEnumerateInstanceVersion g_vkEnumerateInstanceVersion_real = nullptr;

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
  // ... các else if khác ...

    // --- PASS-THROUGH LOGIC ---
    // If the function is not one of the ones we're wrapping,
    // just call the real vkGetInstanceProcAddr and return its result.

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
 * vkQueueSubmit
 * -------------
 * Chặn lời gọi để gửi một lô công việc (command buffer) cho GPU.
 *
 * Đây là một hàm cực kỳ quan trọng để gỡ lỗi. Bằng cách ghi log ở đây,
 * chúng ta có thể biết được liệu command buffer gây lỗi trong vkEndCommandBuffer
 * có bao giờ được gửi đi hay không.
 */

/*extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue                 queue,
    uint32_t                submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence                 fence)
{
    LYNXVK_LOG("--> LYNXVK: Đã chặn vkQueueSubmit. Số lượng submit: %u\n", submitCount);

    // Ghi log chi tiết về các command buffer được gửi đi
    if (pSubmits) {
        for (uint32_t i = 0; i < submitCount; ++i) {
            LYNXVK_LOG("    LYNXVK: Submit #%u có %u command buffer.\n", i, pSubmits[i].commandBufferCount);
            for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; ++j) {
                LYNXVK_LOG("        LYNXVK: CommandBuffer[%u] = %p\n", j, (void*)pSubmits[i].pCommandBuffers[j]);
            }
        }
    }

    // Lấy con trỏ hàm gốc từ driver
    PFN_vkQueueSubmit vkQueueSubmit_real =
        (PFN_vkQueueSubmit)g_vkGetDeviceProcAddr_real(g_vkGetDeviceProcAddr_real ? (VkDevice)0 : (VkDevice)g_last_instance, "vkQueueSubmit");

    if (!vkQueueSubmit_real) {
        // Cần một cách thông minh hơn để lấy device handle, tạm thời dùng một cách hack
        // Vì g_vkGetDeviceProcAddr_real có thể là null, chúng ta cần một device hợp lệ
        // Lấy device từ queue là một cách, nhưng phức tạp. Tạm thời dùng cách đơn giản.
        // Đây là một giả định: hầu hết các ứng dụng sẽ lấy hàm này từ g_last_instance
         vkQueueSubmit_real = (PFN_vkQueueSubmit)g_vkGetInstanceProcAddr_real(g_last_instance, "vkQueueSubmit");
         if(!vkQueueSubmit_real) {
              fprintf(stderr, "LYNXVK CRITICAL: Không thể lấy con trỏ hàm vkQueueSubmit gốc!\n");
              return VK_ERROR_INITIALIZATION_FAILED;
         }
    }

    // Gọi hàm gốc
    VkResult result = vkQueueSubmit_real(queue, submitCount, pSubmits, fence);
    LYNXVK_LOG("<-- LYNXVK: Rời khỏi vkQueueSubmit với kết quả: %d\n", result);
    return result;
}
*/

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



// --- WRAPPER FUNCTIONS ---

/*
 * vkEnumerateInstanceExtensionProperties
 * --------------------------------------
 * NEW: This wrapper intercepts the query for available extensions.
 * Its job is to add fake X11-related extensions to the list returned
 * by the real Android driver. This is crucial for compatibility.
 */

/*
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    printf("LynxVK: Intercepted vkEnumerateInstanceExtensionProperties.\n");

    if (g_vkEnumerateInstanceExtensionProperties_real == NULL) {
        fprintf(stderr, "LYNXVK ERROR: Real function for vkEnumerateInstanceExtensionProperties is NULL.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // This function can be called in two ways:
    // 1. With pProperties = NULL, to get the total count of extensions.
    // 2. With pProperties != NULL, to get the actual extension data.
    // We must handle both cases correctly.

    // First, get the count and properties from the real driver.
    uint32_t real_property_count = 0;
    VkResult result = g_vkEnumerateInstanceExtensionProperties_real(pLayerName, &real_property_count, NULL);
    if (result != VK_SUCCESS) {
        return result;
    }

    // The two extensions we are faking
    const int FAKE_EXTENSION_COUNT = 2;
    VkExtensionProperties fake_extensions[FAKE_EXTENSION_COUNT];
    strcpy(fake_extensions[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
    fake_extensions[0].specVersion = VK_KHR_SURFACE_SPEC_VERSION;
    strcpy(fake_extensions[1].extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    fake_extensions[1].specVersion = VK_KHR_XLIB_SURFACE_SPEC_VERSION;


    // Case 1: The application is just asking for the count.
    if (pProperties == NULL) {
        *pPropertyCount = real_property_count + FAKE_EXTENSION_COUNT;
        printf("LynxVK: Faking X11 support. Reporting %u total extensions.\n", *pPropertyCount);
        return VK_SUCCESS;
    }

    // Case 2: The application wants the actual data.
    printf("LynxVK: Populating extension list.\n");

    // We call the real function again to fill the user's buffer up to the real count.
    result = g_vkEnumerateInstanceExtensionProperties_real(pLayerName, pPropertyCount, pProperties);

    // If the buffer was too small for the real extensions, the driver returns VK_INCOMPLETE.
    // We should respect that and not add our fake extensions.
    if (result == VK_INCOMPLETE) {
        return VK_INCOMPLETE;
    }

    uint32_t provided_capacity = *pPropertyCount;
    uint32_t copied_count = real_property_count;

    // Now, add our fake extensions if there is space in the user's buffer.
    if (copied_count < provided_capacity) {
        pProperties[copied_count] = fake_extensions[0]; // VK_KHR_surface
        copied_count++;
    }
    if (copied_count < provided_capacity) {
        pProperties[copied_count] = fake_extensions[1]; // VK_KHR_xlib_surface
        copied_count++;
    }

    // Finally, update the count and return status according to the Vulkan specification.
    *pPropertyCount = copied_count;

    // If we couldn't fit all extensions (real + fake) into the provided buffer,
    // we must return VK_INCOMPLETE.
    if (copied_count < real_property_count + FAKE_EXTENSION_COUNT) {
        return VK_INCOMPLETE;
    }

    return VK_SUCCESS;
}

*/

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

/*
 * vkAcquireNextImageKHR
 * ---------------------
 * Chặn lời gọi để lấy chỉ số của tấm ảnh tiếp theo có sẵn trong swapchain để vẽ.
 * Đây là một trong những hàm quan trọng nhất, được gọi một lần mỗi frame.
 * 1. Nhận handle swapchain từ ứng dụng.
 * 2. Tra cứu handle này trong map `g_swapchain_map` để lấy ra handle thật sự.
 * 3. Gọi hàm gốc của driver với handle thật đó.
 * 4. Các tham số khác như semaphore, fence, và pImageIndex có thể được chuyển thẳng qua.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice            device,
    VkSwapchainKHR      swapchain,
    uint64_t            timeout,
    VkSemaphore         semaphore,
    VkFence             fence,
    uint32_t* pImageIndex)
{
    // Chúng ta không in log ở đây vì hàm này được gọi mỗi frame, sẽ gây spam console.
    // Chỉ bật log này khi cần gỡ lỗi.
    // printf("LynxVK: Đã chặn vkAcquireNextImageKHR.\n");

    // Tra cứu handle swapchain được cung cấp trong map của chúng ta.
    auto it = g_swapchain_map.find(swapchain);
    if (it == g_swapchain_map.end()) {
        // Lỗi này nghiêm trọng vì nó xảy ra trong vòng lặp render.
        fprintf(stderr, "LYNXVK ERROR: vkAcquireNextImageKHR được gọi với một swapchain handle không xác định.\n");
        return VK_ERROR_OUT_OF_DATE_KHR; // Một lỗi hợp lý để báo cho ứng dụng tạo lại swapchain.
    }

    // Nếu tìm thấy, lấy ra handle thật.
    VkSwapchainKHR real_swapchain = it->second;

    // Lấy con trỏ hàm thật từ driver
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_real =
        (PFN_vkAcquireNextImageKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkAcquireNextImageKHR");

    if (!vkAcquireNextImageKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkAcquireNextImageKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với handle swapchain thật
    // Các tham số còn lại được truyền thẳng qua.
    return vkAcquireNextImageKHR_real(device, real_swapchain, timeout, semaphore, fence, pImageIndex);
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
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue                 queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    // Giống như vkAcquireNextImageKHR, hàm này được gọi mỗi frame.
    // Tắt log để tránh spam.
    // printf("LynxVK: Đã chặn vkQueuePresentKHR.\n");

    if (!pPresentInfo) {
        fprintf(stderr, "LYNXVK ERROR: pPresentInfo trong vkQueuePresentKHR là NULL.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // -- Bước 1: Chuẩn bị để sửa đổi --
    // Tạo một vector để chứa các handle swapchain thật. std::vector an toàn hơn mảng C.
    std::vector<VkSwapchainKHR> realSwapchains(pPresentInfo->swapchainCount);

    // -- Bước 2: Lặp và tra cứu --
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR app_swapchain = pPresentInfo->pSwapchains[i];
        auto it = g_swapchain_map.find(app_swapchain);

        if (it == g_swapchain_map.end()) {
            fprintf(stderr, "LYNXVK ERROR: vkQueuePresentKHR được gọi với một swapchain handle không xác định.\n");
            // Không thể tiếp tục nếu một trong các swapchain không hợp lệ.
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        // Điền handle thật vào vector của chúng ta
        realSwapchains[i] = it->second;
    }

    // -- Bước 3: Tạo struct đã sửa đổi --
    // Sao chép toàn bộ struct gốc
    VkPresentInfoKHR modifiedPresentInfo = *pPresentInfo;
    // Và trỏ nó đến mảng các handle thật của chúng ta
    modifiedPresentInfo.pSwapchains = realSwapchains.data();


    // -- Bước 4: Gọi hàm gốc --
    PFN_vkQueuePresentKHR vkQueuePresentKHR_real =
        (PFN_vkQueuePresentKHR)g_vkGetInstanceProcAddr_real(g_last_instance, "vkQueuePresentKHR");

    if (!vkQueuePresentKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkQueuePresentKHR gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Gọi hàm thật với thông tin đã được sửa đổi hoàn toàn.
    return vkQueuePresentKHR_real(queue, &modifiedPresentInfo);
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


/*
 * vkCreateDevice
 * --------------
 * Intercepts the call to create a logical device (the main interface to the GPU).
 * This is primarily a pass-through wrapper for now, with detailed logging.
 * Its main purpose is to log which extensions the application is requesting at the device level,
 * which is crucial for debugging swapchain issues.
 *
 * It is exported directly to prevent potential dynamic linking errors.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice            physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    printf("LynxVK: Intercepted vkCreateDevice.\n");

    // Log the requested extensions, which is very useful for debugging.
    if (pCreateInfo) {
        printf("LynxVK: Application is requesting %u device extensions:\n", pCreateInfo->enabledExtensionCount);
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            printf("LynxVK:   - %s\n", pCreateInfo->ppEnabledExtensionNames[i]);
        }
    } else {
        fprintf(stderr, "LYNXVK WARNING: vkCreateDevice called with NULL pCreateInfo.\n");
    }


    // Get the real function pointer from the driver using the stored instance.
    // vkCreateDevice is an instance-level function pointer.
    PFN_vkCreateDevice vkCreateDevice_real =
        (PFN_vkCreateDevice)g_vkGetInstanceProcAddr_real(g_last_instance, "vkCreateDevice");

    if (!vkCreateDevice_real) {
        fprintf(stderr, "LYNXVK ERROR: Failed to get real vkCreateDevice function pointer.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the real function with the original, unmodified parameters.
    printf("LynxVK: --> Calling real vkCreateDevice...\n");
    VkResult result = vkCreateDevice_real(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (result == VK_SUCCESS) {
        printf("LynxVK: --> Real vkCreateDevice succeeded.\n");
    } else {
        fprintf(stderr, "LYNXVK ERROR: Real vkCreateDevice failed with result: %d\n", result);
    }

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
 * vkEndCommandBuffer
 * ------------------
 * Cung cấp (export) trực tiếp hàm này để giải quyết việc bị bỏ qua (bypass)
 * bởi cơ chế bảng điều phối (dispatch table) của Vulkan loader/Wine.
 *
 * Đây là một "wrapper truyền qua" đơn giản. Nó sẽ được gọi trực tiếp
 * bởi Wine sau khi bảng điều phối được tạo ra lúc vkCreateDevice.
 */
/*extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer             commandBuffer)
{
    printf("--> LYNXVK: ĐÃ CHẶN TRỰC TIẾP vkEndCommandBuffer cho commandBuffer %p\n", (void*)commandBuffer);

    // Lấy con trỏ hàm gốc từ driver
    // Chúng ta phải lấy nó qua GetInstanceProcAddr vì GetDeviceProcAddr có thể bị bỏ qua
    PFN_vkEndCommandBuffer vkEndCommandBuffer_real =
        (PFN_vkEndCommandBuffer)g_vkGetInstanceProcAddr_real(g_last_instance, "vkEndCommandBuffer");

    if (!vkEndCommandBuffer_real) {
        fprintf(stderr, "LYNXVK CRITICAL: Không thể lấy con trtrỏ hàm vkEndCommandBuffer gốc!\n");
        return VK_ERROR_INITIALIZATION_FAILED; // Trả về một lỗi nghiêm trọng
    }

    // Gọi hàm gốc
    VkResult result = vkEndCommandBuffer_real(commandBuffer);

    printf("<-- LYNXVK: Rời khỏi vkEndCommandBuffer với kết quả: %d\n", result);
    return result;
}

*/

/*
 * vkEndCommandBuffer
 * ------------------
 * Chặn lời gọi để kết thúc việc ghi một command buffer.
 * Sau lệnh này, command buffer được coi là hoàn chỉnh và sẵn sàng
 * để được gửi đến một hàng đợi (queue) để thực thi.
 *
 * Đây là một "wrapper truyền qua" đơn giản. Handle VkCommandBuffer đã là
 * handle thật sự từ driver.
 */
/*VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer)
{
    // Chú thích lại dòng này vì nó được gọi mỗi frame và sẽ gây spam console.
    // Bỏ chú thích khi cần gỡ lỗi.
    // printf("LynxVK: Đã chặn vkEndCommandBuffer.\n");

    // Lấy con trỏ hàm thật từ driver.
    PFN_vkEndCommandBuffer vkEndCommandBuffer_real =
        (PFN_vkEndCommandBuffer)g_vkGetInstanceProcAddr_real(g_last_instance, "vkEndCommandBuffer");

    if (!vkEndCommandBuffer_real) {
        fprintf(stderr, "LYNXVK ERROR: Không thể lấy con trỏ hàm vkEndCommandBuffer gốc.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    printf("vkEndCommandBuffer call...\n");
    // Gọi hàm thật với các tham số gốc.
    return vkEndCommandBuffer_real(commandBuffer);
}
*/
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

