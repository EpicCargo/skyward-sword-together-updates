// Real Vulkan host-access stress using the actual runtime queue gate; no game/window.
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue_guard.h"

static atomic_uint warnings, errors;
static pthread_barrier_t start;
static VkDevice device;
static VkQueue queue;
static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *message, void *user) {
    (void)type; (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) atomic_fetch_add(&errors, 1);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) atomic_fetch_add(&warnings, 1);
    fprintf(stderr, "%s\n", message->pMessage);
    return VK_FALSE;
}
static void *worker(void *argument) {
    uintptr_t kind = (uintptr_t)argument;
    pthread_barrier_wait(&start);
    for (unsigned i = 0; i < 1000; ++i) {
        VkResult result;
        if (kind == 0) {
            VkSubmitInfo2 submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            result = vk_queue_submit2(queue, 1, &submit, VK_NULL_HANDLE);
        } else if (kind == 1) result = vk_queue_wait_idle(device);
        else {
            // Match ImGui's legacy upload calls inside the shared gate.
            VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO};
            vk_queue_lock();
            result = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
            if (result == VK_SUCCESS) result = vkQueueWaitIdle(queue);
            vk_queue_unlock();
        }
        assert(result == VK_SUCCESS);
    }
    return NULL;
}
int main(void) {
    const char *layer = "VK_LAYER_KHRONOS_validation", *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo application = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo create = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&application,
        .enabledLayerCount=1, .ppEnabledLayerNames=&layer, .enabledExtensionCount=1, .ppEnabledExtensionNames=&extension};
    VkInstance instance;
    assert(vkCreateInstance(&create, NULL, &instance) == VK_SUCCESS);
    PFN_vkCreateDebugUtilsMessengerEXT make_debug = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT free_debug = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    assert(make_debug && free_debug);
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback=debug};
    VkDebugUtilsMessengerEXT messenger;
    assert(make_debug(instance, &debug_info, NULL, &messenger) == VK_SUCCESS);
    uint32_t count=0;
    assert(vkEnumeratePhysicalDevices(instance, &count, NULL) == VK_SUCCESS && count > 0 && count <= 16);
    VkPhysicalDevice physical[16];
    assert(vkEnumeratePhysicalDevices(instance, &count, physical) == VK_SUCCESS);
    uint32_t families=0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &families, NULL);
    assert(families > 0 && families <= 32);
    VkQueueFamilyProperties properties[32];
    vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &families, properties);
    uint32_t family=0;
    while (family < families && !(properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    assert(family < families);
    VkPhysicalDeviceVulkan13Features available = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext=&available};
    vkGetPhysicalDeviceFeatures2(physical[0], &features);
    assert(available.synchronization2);
    VkPhysicalDeviceVulkan13Features enabled = {.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2=VK_TRUE};
    float priority=1;
    VkDeviceQueueCreateInfo queue_info = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo device_info = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext=&enabled, .queueCreateInfoCount=1, .pQueueCreateInfos=&queue_info};
    assert(vkCreateDevice(physical[0], &device_info, NULL, &device) == VK_SUCCESS);
    vkGetDeviceQueue(device, family, 0, &queue);
    assert(!pthread_barrier_init(&start, NULL, 3));
    pthread_t threads[3];
    for (uintptr_t i=0; i<3; ++i) assert(!pthread_create(&threads[i], NULL, worker, (void *)i));
    for (unsigned i=0; i<3; ++i) assert(!pthread_join(threads[i], NULL));
    assert(!pthread_barrier_destroy(&start));
    assert(vk_queue_wait_idle(device) == VK_SUCCESS);
    vkDestroyDevice(device, NULL);
    free_debug(instance, messenger, NULL);
    vkDestroyInstance(instance, NULL);
    printf("Vulkan queue host sync PASS: 3000 operations, warnings %u, errors %u\n", atomic_load(&warnings), atomic_load(&errors));
    assert(!atomic_load(&warnings) && !atomic_load(&errors));
    return 0;
}
