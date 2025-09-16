#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <native_surface/extern_function.h>
#include "lynxvk.h"
// --- GLOBAL VARIABLES ---

ANativeWindow* g_native_window = NULL;
static void* g_vulkan_library_handle = NULL;
static PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr_real = NULL;
static VkInstance g_last_instance = VK_NULL_HANDLE;
static PFN_vkEnumerateInstanceExtensionProperties g_vkEnumerateInstanceExtensionProperties_real = NULL;
static PFN_vkCreateInstance g_vkCreateInstance_real = NULL;

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

    const char* vulkan_path = "system/lib64/libvulkan.so";
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
 * This wrapper intercepts the instance creation call. Its main purpose is to
 * filter the list of extensions requested by the application.
 * It removes the fake VK_KHR_xlib_surface extension and injects the real
 * VK_KHR_android_surface extension, which is required by the Android driver.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    printf("LynxVK: Intercepted vkCreateInstance.\n");

    if (g_vkCreateInstance_real == NULL) {
        fprintf(stderr, "LYNXVK ERROR: Real function for vkCreateInstance is NULL. Was it loaded in initialize?\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // We need to modify the requested extensions.
    // Create a new list on the stack. 64 slots should be more than enough.
    const char* modified_extensions[64];
    uint32_t modified_count = 0;
    int xlib_found = 0;

    printf("LynxVK: Filtering extensions for vkCreateInstance...\n");
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* current_ext = pCreateInfo->ppEnabledExtensionNames[i];

        // Check if the current extension is the one we want to replace.
        if (strcmp(current_ext, VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0) {
            // Found the Xlib extension, so we will filter it out (i.e., not copy it).
            printf("LynxVK: --> Filtering out '%s'.\n", VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
            xlib_found = 1;
        } else {
            // This is a different extension, so we keep it.
            if (modified_count < 64) {
                modified_extensions[modified_count++] = current_ext;
            } else {
                fprintf(stderr, "LYNXVK WARNING: Exceeded static buffer for extensions. Some may be dropped.\n");
            }
        }
    }

    // If the application originally requested the Xlib extension,
    // we must add the Android one in its place.
    if (xlib_found) {
        if (modified_count < 64) {
            printf("LynxVK: --> Injecting '%s'.\n", VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
            modified_extensions[modified_count++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
        }
    }

    // Now, create a copy of the VkInstanceCreateInfo struct on the stack.
    // We will pass this modified version to the real function.
    VkInstanceCreateInfo modified_create_info = *pCreateInfo;
    modified_create_info.ppEnabledExtensionNames = modified_extensions;
    modified_create_info.enabledExtensionCount = modified_count;

    printf("LynxVK: Calling real vkCreateInstance with modified extensions.\n");
    // Inside your vkCreateInstance wrapper, after the real function call succeeds:

    VkResult result = vkCreateInstance_real(pModifiedCreateInfo, pAllocator, pInstance);

    if (result == VK_SUCCESS) {
        printf("LynxVK: --> Real vkCreateInstance succeeded. Storing instance handle.\n");
        g_last_instance = *pInstance;
    }

    return g_vkCreateInstance_real(&modified_create_info, pAllocator, pInstance);
}

// --- WRAPPER FUNCTIONS ---

/*
 * vkEnumerateInstanceExtensionProperties
 * --------------------------------------
 * NEW: This wrapper intercepts the query for available extensions.
 * Its job is to add fake X11-related extensions to the list returned
 * by the real Android driver. This is crucial for compatibility.
 */
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


VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance                                  instance,
    const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface)
{
    printf("LynxVK: Intercepted vkCreateXlibSurfaceKHR. Redirecting to Android Surface...\n");

    if (!g_native_window) {
        fprintf(stderr, "LYNXVK ERROR: g_native_window is NULL. Make sure your window creation library has set it.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR_real =
        (PFN_vkCreateAndroidSurfaceKHR)g_vkGetInstanceProcAddr_real(instance, "vkCreateAndroidSurfaceKHR");

    if (!vkCreateAndroidSurfaceKHR_real) {
        fprintf(stderr, "LYNXVK ERROR: Could not get function pointer for 'vkCreateAndroidSurfaceKHR'. Is the extension enabled?\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkAndroidSurfaceCreateInfoKHR android_surface_info = {};
    android_surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    android_surface_info.window = g_native_window;

    printf("LynxVK: Calling real vkCreateAndroidSurfaceKHR with ANativeWindow = %p\n", (void*)g_native_window);
    VkResult result = vkCreateAndroidSurfaceKHR_real(instance, &android_surface_info, pAllocator, pSurface);

    if (result == VK_SUCCESS) {
        printf("LynxVK: Redirect successful! Android VkSurfaceKHR created.\n");
    } else {
        fprintf(stderr, "LYNXVK ERROR: Real vkCreateAndroidSurfaceKHR call failed with error code: %d\n", result);
    }

    return result;
}


VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance                    instance,
    VkSurfaceKHR                  surface,
    const VkAllocationCallbacks* pAllocator)
{
    if (!g_vkGetInstanceProcAddr_real) return;

    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR_real =
        (PFN_vkDestroySurfaceKHR)g_vkGetInstanceProcAddr_real(instance, "vkDestroySurfaceKHR");

    if (vkDestroySurfaceKHR_real) {
        printf("LynxVK: Intercepted vkDestroySurfaceKHR.\n");
        vkDestroySurfaceKHR_real(instance, surface, pAllocator);
    }
}


