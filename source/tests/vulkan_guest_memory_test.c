// Synthetic Maxwell -> GLSL -> real Vulkan readback. No game or surface.
#include <assert.h>
#include <inttypes.h>
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../engine/runtime/nvn/maxwell/shader.h"
#include "../engine/runtime/nvn/vulkan/guest_memory.h"
#define VK(call) do {VkResult r=(call);if(r!=VK_SUCCESS){fprintf(stderr,"FAIL %s: %d\n",#call,r);exit(1);}} while(0)
static unsigned messages;
static VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
    (void)type;(void)user;if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT){messages++;fprintf(stderr,"validation: %s\n",data->pMessage);}return VK_FALSE;
}
static uint64_t mov(unsigned reg,uint32_t value) {return UINT64_C(0x0100000000000000)|((uint64_t)value<<20)|(7ull<<16)|reg;}
static uint64_t memory_op(bool store,unsigned reg,unsigned address,unsigned size) {
    uint64_t word=store?UINT64_C(0xeed8000000000000):UINT64_C(0xeed0000000000000);
    return (word&~(7ull<<48))|((uint64_t)size<<48)|(1ull<<45)|(7ull<<16)|((uint64_t)address<<8)|reg;
}
static uint32_t *program(uint64_t input,uint64_t output,unsigned width,size_t *bytes) {
    uint64_t code[16]={0};
    code[0]=UINT64_C(0x001f8000fc0007e0);code[1]=mov(0,(uint32_t)input);code[2]=mov(1,(uint32_t)(input>>32));code[3]=mov(2,(uint32_t)output);
    code[4]=code[0];code[5]=mov(3,(uint32_t)(output>>32));code[6]=memory_op(false,4,0,width);code[7]=memory_op(true,4,2,width);
    code[8]=code[0];code[9]=UINT64_C(0xe30000000007000f);code[10]=UINT64_C(0xe2400fffff87000f);code[11]=code[10];
    ShaderKey key={.glsl=1};ShaderInfo *shader=maxwell_translate((const uint8_t *)code,sizeof code,STAGE_COMPUTE,&key);
    if(!shader || !shader->msl){fprintf(stderr,"FAIL translate: %s\n",shader?shader->error:"null");exit(1);}
    // Binding17 must describe16 bytes of address/size, never the whole heap.
    if(!strstr(shader->msl,"uniform GMemAddress")){fprintf(stderr,"FAIL oversized guest heap storage descriptor\n");exit(1);}
    shaderc_compiler_t compiler=shaderc_compiler_initialize();shaderc_compile_options_t options=shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(options,shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_3);
    shaderc_compilation_result_t result=shaderc_compile_into_spv(compiler,shader->msl,strlen(shader->msl),shaderc_compute_shader,"synthetic-gmem","main",options);
    if(shaderc_result_get_compilation_status(result)!=shaderc_compilation_status_success){fprintf(stderr,"FAIL shader: %s\nSOURCE:\n%s\n",shaderc_result_get_error_message(result),shader->msl);exit(1);}
    *bytes=shaderc_result_get_length(result);uint32_t *words=malloc(*bytes);assert(words);memcpy(words,shaderc_result_get_bytes(result),*bytes);
    shaderc_result_release(result);shaderc_compile_options_release(options);shaderc_compiler_release(compiler);maxwell_free(shader);return words;
}
typedef struct {VkBuffer buffer;VkDeviceMemory memory;void *map;} SmallBuffer;
static SmallBuffer small_buffer(VkPhysicalDevice physical,VkDevice device,VkDeviceSize size,VkBufferUsageFlags usage) {
    SmallBuffer b={0};VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};VK(vkCreateBuffer(device,&create,NULL,&b.buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(physical,&props);
    uint32_t type=UINT32_MAX;for(uint32_t i=0;i<props.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
    assert(type!=UINT32_MAX);VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};VK(vkAllocateMemory(device,&alloc,NULL,&b.memory));VK(vkBindBufferMemory(device,b.buffer,b.memory,0));VK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.map));memset(b.map,0,size);return b;
}
static void destroy_small(VkDevice device,SmallBuffer *b){vkUnmapMemory(device,b->memory);vkDestroyBuffer(device,b->buffer,NULL);vkFreeMemory(device,b->memory,NULL);}
static uint64_t input_offset(unsigned mode,uint64_t limit,uint64_t size) {
    return mode==0?0:mode==1?limit+32:mode==2?size-4:mode==3?4096:mode==4?size-4:mode==5?size:mode==6?UINT64_MAX-UINT64_C(0x7240000000):32;
}
static uint32_t query_limit(void) {
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};VkInstance instance;VK(vkCreateInstance(&info,NULL,&instance));
    VkPhysicalDevice physical;uint32_t count=1;VK(vkEnumeratePhysicalDevices(instance,&count,&physical));assert(count==1);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);vkDestroyInstance(instance,NULL);return properties.limits.maxStorageBufferRange;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);assert(argc==4);unsigned first=(unsigned)strtoul(argv[2],NULL,10);assert(first<14);unsigned last=first+1;
    if(!strcmp(argv[1],"--compile")) {
        uint64_t base=UINT64_C(0x7240000000),limit=query_limit();maxwell_gmem_base=base;maxwell_gmem_size=UINT64_C(0xd0000000);
        uint64_t size=limit+65536, input=first>=9?base+32:base+input_offset(first,limit,size);
        uint64_t output=first==9?base+size-4:first==10?base+size-16:first==11?base+size: first==12?base+4096:base+512;
        size_t bytes;uint32_t *spv=program(input,output,first==4||first==9||first==10||first==13?6:4,&bytes);
        FILE *file=fopen(argv[3],"wb");assert(file);assert(fwrite(spv,1,bytes,file)==bytes);assert(!fclose(file));free(spv);
        printf("Compiled synthetic GPU guest-memory mode%u\n",first);return 0;
    }
    assert(!strcmp(argv[1],"--run"));
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
    const char *layers[]={"VK_LAYER_KHRONOS_validation"},*extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkInstanceCreateInfo create={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledLayerCount=1,.ppEnabledLayerNames=layers,.enabledExtensionCount=1,.ppEnabledExtensionNames=extensions};
    VkInstance instance;VK(vkCreateInstance(&create,NULL,&instance));
    PFN_vkCreateDebugUtilsMessengerEXT add=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=message};
    VkDebugUtilsMessengerEXT messenger;assert(add);VK(add(instance,&debug,NULL,&messenger));
    uint32_t count=1;VkPhysicalDevice physical;VK(vkEnumeratePhysicalDevices(instance,&count,&physical));assert(count==1);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,NULL);assert(families && families<=64);VkQueueFamilyProperties q[64];vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,q);
    uint32_t family=UINT32_MAX;for(uint32_t i=0;i<families;i++)if(q[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}assert(family!=UINT32_MAX);
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.maintenance4=VK_TRUE};
    VkPhysicalDeviceVulkan12Features f12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&f13,.bufferDeviceAddress=VK_TRUE};
    VkPhysicalDeviceFeatures features={.shaderInt64=VK_TRUE};const char *device_extensions[]={VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f12,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,.enabledExtensionCount=1,.ppEnabledExtensionNames=device_extensions,.pEnabledFeatures=&features};
    VkDevice device;VK(vkCreateDevice(physical,&device_info,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    // The CPU-visible import is larger than this driver's descriptor range.
    VkDeviceSize size=(VkDeviceSize)properties.limits.maxStorageBufferRange+65536;
    assert(size<=(256ull<<20));void *heap=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(heap!=MAP_FAILED);
    GuestMemoryBuffer imported;VK(guest_memory_import(physical,device,heap,size,&imported));assert(imported.address && imported.size==size);
    GuestMemoryBuffer zero;VK(guest_memory_zero(physical,device,&zero));void *zero_map;VK(vkMapMemory(device,zero.memory,0,4096,0,&zero_map));
    SmallBuffer meta=small_buffer(physical,device,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),map=small_buffer(physical,device,(GMAP_MAX+1)*16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),fast=small_buffer(physical,device,4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    VkDescriptorSetLayoutBinding bindings[3]={{.binding=17,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=20,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=21,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo layout_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};VkDescriptorSetLayout layout;VK(vkCreateDescriptorSetLayout(device,&layout_info,NULL,&layout));
    VkPipelineLayoutCreateInfo pipeline_layout_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};VkPipelineLayout pipeline_layout;VK(vkCreatePipelineLayout(device,&pipeline_layout_info,NULL,&pipeline_layout));
    VkDescriptorPoolSize pools[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=pools};VkDescriptorPool pool;VK(vkCreateDescriptorPool(device,&pool_info,NULL,&pool));
    VkDescriptorSetAllocateInfo set_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};VkDescriptorSet set;VK(vkAllocateDescriptorSets(device,&set_info,&set));
    VkDescriptorBufferInfo buffer_info[3]={{meta.buffer,0,16},{map.buffer,0,(GMAP_MAX+1)*16},{fast.buffer,0,4}};VkWriteDescriptorSet writes[3];
    for(unsigned i=0;i<3;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=bindings[i].binding,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pBufferInfo=&buffer_info[i]};vkUpdateDescriptorSets(device,3,writes,0,NULL);
    VkCommandPoolCreateInfo command_pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool command_pool;VK(vkCreateCommandPool(device,&command_pool_info,NULL,&command_pool));
    VkCommandBufferAllocateInfo commands={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;VK(vkAllocateCommandBuffers(device,&commands,&command));
    unsigned passed=0;
    for(unsigned mode=first;mode<last;mode++) {
        uint64_t live=mode==7?0:mode==3||mode==8||mode==12?4096:imported.size;uint64_t offset=mode>=9?32:input_offset(mode,properties.limits.maxStorageBufferRange,size);
        uint32_t expected=mode==0||mode==1||mode==2?UINT32_C(0x13579bdf):0;
        uint64_t *metadata=meta.map;metadata[0]=mode==7||mode==8?zero.address:imported.address;metadata[1]=live;
        uint32_t *output=(uint32_t *)((char *)(mode==8?zero_map:heap)+512);*output=UINT32_C(0xfeedface);
        if(offset<size)memcpy((char *)heap+offset,&(uint32_t){UINT32_C(0x13579bdf)},4);
        uint32_t *tail=(uint32_t *)((char *)heap+size-16);
        uint32_t *short_end=(uint32_t *)((char *)heap+4096);
        if(mode>=9) {
            memcpy((char *)heap+32,(uint32_t[]){0x12345678,0x23456789,0x3456789a,0x456789ab},16);
            for(unsigned i=0;i<4;i++)tail[i]=0xfeedface;
            *short_end=0xfeedface;
        }
        printf("Starting GPU guest-memory mode%u\n",mode);
        FILE *file=fopen(argv[3],"rb");assert(file);assert(!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0 && length%4==0 && length<1048576);rewind(file);
        size_t bytes=(size_t)length;uint32_t *spv=malloc(bytes);assert(spv);assert(fread(spv,1,bytes,file)==bytes);assert(!fclose(file));
        VkShaderModuleCreateInfo module_info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=spv};VkShaderModule module;VK(vkCreateShaderModule(device,&module_info,NULL,&module));free(spv);
        VkComputePipelineCreateInfo pipeline_info={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=pipeline_layout};VkPipeline pipeline;VK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
        VK(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(command,&begin));
        VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);vkCmdDispatch(command,1,1,1);
        VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);VK(vkEndCommandBuffer(command));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};VK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));VK(vkQueueWaitIdle(queue));
        if(mode==13) {
            assert(!memcmp(output,(char *)heap+32,16));
        } else if(mode>=9) {
            for(unsigned i=0;i<4;i++)assert(tail[i]==(mode==10?((uint32_t *)((char *)heap+32))[i]:0xfeedface));
            assert(*short_end==0xfeedface);
        } else if(mode==7){assert(*output==UINT32_C(0xfeedface));}else if(*output!=expected){fprintf(stderr,"FAIL mode%u readback %08x expected %08x\n",mode,*output,expected);return 1;}
        vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,module,NULL);printf("GPU guest-memory mode%u PASS\n",mode);passed++;
    }
    assert(!messages);VK(vkDeviceWaitIdle(device));vkDestroyCommandPool(device,command_pool,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyPipelineLayout(device,pipeline_layout,NULL);vkDestroyDescriptorSetLayout(device,layout,NULL);
    destroy_small(device,&fast);destroy_small(device,&map);destroy_small(device,&meta);vkUnmapMemory(device,zero.memory);vkDestroyBuffer(device,zero.buffer,NULL);vkFreeMemory(device,zero.memory,NULL);vkDestroyBuffer(device,imported.buffer,NULL);vkFreeMemory(device,imported.memory,NULL);munmap(heap,size);vkDestroyDevice(device,NULL);
    PFN_vkDestroyDebugUtilsMessengerEXT remove=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");remove(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
    printf("GPU guest-memory: %u cases PASS; descriptor limit %u, imported bytes %" PRIu64 "; zero validation warnings; no game\n",passed,properties.limits.maxStorageBufferRange,(uint64_t)size);return 0;
}
