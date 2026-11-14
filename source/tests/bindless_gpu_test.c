// Offscreen, asset-free readback through the actual Maxwell TEX.B translator.
// 32 adjacent pixels select different handles; explicit gradients must survive
// the helper's per-texture control flow. No window, game or user files.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include "../engine/runtime/nvn/maxwell/shader.h"
#include "../engine/runtime/nvn/texture_cbuf.h"
#include "../engine/tools/bindless_fixture.h"

#define CHECK(x) do { VkResult r=(x); if(r!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#x,r);abort();} } while(0)
enum { WIDTH=32, HEIGHT=2, PHASES=4, MIPS=3, DRAWS=PHASES, PIXEL_BYTES=16, DRAW_BYTES=WIDTH*HEIGHT*PIXEL_BYTES };
static VkInstance instance;
static VkPhysicalDevice physical;
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory;
static _Atomic unsigned warnings,errors;
typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *mapped; } Buffer;
typedef struct { VkImage image; VkDeviceMemory memory; VkImageView view; } Image;

static VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
    (void)type;(void)user;
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;
    else if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)++warnings;
    fprintf(stderr,"validation: %s\n",data->pMessage);return VK_FALSE;
}
static uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags) {
    for(uint32_t i=0;i<memory.memoryTypeCount;i++)
        if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
    fprintf(stderr,"Required memory type unavailable\n");abort();
}
static Buffer buffer(VkDeviceSize bytes,VkBufferUsageFlags usage) {
    Buffer b={0};VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    CHECK(vkCreateBuffer(device,&info,NULL,&b.buffer));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    CHECK(vkAllocateMemory(device,&alloc,NULL,&b.memory));CHECK(vkBindBufferMemory(device,b.buffer,b.memory,0));
    CHECK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped));return b;
}
static Image image(unsigned width,unsigned height,unsigned levels,VkFormat format,VkImageUsageFlags usage) {
    Image image={0};VkImageCreateInfo info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=format,.extent={width,height,1},.mipLevels=levels,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    CHECK(vkCreateImage(device,&info,NULL,&image.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image.image,&req);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    CHECK(vkAllocateMemory(device,&alloc,NULL,&image.memory));CHECK(vkBindImageMemory(device,image.image,image.memory,0));
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1}};
    CHECK(vkCreateImageView(device,&view,NULL,&image.view));return image;
}
static void free_buffer(Buffer b) { vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,NULL);vkFreeMemory(device,b.memory,NULL); }
static void free_image(Image i) { vkDestroyImageView(device,i.view,NULL);vkDestroyImage(device,i.image,NULL);vkFreeMemory(device,i.memory,NULL); }
static void barrier(VkCommandBuffer cb,VkImage image,unsigned levels,VkImageLayout from,VkImageLayout to,
    VkAccessFlags source,VkAccessFlags dest,VkPipelineStageFlags source_stage,VkPipelineStageFlags dest_stage) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=source,.dstAccessMask=dest,
        .oldLayout=from,.newLayout=to,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1}};
    vkCmdPipelineBarrier(cb,source_stage,dest_stage,0,0,NULL,0,NULL,1,&b);
}
static void compile_file(shaderc_compiler_t compiler,shaderc_compile_options_t options,const char *folder,const char *name,const char *source,shaderc_shader_kind stage) {
    shaderc_compilation_result_t result=shaderc_compile_into_spv(compiler,source,strlen(source),stage,"bindless-fixture","main",options);
    if(shaderc_result_get_compilation_status(result)!=shaderc_compilation_status_success) {
        fprintf(stderr,"Shader compile: %s\n",shaderc_result_get_error_message(result));abort();
    }
    char path[1024];assert(snprintf(path,sizeof path,"%s/%s",folder,name)>0);
    FILE *file=fopen(path,"wb");assert(file);
    size_t size=shaderc_result_get_length(result);assert(fwrite(shaderc_result_get_bytes(result),1,size,file)==size && !fclose(file));
    shaderc_result_release(result);
}
static VkShaderModule load_module(const char *folder,const char *name) {
    char path[1024];assert(snprintf(path,sizeof path,"%s/%s",folder,name)>0);
    FILE *file=fopen(path,"rb");assert(file && !fseek(file,0,SEEK_END));long size=ftell(file);
    assert(size>0 && size<=4*1024*1024 && !(size%4) && !fseek(file,0,SEEK_SET));
    uint32_t *code=malloc((size_t)size);assert(code && fread(code,1,(size_t)size,file)==(size_t)size && !fclose(file));
    VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)size,.pCode=code};
    VkShaderModule module;CHECK(vkCreateShaderModule(device,&info,NULL,&module));free(code);return module;
}
static uint64_t ipa(unsigned d,unsigned address) {
    return UINT64_C(0xe00000000007ff00)|((uint64_t)address<<28)|d;
}
static ShaderInfo *fragment(unsigned mip) {
    BindlessFixture p;bf_init(&p);
    bf_emit(&p,ipa(0,0x70));bf_emit(&p,ipa(1,0x74));
    // Truncate guest fragment x, then use its integer value as the handle slot.
    bf_emit(&p,UINT64_C(0x5cb0000000070a06)|(UINT64_C(3)<<39));
    bf_emit(&p,UINT64_C(0x3848000000370606));
    const uint32_t scale[]={0x3e000000,0x3f000000,0x3f800000};
    bf_mov(&p,2,scale[mip]);
    bf_emit(&p,UINT64_C(0x5c68000000270000));bf_emit(&p,UINT64_C(0x5c68000000270101));
    bf_emit(&p,bf_ldc(4,6,2,0x20,4));bf_emit(&p,bf_tex(0,0,4,15));bf_exit(&p);
    if(mip==2) {
        while(p.used%4)bf_emit(&p,UINT64_C(0x50b0000000070f00));
        bf_emit(&p,UINT64_C(0xe2400fffff07000f)); // terminal branch to its own scheduling word
    }
    ShaderKey key={.glsl=1};key.rt_kind[0]=2; // uint attachment preserves sample float bits; no cosmetic tint UBO
    ShaderInfo *s=maxwell_translate((const uint8_t *)p.words,bf_bytes(&p),STAGE_FRAGMENT,&key);
    if(!s->msl){fprintf(stderr,"Translation: %s\n",s->error);abort();}
    assert(s->ntex==32 && !s->uses_tint && s->uses_rscale && s->cbuf_size[2]==65536);return s;
}
static void compile_sources(const char *folder) {
    shaderc_compiler_t compiler=shaderc_compiler_initialize();shaderc_compile_options_t options=shaderc_compile_options_initialize();assert(compiler && options);
    shaderc_compile_options_set_target_env(options,shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_3);
    const char *vertex="#version 460\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex],0,1);}\n";
    compile_file(compiler,options,folder,"vs.spv",vertex,shaderc_vertex_shader);
    for(unsigned l=0;l<MIPS;l++) {
        ShaderInfo *shader=fragment(l);char name[32];snprintf(name,sizeof name,"fs%u.spv",l);
        compile_file(compiler,options,folder,name,shader->msl,shaderc_fragment_shader);maxwell_free(shader);
    }
    shaderc_compile_options_release(options);shaderc_compiler_release(compiler);
    puts("Bindless synthetic SPIR-V compilation PASS");
}
static const uint8_t colors[2][3][4]={{{255,0,0,255},{0,255,0,255},{0,0,255,255}},
                                    {{0,255,255,255},{255,0,255,255},{255,255,0,255}}};
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--compile")) { compile_sources(argv[2]);return 0; }
    assert(argc==4 && !strcmp(argv[1],"--run"));const char *folder=argv[2];
    unsigned selected=(unsigned)strtoul(argv[3],NULL,10);assert(selected<MIPS);
    const char *layer="VK_LAYER_KHRONOS_validation",*extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SST bindless fixture",.apiVersion=VK_API_VERSION_1_3};
    VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback=message};
    VkInstanceCreateInfo create={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&debug,.pApplicationInfo=&app,
        .enabledLayerCount=1,.ppEnabledLayerNames=&layer,.enabledExtensionCount=1,.ppEnabledExtensionNames=&extension};
    CHECK(vkCreateInstance(&create,NULL,&instance));
    PFN_vkCreateDebugUtilsMessengerEXT make_debug=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT messenger;CHECK(make_debug(instance,&debug,NULL,&messenger));
    uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));assert(count>0 && count<=16);
    VkPhysicalDevice devices[16];CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));physical=devices[0];
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    assert(properties.limits.maxPerStageDescriptorSamplers>=32 && properties.limits.maxPerStageDescriptorSampledImages>=32 && properties.limits.maxUniformBufferRange>=65536);
    VkPhysicalDeviceFeatures features;vkGetPhysicalDeviceFeatures(physical,&features);assert(features.shaderInt64);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint32_t qcount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,NULL);assert(qcount<=32);
    VkQueueFamilyProperties queues[32];vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,queues);uint32_t family=0;
    while(family<qcount && !(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<qcount);
    float priority=1;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceFeatures enabled={.shaderInt64=VK_TRUE};VkDeviceCreateInfo dev={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_info,.pEnabledFeatures=&enabled};
    CHECK(vkCreateDevice(physical,&dev,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool pool;CHECK(vkCreateCommandPool(device,&pool_info,NULL,&pool));
    VkCommandBufferAllocateInfo cb_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb;CHECK(vkAllocateCommandBuffers(device,&cb_info,&cb));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));

    Buffer upload=buffer(168,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),readback=buffer(DRAWS*DRAW_BYTES,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Image textures[2];
    for(unsigned t=0;t<2;t++) {
        textures[t]=image(4,4,3,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
        barrier(cb,textures[t].image,3,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        unsigned cursor=t*84;
        for(unsigned l=0;l<3;l++) {
            unsigned side=4>>l;
            for(unsigned i=0;i<side*side;i++)memcpy((uint8_t *)upload.mapped+cursor+i*4,colors[t][l],4);
            VkBufferImageCopy copy={.bufferOffset=cursor,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,l,0,1},.imageExtent={side,side,1}};
            vkCmdCopyBufferToImage(cb,upload.buffer,textures[t].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);cursor+=side*side*4;
        }
        barrier(cb,textures[t].image,3,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    VkSamplerCreateInfo sampling={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT,.maxLod=2};
    VkSampler sampler;CHECK(vkCreateSampler(device,&sampling,NULL,&sampler));
    Buffer cbuf[PHASES],rscale=buffer(66*16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    memset(rscale.mapped,0,66*16);((float *)rscale.mapped)[0]=((float *)rscale.mapped)[1]=1;
    uint64_t handles[32];for(unsigned i=0;i<32;i++)handles[i]=(UINT64_C(0xa5a50000)<<32)|((i%2+1)<<20)|(i+1);
    VkDescriptorSetLayoutBinding bindings[34]={{.binding=2,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=18,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
    for(unsigned i=0;i<32;i++)bindings[2+i]=(VkDescriptorSetLayoutBinding){.binding=32+i,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo layout_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};VkDescriptorSetLayout layouts[2];CHECK(vkCreateDescriptorSetLayout(device,&layout_info,NULL,&layouts[0]));
    layout_info.bindingCount=34;layout_info.pBindings=bindings;CHECK(vkCreateDescriptorSetLayout(device,&layout_info,NULL,&layouts[1]));
    VkPipelineLayoutCreateInfo pipe_layout_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=layouts};VkPipelineLayout pipe_layout;CHECK(vkCreatePipelineLayout(device,&pipe_layout_info,NULL,&pipe_layout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,PHASES*2},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,PHASES*32}};
    VkDescriptorPoolCreateInfo descriptor_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=PHASES,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool descriptors;CHECK(vkCreateDescriptorPool(device,&descriptor_info,NULL,&descriptors));
    VkDescriptorSet sets[PHASES];
    for(unsigned phase=0;phase<PHASES;phase++) {
        if(phase==1)handles[31]=(UINT64_C(0xdeadbeef)<<32)|(1u<<20)|100;
        if(phase==2)handles[31]=0;
        if(phase==3)handles[31]=handles[0];
        cbuf[phase]=buffer(65536,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);nvn_texture_cbuf_snapshot(cbuf[phase].mapped,65536,handles);
        VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1,.pSetLayouts=&layouts[1]};CHECK(vkAllocateDescriptorSets(device,&allocate,&sets[phase]));
        VkDescriptorBufferInfo buffers[]={{cbuf[phase].buffer,0,65536},{rscale.buffer,0,66*16}};
        VkDescriptorImageInfo images[32];VkWriteDescriptorSet writes[34];memset(writes,0,sizeof writes);
        for(unsigned i=0;i<34;i++) {
            writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[phase],.dstBinding=bindings[i].binding,.descriptorCount=1,.descriptorType=bindings[i].descriptorType};
            if(i<2)writes[i].pBufferInfo=&buffers[i];
            else {unsigned slot=i-2,t=slot%2;if(slot==31 && (phase==1 || phase==3))t=0;
                images[slot]=(VkDescriptorImageInfo){sampler,textures[t].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};writes[i].pImageInfo=&images[slot];}
        }
        vkUpdateDescriptorSets(device,34,writes,0,NULL);
    }
    memset(handles,0,sizeof handles); // no descriptor may borrow this mutable CPU table
    VkAttachmentDescription attachment={.format=VK_FORMAT_R32G32B32A32_UINT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
    VkSubpassDependency dependencies[]={{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
        {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0}};
    VkRenderPassCreateInfo render_info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=2,.pDependencies=dependencies};
    VkRenderPass render;CHECK(vkCreateRenderPass(device,&render_info,NULL,&render));
    VkShaderModule vs=load_module(folder,"vs.spv"),fs[MIPS]={0};VkPipeline pipelines[MIPS]={0};
    for(unsigned l=selected;l<=selected;l++) {
        char name[32];snprintf(name,sizeof name,"fs%u.spv",l);fs[l]=load_module(folder,name);
        VkPipelineShaderStageCreateInfo stages[]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
            {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs[l],.pName="main"}};
        VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkViewport viewport={0,0,WIDTH,HEIGHT,0,1};VkRect2D scissor={{0,0},{WIDTH,HEIGHT}};
        VkPipelineViewportStateCreateInfo view={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
        VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};
        VkPipelineMultisampleStateCreateInfo multisample={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blending={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
        VkGraphicsPipelineCreateInfo graphics={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&input,.pInputAssemblyState=&assembly,
            .pViewportState=&view,.pRasterizationState=&raster,.pMultisampleState=&multisample,.pColorBlendState=&blending,.layout=pipe_layout,.renderPass=render};
        CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&graphics,NULL,&pipelines[l]));
    }
    Image targets[DRAWS];VkFramebuffer framebuffers[DRAWS];
    for(unsigned draw=0;draw<DRAWS;draw++) {
        targets[draw]=image(WIDTH,HEIGHT,1,VK_FORMAT_R32G32B32A32_UINT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=render,.attachmentCount=1,.pAttachments=&targets[draw].view,.width=WIDTH,.height=HEIGHT,.layers=1};CHECK(vkCreateFramebuffer(device,&fb,NULL,&framebuffers[draw]));
        VkClearValue clear={0};VkRenderPassBeginInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=render,.framebuffer=framebuffers[draw],.renderArea={{0,0},{WIDTH,HEIGHT}},.clearValueCount=1,.pClearValues=&clear};
        vkCmdBeginRenderPass(cb,&pass,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[selected]);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe_layout,1,1,&sets[draw],0,NULL);vkCmdDraw(cb,3,1,0,0);vkCmdEndRenderPass(cb);
        VkBufferImageCopy copy={.bufferOffset=draw*DRAW_BYTES,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={WIDTH,HEIGHT,1}};
        vkCmdCopyImageToBuffer(cb,targets[draw].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
    }
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
    unsigned checked=0;
    for(unsigned draw=0;draw<DRAWS;draw++)for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++) {
        unsigned phase=draw,l=selected,t=x%2;bool zero=phase==2 && x==31;
        if(x==31 && (phase==1 || phase==3))t=0;
        for(unsigned c=0;c<4;c++) {
            uint32_t expected=!zero && colors[t][l][c]?0x3f800000:0;
            uint32_t actual=((uint32_t *)readback.mapped)[draw*DRAW_BYTES/4+(y*WIDTH+x)*4+c];
            if(actual!=expected){fprintf(stderr,"Readback draw%u x%u y%u channel%u: %08x expected %08x\n",draw,x,y,c,actual,expected);abort();}
        }
        ++checked;
    }
    for(unsigned i=0;i<DRAWS;i++){vkDestroyFramebuffer(device,framebuffers[i],NULL);free_image(targets[i]);}
    for(unsigned i=selected;i<=selected;i++){vkDestroyPipeline(device,pipelines[i],NULL);vkDestroyShaderModule(device,fs[i],NULL);}
    vkDestroyShaderModule(device,vs,NULL);
    vkDestroyRenderPass(device,render,NULL);vkDestroyDescriptorPool(device,descriptors,NULL);vkDestroyPipelineLayout(device,pipe_layout,NULL);
    for(unsigned i=0;i<2;i++)vkDestroyDescriptorSetLayout(device,layouts[i],NULL);
    for(unsigned i=0;i<PHASES;i++)free_buffer(cbuf[i]);free_buffer(rscale);free_buffer(upload);free_buffer(readback);
    vkDestroySampler(device,sampler,NULL);free_image(textures[0]);free_image(textures[1]);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    destroy_debug(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
    printf("Bindless GPU mode%u readback: %u draws, %u pixels, warnings %u, errors %u\n",selected,DRAWS,checked,warnings,errors);
    assert(!warnings && !errors);return 0;
}
