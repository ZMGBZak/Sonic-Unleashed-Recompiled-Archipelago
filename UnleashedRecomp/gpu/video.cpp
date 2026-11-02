#include <nfd.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>
#include <apu/custom_music.h>
#include "video.h"
// AP_UPDATER_V1_INCLUDES
#include <atomic>
#include <thread>
#include <mutex>
#include <stdexcept>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#include <os/ap_update_helper.h> // embedded EXE self-updater
#pragma comment(lib, "winhttp.lib")
#endif

#include "imgui/imgui_common.h"
#include "imgui/imgui_snapshot.h"
#include "imgui/imgui_font_builder.h"

#include <app.h>
#include <bc_diff.h>
#include <cpu/guest_thread.h>
#include <decompressor.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <hid/hid.h>
#include <kernel/memory.h>
#include <kernel/xdbf.h>
#include <res/bc_diff/button_bc_diff.bin.h>
#include <res/font/im_font_atlas.dds.h>
#include <shader/shader_cache.h>
#include <SWA.h>
#include <ui/achievement_menu.h>
#include <ui/achievement_overlay.h>
#include <ui/button_guide.h>
#include <ui/fader.h>
#include <ui/imgui_utils.h>
#include <ui/installer_wizard.h>
#include <ui/message_window.h>
#include <ui/options_menu.h>
#include <ui/game_window.h>
#include <ui/black_bar.h>
#include <patches/aspect_ratio_patches.h>
#include <patches/ap_medal_radar.h> // AP_MEDAL_RADAR_AUDIO_V1
#include <patches/ap_ring_roulette.h> // AP_RING_ROULETTE_V1
#include <gpu/ap_roulette_embedded_assets.h> // AP_RING_ROULETTE_EMBEDDED_V3
#include <user/config.h>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <limits>
#include <random>
#include <iterator>
#include "../archipelago_client.h"
#include <sdl_listener.h>
#include <xxHashMap.h>
#include <os/process.h>
#include <os/logger.h>

#if defined(ASYNC_PSO_DEBUG) || defined(PSO_CACHING)
#include <magic_enum/magic_enum.hpp>
#endif

#define UNLEASHED_RECOMP
#include "../../tools/XenosRecomp/XenosRecomp/shader_common.h"

#ifdef UNLEASHED_RECOMP_D3D12
#include "shader/blend_color_alpha_ps.hlsl.dxil.h"
#include "shader/copy_vs.hlsl.dxil.h"
#include "shader/copy_color_ps.hlsl.dxil.h"
#include "shader/copy_depth_ps.hlsl.dxil.h"
#include "shader/csd_filter_ps.hlsl.dxil.h"
#include "shader/csd_no_tex_vs.hlsl.dxil.h"
#include "shader/csd_vs.hlsl.dxil.h"
#include "shader/enhanced_motion_blur_ps.hlsl.dxil.h"
#include "shader/gamma_correction_ps.hlsl.dxil.h"
#include "shader/gaussian_blur_3x3.hlsl.dxil.h"
#include "shader/gaussian_blur_5x5.hlsl.dxil.h"
#include "shader/gaussian_blur_7x7.hlsl.dxil.h"
#include "shader/gaussian_blur_9x9.hlsl.dxil.h"
#include "shader/imgui_ps.hlsl.dxil.h"
#include "shader/imgui_vs.hlsl.dxil.h"
#include "shader/movie_ps.hlsl.dxil.h"
#include "shader/movie_vs.hlsl.dxil.h"
#include "shader/resolve_msaa_color_2x.hlsl.dxil.h"
#include "shader/resolve_msaa_color_4x.hlsl.dxil.h"
#include "shader/resolve_msaa_color_8x.hlsl.dxil.h"
#include "shader/resolve_msaa_depth_2x.hlsl.dxil.h"
#include "shader/resolve_msaa_depth_4x.hlsl.dxil.h"
#include "shader/resolve_msaa_depth_8x.hlsl.dxil.h"
#endif

#include "shader/blend_color_alpha_ps.hlsl.spirv.h"
#include "shader/copy_vs.hlsl.spirv.h"
#include "shader/copy_color_ps.hlsl.spirv.h"
#include "shader/copy_depth_ps.hlsl.spirv.h"
#include "shader/csd_filter_ps.hlsl.spirv.h"
#include "shader/csd_no_tex_vs.hlsl.spirv.h"
#include "shader/csd_vs.hlsl.spirv.h"
#include "shader/enhanced_motion_blur_ps.hlsl.spirv.h"
#include "shader/gamma_correction_ps.hlsl.spirv.h"
#include "shader/gaussian_blur_3x3.hlsl.spirv.h"
#include "shader/gaussian_blur_5x5.hlsl.spirv.h"
#include "shader/gaussian_blur_7x7.hlsl.spirv.h"
#include "shader/gaussian_blur_9x9.hlsl.spirv.h"
#include "shader/imgui_ps.hlsl.spirv.h"
#include "shader/imgui_vs.hlsl.spirv.h"
#include "shader/movie_ps.hlsl.spirv.h"
#include "shader/movie_vs.hlsl.spirv.h"
#include "shader/resolve_msaa_color_2x.hlsl.spirv.h"
#include "shader/resolve_msaa_color_4x.hlsl.spirv.h"
#include "shader/resolve_msaa_color_8x.hlsl.spirv.h"
#include "shader/resolve_msaa_depth_2x.hlsl.spirv.h"
#include "shader/resolve_msaa_depth_4x.hlsl.spirv.h"
#include "shader/resolve_msaa_depth_8x.hlsl.spirv.h"

#ifdef _WIN32
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace plume
{
#ifdef UNLEASHED_RECOMP_D3D12
    extern std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#ifdef SDL_VULKAN_ENABLED
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow sdlWindow);
#else
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif

    static std::unique_ptr<RenderInterface> CreateVulkanInterfaceWrapper() {
#ifdef SDL_VULKAN_ENABLED
        return CreateVulkanInterface(GameWindow::s_renderWindow);
#else
        return CreateVulkanInterface();
#endif
    }
}

#pragma pack(push, 1)
struct PipelineState
{
    GuestShader* vertexShader = nullptr;
    GuestShader* pixelShader = nullptr;
    GuestVertexDeclaration* vertexDeclaration = nullptr;
    bool instancing = false;
    bool zEnable = true;
    bool zWriteEnable = true;
    RenderBlend srcBlend = RenderBlend::ONE;
    RenderBlend destBlend = RenderBlend::ZERO;
    RenderCullMode cullMode = RenderCullMode::NONE;
    RenderComparisonFunction zFunc = RenderComparisonFunction::LESS;
    bool alphaBlendEnable = false;
    RenderBlendOperation blendOp = RenderBlendOperation::ADD;
    float slopeScaledDepthBias = 0.0f;
    int32_t depthBias = 0;
    RenderBlend srcBlendAlpha = RenderBlend::ONE;
    RenderBlend destBlendAlpha = RenderBlend::ZERO;
    RenderBlendOperation blendOpAlpha = RenderBlendOperation::ADD;
    uint32_t colorWriteEnable = uint32_t(RenderColorWriteEnable::ALL);
    RenderPrimitiveTopology primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    uint8_t vertexStrides[16]{};
    RenderFormat renderTargetFormat{};
    RenderFormat depthStencilFormat{};
    RenderSampleCounts sampleCount = RenderSampleCount::COUNT_1;
    bool enableAlphaToCoverage = false;
    uint32_t specConstants = 0;
};
#pragma pack(pop)

struct UploadAllocation
{
    const RenderBuffer* buffer;
    uint64_t offset;
    uint8_t* memory;
    uint64_t deviceAddress;
};

struct SharedConstants
{
    uint32_t texture2DIndices[16]{};
    uint32_t texture3DIndices[16]{};
    uint32_t textureCubeIndices[16]{};
    uint32_t samplerIndices[16]{};
    uint32_t booleans{};
    uint32_t swappedTexcoords{};
    float halfPixelOffsetX{};
    float halfPixelOffsetY{};
    float alphaThreshold{};
};

// Depth bias values here are only used when the render device has 
// dynamic depth bias capability enabled. Otherwise, they get unused
// and the values get assigned in the pipeline state instead.

static GuestSurface* g_renderTarget;
static GuestSurface* g_depthStencil;
static RenderFramebuffer* g_framebuffer;
static RenderViewport g_viewport(0.0f, 0.0f, 1280.0f, 720.0f);
static PipelineState g_pipelineState;
static int32_t g_depthBias;
static float g_slopeScaledDepthBias;
static uint32_t g_vertexShaderConstants[0x400];
static uint32_t g_pixelShaderConstants[0x380];
static SharedConstants g_sharedConstants;
static GuestTexture* g_textures[16];
static RenderSamplerDesc g_samplerDescs[16];
static bool g_scissorTestEnable = false;
static RenderRect g_scissorRect;
static RenderVertexBufferView g_vertexBufferViews[16];
static RenderInputSlot g_inputSlots[16];
static RenderIndexBufferView g_indexBufferView({}, 0, RenderFormat::R16_UINT);

struct DirtyStates
{
    bool renderTargetAndDepthStencil;
    bool viewport;
    bool pipelineState;
    bool depthBias;
    bool sharedConstants;
    bool scissorRect;
    bool vertexShaderConstants;
    uint8_t vertexStreamFirst;
    uint8_t vertexStreamLast;
    bool indices;
    bool pixelShaderConstants;

    DirtyStates(bool value)
        : renderTargetAndDepthStencil(value)
        , viewport(value)
        , pipelineState(value)
        , depthBias(value)
        , sharedConstants(value)
        , scissorRect(value)
        , vertexShaderConstants(value)
        , vertexStreamFirst(value ? 0 : 255)
        , vertexStreamLast(value ? 15 : 0)
        , indices(value)
        , pixelShaderConstants(value)
    {
    }
};

static DirtyStates g_dirtyStates(true);

template<typename T>
static void SetDirtyValue(bool& dirtyState, T& dest, const T& src)
{
    if (dest != src)
    {
        dest = src;
        dirtyState = true;
    }
}

static constexpr size_t PROFILER_VALUE_COUNT = 256;
static size_t g_profilerValueIndex;

struct Profiler
{
    std::atomic<double> value;
    double values[PROFILER_VALUE_COUNT];
    std::chrono::steady_clock::time_point start;

    void Begin()
    {
        start = std::chrono::steady_clock::now();
    }

    void End()
    {
        value = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    void Set(double v)
    {
        value = v;
    }

    void Reset()
    {
        End();
        Begin();
    }

    double UpdateAndReturnAverage()
    {
        values[g_profilerValueIndex] = value;
        return std::accumulate(values, values + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
    }
};

static double g_applicationValues[PROFILER_VALUE_COUNT];
static Profiler g_gpuFrameProfiler;
static Profiler g_presentProfiler;
static Profiler g_updateDirectorProfiler;
static Profiler g_renderDirectorProfiler;
static Profiler g_frameFenceProfiler;
static Profiler g_presentWaitProfiler;
static Profiler g_swapChainAcquireProfiler;

static bool g_profilerVisible;
static bool g_profilerWasToggled;

#ifdef UNLEASHED_RECOMP_D3D12
static bool g_vulkan = false;
#else
static constexpr bool g_vulkan = true;
#endif

static bool g_triangleStripWorkaround = false;

static bool g_hardwareResolve = true;
static bool g_hardwareDepthResolve = true;

static std::unique_ptr<RenderInterface> g_interface;
static std::unique_ptr<RenderDevice> g_device;

static RenderDeviceCapabilities g_capabilities;

static constexpr size_t NUM_FRAMES = 2;
static constexpr size_t NUM_QUERIES = 2;

static uint32_t g_frame = 0;
static uint32_t g_nextFrame = 1;

static std::unique_ptr<RenderCommandQueue> g_queue;
static std::unique_ptr<RenderCommandList> g_commandLists[NUM_FRAMES];
static std::unique_ptr<RenderCommandFence> g_commandFences[NUM_FRAMES];
static std::unique_ptr<RenderQueryPool> g_queryPools[NUM_FRAMES];
static bool g_commandListStates[NUM_FRAMES];

static Mutex g_copyMutex;
static std::unique_ptr<RenderCommandQueue> g_copyQueue;
static std::unique_ptr<RenderCommandList> g_copyCommandList;
static std::unique_ptr<RenderCommandFence> g_copyCommandFence;

static std::unique_ptr<RenderSwapChain> g_swapChain;
static bool g_swapChainValid;

static constexpr RenderFormat BACKBUFFER_FORMAT = RenderFormat::B8G8R8A8_UNORM;

static std::unique_ptr<RenderCommandSemaphore> g_acquireSemaphores[NUM_FRAMES];
static std::unique_ptr<RenderCommandSemaphore> g_renderSemaphores[NUM_FRAMES];
static uint32_t g_backBufferIndex;
static std::unique_ptr<GuestSurface> g_backBufferHolder;
static GuestSurface* g_backBuffer;

static std::unique_ptr<RenderTexture> g_intermediaryBackBufferTexture;
static uint32_t g_intermediaryBackBufferTextureWidth;
static uint32_t g_intermediaryBackBufferTextureHeight;
static uint32_t g_intermediaryBackBufferTextureDescriptorIndex;

static std::unique_ptr<RenderPipeline> g_gammaCorrectionPipeline;

struct std::unique_ptr<RenderDescriptorSet> g_textureDescriptorSet;
struct std::unique_ptr<RenderDescriptorSet> g_samplerDescriptorSet;

enum
{
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D,
    TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE,
    TEXTURE_DESCRIPTOR_NULL_COUNT
};

struct TextureDescriptorAllocator
{
    Mutex mutex;
    uint32_t capacity = TEXTURE_DESCRIPTOR_NULL_COUNT;
    std::vector<uint32_t> freed;

    uint32_t allocate()
    {
        std::lock_guard lock(mutex);

        uint32_t value;
        if (!freed.empty())
        {
            value = freed.back();
            freed.pop_back();
        }
        else
        {
            value = capacity;
            ++capacity;
        }

        return value;
    }

    void free(uint32_t value)
    {
        assert(value != NULL);
        std::lock_guard lock(mutex);
        freed.push_back(value);
    }
};

static std::unique_ptr<RenderTexture> g_blankTextures[TEXTURE_DESCRIPTOR_NULL_COUNT];
static std::unique_ptr<RenderTextureView> g_blankTextureViews[TEXTURE_DESCRIPTOR_NULL_COUNT];

static TextureDescriptorAllocator g_textureDescriptorAllocator;

static std::unique_ptr<RenderPipelineLayout> g_pipelineLayout;
static xxHashMap<std::unique_ptr<RenderPipeline>> g_pipelines;

#ifdef ASYNC_PSO_DEBUG
static std::atomic<uint32_t> g_pipelinesCreatedInRenderThread;
static std::atomic<uint32_t> g_pipelinesCreatedAsynchronously;
static std::atomic<uint32_t> g_pipelinesDropped;
static std::atomic<uint32_t> g_pipelinesCurrentlyCompiling;
static std::string g_pipelineDebugText;
static Mutex g_debugMutex;
#endif

#ifdef PSO_CACHING
static xxHashMap<PipelineState> g_pipelineStatesToCache;
static Mutex g_pipelineCacheMutex;
#endif

static std::atomic<uint32_t> g_compilingPipelineTaskCount;
static std::atomic<uint32_t> g_pendingPipelineTaskCount;

enum class PipelineTaskType
{
    Null,
    DatabaseData,
    PrecompilePipelines,
    RecompilePipelines
};

struct PipelineTask
{
    PipelineTaskType type{};
    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;
};

static Mutex g_pipelineTaskMutex;
static std::vector<PipelineTask> g_pipelineTaskQueue;

static void EnqueuePipelineTask(PipelineTaskType type, const boost::shared_ptr<Hedgehog::Database::CDatabaseData>& databaseData)
{
    // Precompiled pipelines deliberately do not increment 
    // this counter to overlap the compilation with intro logos.
    if (type != PipelineTaskType::PrecompilePipelines)
        ++g_compilingPipelineTaskCount;

    {
        std::lock_guard lock(g_pipelineTaskMutex);
        g_pipelineTaskQueue.emplace_back(type, databaseData);
    }

    if ((++g_pendingPipelineTaskCount) == 1)
        g_pendingPipelineTaskCount.notify_one();
}

static const PipelineState g_pipelineStateCache[] =
{
#include "cache/pipeline_state_cache.h"
};

#include "cache/vertex_element_cache.h"

static uint8_t* const g_vertexDeclarationCache[] =
{
#include "cache/vertex_declaration_cache.h"
};

static xxHashMap<std::pair<uint32_t, std::unique_ptr<RenderSampler>>> g_samplerStates;

static Mutex g_vertexDeclarationMutex;
static xxHashMap<GuestVertexDeclaration*> g_vertexDeclarations;

struct UploadBuffer
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;

    std::unique_ptr<RenderBuffer> buffer;
    uint8_t* memory = nullptr;
    uint64_t deviceAddress = 0;
};

struct UploadAllocator
{
    std::vector<UploadBuffer> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;

    UploadAllocation allocate(uint32_t size, uint32_t alignment)
    {
        assert(size <= UploadBuffer::SIZE);

        offset = (offset + alignment - 1) & ~(alignment - 1);

        if (offset + size > UploadBuffer::SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer.buffer == nullptr)
        {
            buffer.buffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(UploadBuffer::SIZE, RenderBufferFlag::CONSTANT | RenderBufferFlag::VERTEX | RenderBufferFlag::INDEX));
            buffer.memory = reinterpret_cast<uint8_t*>(buffer.buffer->map());
            buffer.deviceAddress = buffer.buffer->getDeviceAddress();
        }
        
        auto ref = buffer.buffer->at(offset);
        offset += size;

        return { ref.ref, ref.offset, buffer.memory + ref.offset, buffer.deviceAddress + ref.offset };
    }

    template<bool TByteSwap, typename T>
    UploadAllocation allocate(const T* memory, uint32_t size, uint32_t alignment)
    {
        auto result = allocate(size, alignment);

        if constexpr (TByteSwap)
        {
            auto destination = reinterpret_cast<T*>(result.memory);

            for (size_t i = 0; i < size; i += sizeof(T))
            {
                *destination = ByteSwap(*memory);
                ++destination;
                ++memory;
            }
        }
        else
        {
            memcpy(result.memory, memory, size);
        }

        return result;
    }

    void reset()
    {
        index = 0;
        offset = 0;
    }
};

static UploadAllocator g_uploadAllocators[NUM_FRAMES];

struct IntermediaryUploadAllocator
{
    static constexpr size_t SIZE = 16 * 1024 * 1024;

    std::vector<std::unique_ptr<uint8_t[]>> buffers;
    uint32_t index = 0;
    uint32_t offset = 0;

    uint8_t* allocate(uint32_t size)
    {
        assert(size <= SIZE);

        if (offset + size > SIZE)
        {
            ++index;
            offset = 0;
        }

        if (buffers.size() <= index)
            buffers.resize(index + 1);

        auto& buffer = buffers[index];
        if (buffer == nullptr)
            buffer = std::make_unique_for_overwrite<uint8_t[]>(SIZE);

        auto result = buffer.get() + offset;
        offset += ((size + 0xF) & ~0xF);

        return result;
    }

    uint8_t* allocate(const void* memory, uint32_t size)
    {
        auto result = allocate(size);
        memcpy(result, memory, size);
        return result;
    }

    void reset()
    {
        index = 0;
        offset = 0;
    }
};

static IntermediaryUploadAllocator g_intermediaryUploadAllocator;

static std::vector<GuestResource*> g_tempResources[NUM_FRAMES];
static std::vector<std::unique_ptr<RenderBuffer>> g_tempBuffers[NUM_FRAMES];

template<GuestPrimitiveType PrimitiveType>
struct PrimitiveIndexData
{
    std::vector<uint16_t> indexData;
    RenderBufferReference indexBuffer;
    uint32_t currentIndexCount = 0;

    uint32_t prepare(uint32_t guestPrimCount)
    {
        uint32_t primCount;
        uint32_t indexCountPerPrimitive;

        switch (PrimitiveType)
        {
        case D3DPT_TRIANGLEFAN:
            primCount = guestPrimCount - 2;
            indexCountPerPrimitive = 3; 
            break;
        case D3DPT_QUADLIST:
            primCount = guestPrimCount / 4;
            indexCountPerPrimitive = 6;
            break;
        default:
            assert(false && "Unknown primitive type.");
            break;
        }

        uint32_t indexCount = primCount * indexCountPerPrimitive;

        if (indexData.size() < indexCount)
        {
            const size_t oldPrimCount = indexData.size() / indexCountPerPrimitive;
            indexData.resize(indexCount);

            for (size_t i = oldPrimCount; i < primCount; i++)
            {
                switch (PrimitiveType)
                {
                case D3DPT_TRIANGLEFAN:
                {
                    indexData[i * 3 + 0] = 0;
                    indexData[i * 3 + 1] = static_cast<uint16_t>(i + 1);
                    indexData[i * 3 + 2] = static_cast<uint16_t>(i + 2);
                    break;
                }
                case D3DPT_QUADLIST:
                {
                    indexData[i * 6 + 0] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 1] = static_cast<uint16_t>(i * 4 + 1);
                    indexData[i * 6 + 2] = static_cast<uint16_t>(i * 4 + 2);

                    indexData[i * 6 + 3] = static_cast<uint16_t>(i * 4 + 0);
                    indexData[i * 6 + 4] = static_cast<uint16_t>(i * 4 + 2);
                    indexData[i * 6 + 5] = static_cast<uint16_t>(i * 4 + 3);
                    break;
                }
                default:
                    assert(false && "Unknown primitive type.");
                    break;
                }
            }
        }

        if (indexBuffer == NULL || currentIndexCount < indexCount)
        {
            auto allocation = g_uploadAllocators[g_frame].allocate<false>(indexData.data(), indexCount * 2, 2);
            indexBuffer = allocation.buffer->at(allocation.offset);
            currentIndexCount = indexCount;
        }

        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, indexBuffer);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, indexCount * 2);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, RenderFormat::R16_UINT);

        return indexCount;
    }

    void reset()
    {
        indexBuffer = {};
        currentIndexCount = 0;
    }
};

static PrimitiveIndexData<D3DPT_TRIANGLEFAN> g_triangleFanIndexData;
static PrimitiveIndexData<D3DPT_QUADLIST> g_quadIndexData;

static void DestructTempResources()
{
    for (auto resource : g_tempResources[g_frame])
    {
        switch (resource->type)
        {
        case ResourceType::Texture:
        case ResourceType::VolumeTexture:
        {
            const auto texture = reinterpret_cast<GuestTexture*>(resource);

            if (texture->mappedMemory != nullptr)
                g_userHeap.Free(texture->mappedMemory);

            g_textureDescriptorAllocator.free(texture->descriptorIndex);

            if (texture->patchedTexture != nullptr)
                g_textureDescriptorAllocator.free(texture->patchedTexture->descriptorIndex); 
            
            if (texture->recreatedCubeMapTexture != nullptr)
                g_textureDescriptorAllocator.free(texture->recreatedCubeMapTexture->descriptorIndex);

            texture->~GuestTexture();
            break;
        }

        case ResourceType::VertexBuffer:
        case ResourceType::IndexBuffer:
        {
            const auto buffer = reinterpret_cast<GuestBuffer*>(resource);

            if (buffer->mappedMemory != nullptr)
                g_userHeap.Free(buffer->mappedMemory);

            buffer->~GuestBuffer();
            break;
        }

        case ResourceType::RenderTarget:
        case ResourceType::DepthStencil:
        {
            const auto surface = reinterpret_cast<GuestSurface*>(resource);

            if (surface->descriptorIndex != NULL)
                g_textureDescriptorAllocator.free(surface->descriptorIndex);

            surface->~GuestSurface();
            break;
        }

        case ResourceType::VertexDeclaration:
            reinterpret_cast<GuestVertexDeclaration*>(resource)->~GuestVertexDeclaration();
            break;

        case ResourceType::VertexShader:
        case ResourceType::PixelShader:
        {
            reinterpret_cast<GuestShader*>(resource)->~GuestShader();
            break;
        }
        }

        g_userHeap.Free(resource);
    }

    g_tempResources[g_frame].clear();
    g_tempBuffers[g_frame].clear();
}

static std::thread::id g_presentThreadId = std::this_thread::get_id();
static std::atomic<bool> g_readyForCommands;

PPC_FUNC_IMPL(__imp__sub_824ECA00);
PPC_FUNC(sub_824ECA00)
{
    // Guard against thread ownership changes when between command lists.
    g_readyForCommands.wait(false);
    g_presentThreadId = std::this_thread::get_id();
    __imp__sub_824ECA00(ctx, base);
}

static ankerl::unordered_dense::map<RenderTexture*, RenderTextureLayout> g_barrierMap;

static void AddBarrier(GuestBaseTexture* texture, RenderTextureLayout layout)
{
    if (texture != nullptr && texture->layout != layout)
    {
        g_barrierMap[texture->texture] = layout;
        texture->layout = layout;
    }
}

static std::vector<RenderTextureBarrier> g_barriers;

static void FlushBarriers()
{
    if (!g_barrierMap.empty())
    {
        for (auto& [texture, layout] : g_barrierMap)
            g_barriers.emplace_back(texture, layout);

        g_commandLists[g_frame]->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY, g_barriers);

        g_barrierMap.clear();
        g_barriers.clear();
    }
}

static std::unique_ptr<uint8_t[]> g_shaderCache;
static std::unique_ptr<uint8_t[]> g_buttonBcDiff;

static void LoadEmbeddedResources()
{
    if (g_vulkan)
    {
        g_shaderCache = std::make_unique<uint8_t[]>(g_spirvCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_spirvCacheDecompressedSize, g_compressedSpirvCache, g_spirvCacheCompressedSize);
    }
#ifdef UNLEASHED_RECOMP_D3D12
    else
    {
        g_shaderCache = std::make_unique<uint8_t[]>(g_dxilCacheDecompressedSize);
        ZSTD_decompress(g_shaderCache.get(), g_dxilCacheDecompressedSize, g_compressedDxilCache, g_dxilCacheCompressedSize);
    }
#endif

    g_buttonBcDiff = decompressZstd(g_button_bc_diff, g_button_bc_diff_uncompressed_size);
}

enum class CsdFilterState
{
    Unknown,
    On,
    Off
};

static CsdFilterState g_csdFilterState;

static ankerl::unordered_dense::set<GuestSurface*> g_pendingSurfaceCopies;
static ankerl::unordered_dense::set<GuestSurface*> g_pendingMsaaResolves;

enum class RenderCommandType
{
    SetRenderState,
    DestructResource,
    UnlockTextureRect,
    UnlockBuffer16,
    UnlockBuffer32,
    DrawImGui,
    ExecuteCommandList,
    BeginCommandList,
    StretchRect,
    SetRenderTarget,
    SetDepthStencilSurface,
    ExecutePendingStretchRectCommands,
    Clear,
    SetViewport,
    SetTexture,
    SetScissorRect,
    SetSamplerState,
    SetBooleans,
    SetVertexShaderConstants,
    SetPixelShaderConstants,
    AddPipeline,
    DrawPrimitive,
    DrawIndexedPrimitive,
    DrawPrimitiveUP,
    SetVertexDeclaration,
    SetVertexShader,
    SetStreamSource,
    SetIndices,
    SetPixelShader,
};

struct RenderCommand
{
    RenderCommandType type;
    union
    {
        struct
        {
            GuestRenderState type;
            uint32_t value;
        } setRenderState;

        struct 
        {
            GuestResource* resource;
        } destructResource;

        struct
        {
            GuestTexture* texture;
        } unlockTextureRect;

        struct
        {
            GuestBuffer* buffer;
        } unlockBuffer;

        struct 
        {
            GuestDevice* device;
            uint32_t flags;
            GuestTexture* texture;
        } stretchRect;

        struct 
        {
            GuestSurface* renderTarget;
        } setRenderTarget;

        struct 
        {
            GuestSurface* depthStencil;
        } setDepthStencilSurface;

        struct 
        {
            uint32_t flags;
            float color[4];
            float z;
        } clear;

        struct 
        {
            float x;
            float y;
            float width;
            float height;
            float minDepth;
            float maxDepth;
        } setViewport;

        struct 
        {
            uint32_t index;
            GuestTexture* texture;
        } setTexture;

        struct 
        {
            int32_t left;
            int32_t top;
            int32_t right;
            int32_t bottom;
        } setScissorRect;

        struct
        {
            uint32_t index;
            uint32_t data0;
            uint32_t data3;
            uint32_t data5;
        } setSamplerState;

        struct
        {
            uint32_t booleans;
        } setBooleans;

        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setVertexShaderConstants;  
        
        struct
        {
            uint8_t* memory;
            uint32_t index;
            uint32_t size;
        } setPixelShaderConstants;

        struct
        {
            XXH64_hash_t hash;
            RenderPipeline* pipeline;
        } addPipeline;

        struct 
        {
            uint32_t primitiveType; 
            uint32_t startVertex; 
            uint32_t primitiveCount;
        } drawPrimitive;

        struct 
        {
            uint32_t primitiveType;
            int32_t baseVertexIndex; 
            uint32_t startIndex;
            uint32_t primCount;
        } drawIndexedPrimitive;

        struct 
        {
            uint32_t primitiveType;
            uint32_t primitiveCount; 
            uint8_t* vertexStreamZeroData;
            uint32_t vertexStreamZeroSize;
            uint32_t vertexStreamZeroStride;
            CsdFilterState csdFilterState;
        } drawPrimitiveUP;

        struct 
        {
            GuestVertexDeclaration* vertexDeclaration;
        } setVertexDeclaration;

        struct 
        {
            GuestShader* shader;
        } setVertexShader;

        struct 
        {
            uint32_t index;
            GuestBuffer* buffer;
            uint32_t offset;
            uint32_t stride;
        } setStreamSource;

        struct 
        {
            GuestBuffer* buffer;
        } setIndices;

        struct 
        {
            GuestShader* shader;
        } setPixelShader;
    };
};

static moodycamel::BlockingConcurrentQueue<RenderCommand> g_renderQueue;

template<GuestRenderState TType>
static void SetRenderState(GuestDevice* device, uint32_t value)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetRenderState;
    cmd.setRenderState.type = TType;
    cmd.setRenderState.value = value;
    g_renderQueue.enqueue(cmd);
}

static void SetRenderStateUnimplemented(GuestDevice* device, uint32_t value)
{
}

static void SetAlphaTestMode(bool enable)
{
    uint32_t specConstants = 0;
    bool enableAlphaToCoverage = false;

    if (enable)
    {
        enableAlphaToCoverage = Config::TransparencyAntiAliasing && g_renderTarget != nullptr && g_renderTarget->sampleCount != RenderSampleCount::COUNT_1;

        if (enableAlphaToCoverage)
            specConstants = SPEC_CONSTANT_ALPHA_TO_COVERAGE;
        else
            specConstants = SPEC_CONSTANT_ALPHA_TEST;
    }

    specConstants |= (g_pipelineState.specConstants & ~(SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE));

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.enableAlphaToCoverage, enableAlphaToCoverage);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
}

static RenderBlend ConvertBlendMode(uint32_t blendMode)
{
    switch (blendMode)
    {
    case D3DBLEND_ZERO:
        return RenderBlend::ZERO;
    case D3DBLEND_ONE:
        return RenderBlend::ONE;
    case D3DBLEND_SRCCOLOR:
        return RenderBlend::SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:
        return RenderBlend::INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA:
        return RenderBlend::SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:
        return RenderBlend::INV_SRC_ALPHA;
    case D3DBLEND_DESTCOLOR:
        return RenderBlend::DEST_COLOR;
    case D3DBLEND_INVDESTCOLOR:
        return RenderBlend::INV_DEST_COLOR;
    case D3DBLEND_DESTALPHA:
        return RenderBlend::DEST_ALPHA;
    case D3DBLEND_INVDESTALPHA:
        return RenderBlend::INV_DEST_ALPHA;
    default:
        assert(false && "Invalid blend mode");
        return RenderBlend::ZERO;
    }
}

static RenderBlendOperation ConvertBlendOp(uint32_t blendOp)
{
    switch (blendOp)
    {
    case D3DBLENDOP_ADD:
        return RenderBlendOperation::ADD;
    case D3DBLENDOP_SUBTRACT:
        return RenderBlendOperation::SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT:
        return RenderBlendOperation::REV_SUBTRACT;
    case D3DBLENDOP_MIN:
        return RenderBlendOperation::MIN;
    case D3DBLENDOP_MAX:
        return RenderBlendOperation::MAX;
    default:
        assert(false && "Unknown blend operation");
        return RenderBlendOperation::ADD;
    }
}

static void ProcSetRenderState(const RenderCommand& cmd)
{
    uint32_t value = cmd.setRenderState.value;

    switch (cmd.setRenderState.type)
    {
    case D3DRS_ZENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zEnable, value != 0);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    case D3DRS_ZWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zWriteEnable, value != 0);
        break;
    }
    case D3DRS_ALPHATESTENABLE:
    {
        SetAlphaTestMode(value != 0);
        break;
    }
    case D3DRS_SRCBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLEND:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlend, ConvertBlendMode(value));
        break;
    }
    case D3DRS_CULLMODE:
    {
        RenderCullMode cullMode;

        switch (value) {
        case D3DCULL_NONE:
        case D3DCULL_NONE_2:
            cullMode = RenderCullMode::NONE;
            break;
        case D3DCULL_CW:
            cullMode = RenderCullMode::FRONT;
            break;
        case D3DCULL_CCW:
            cullMode = RenderCullMode::BACK;
            break;
        default:
            assert(false && "Invalid cull mode");
            cullMode = RenderCullMode::NONE;
            break;
        }

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.cullMode, cullMode);
        break;
    }
    case D3DRS_ZFUNC:
    {
        RenderComparisonFunction comparisonFunc;

        switch (value)
        {
        case D3DCMP_NEVER:
            comparisonFunc = RenderComparisonFunction::NEVER;
            break;
        case D3DCMP_LESS:
            comparisonFunc = RenderComparisonFunction::LESS;
            break;
        case D3DCMP_EQUAL:
            comparisonFunc = RenderComparisonFunction::EQUAL;
            break;
        case D3DCMP_LESSEQUAL:
            comparisonFunc = RenderComparisonFunction::LESS_EQUAL;
            break;
        case D3DCMP_GREATER:
            comparisonFunc = RenderComparisonFunction::GREATER;
            break;
        case D3DCMP_NOTEQUAL:
            comparisonFunc = RenderComparisonFunction::NOT_EQUAL;
            break;
        case D3DCMP_GREATEREQUAL:
            comparisonFunc = RenderComparisonFunction::GREATER_EQUAL;
            break;
        case D3DCMP_ALWAYS:
            comparisonFunc = RenderComparisonFunction::ALWAYS;
            break;
        default:
            assert(false && "Unknown comparison function");
            comparisonFunc = RenderComparisonFunction::NEVER;
            break;
        }

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.zFunc, comparisonFunc);
        break;
    }
    case D3DRS_ALPHAREF:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_sharedConstants.alphaThreshold, float(value) / 256.0f);
        break;
    }
    case D3DRS_ALPHABLENDENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.alphaBlendEnable, value != 0);
        break;
    }
    case D3DRS_BLENDOP:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOp, ConvertBlendOp(value));
        break;
    }
    case D3DRS_SCISSORTESTENABLE:
    {
        SetDirtyValue(g_dirtyStates.scissorRect, g_scissorTestEnable, value != 0);
        break;
    }
    case D3DRS_SLOPESCALEDEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_slopeScaledDepthBias, *reinterpret_cast<float*>(&value));
        else 
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, *reinterpret_cast<float*>(&value));

        break;
    }
    case D3DRS_DEPTHBIAS:
    {
        if (g_capabilities.dynamicDepthBias)
            SetDirtyValue(g_dirtyStates.depthBias, g_depthBias, int32_t(*reinterpret_cast<float*>(&value) * (1 << 24)));
        else
            SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, int32_t(*reinterpret_cast<float*>(&value)* (1 << 24)));

        break;
    }
    case D3DRS_SRCBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.srcBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_DESTBLENDALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.destBlendAlpha, ConvertBlendMode(value));
        break;
    }
    case D3DRS_BLENDOPALPHA:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.blendOpAlpha, ConvertBlendOp(value));
        break;
    }
    case D3DRS_COLORWRITEENABLE:
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.colorWriteEnable, value);
        g_dirtyStates.renderTargetAndDepthStencil |= g_dirtyStates.pipelineState;
        break;
    }
    }
}

static const std::pair<GuestRenderState, PPCFunc*> g_setRenderStateFunctions[] =
{
    { D3DRS_ZENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZENABLE>> },
    { D3DRS_ZWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_ZWRITEENABLE>> },
    { D3DRS_ALPHATESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHATESTENABLE>> },
    { D3DRS_SRCBLEND, HostToGuestFunction<SetRenderState<D3DRS_SRCBLEND>> },
    { D3DRS_DESTBLEND, HostToGuestFunction<SetRenderState<D3DRS_DESTBLEND>> },
    { D3DRS_CULLMODE, HostToGuestFunction<SetRenderState<D3DRS_CULLMODE>> },
    { D3DRS_ZFUNC, HostToGuestFunction<SetRenderState<D3DRS_ZFUNC>> },
    { D3DRS_ALPHAREF, HostToGuestFunction<SetRenderState<D3DRS_ALPHAREF>> },
    { D3DRS_ALPHABLENDENABLE, HostToGuestFunction<SetRenderState<D3DRS_ALPHABLENDENABLE>> },
    { D3DRS_BLENDOP, HostToGuestFunction<SetRenderState<D3DRS_BLENDOP>> },
    { D3DRS_SCISSORTESTENABLE, HostToGuestFunction<SetRenderState<D3DRS_SCISSORTESTENABLE>> },
    { D3DRS_SLOPESCALEDEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_SLOPESCALEDEPTHBIAS>> },
    { D3DRS_DEPTHBIAS, HostToGuestFunction<SetRenderState<D3DRS_DEPTHBIAS>> },
    { D3DRS_SRCBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_SRCBLENDALPHA>> },
    { D3DRS_DESTBLENDALPHA, HostToGuestFunction<SetRenderState<D3DRS_DESTBLENDALPHA>> },
    { D3DRS_BLENDOPALPHA, HostToGuestFunction<SetRenderState<D3DRS_BLENDOPALPHA>> },
    { D3DRS_COLORWRITEENABLE, HostToGuestFunction<SetRenderState<D3DRS_COLORWRITEENABLE>> }
};

static std::unique_ptr<RenderShader> g_copyShader;

static std::unique_ptr<RenderShader> g_copyColorShader;
static ankerl::unordered_dense::map<RenderFormat, std::unique_ptr<RenderPipeline>> g_copyColorPipelines;
static std::unique_ptr<RenderPipeline> g_copyDepthPipeline;

static std::unique_ptr<RenderShader> g_resolveMsaaColorShaders[3];
static ankerl::unordered_dense::map<RenderFormat, std::array<std::unique_ptr<RenderPipeline>, 3>> g_resolveMsaaColorPipelines;
static std::unique_ptr<RenderPipeline> g_resolveMsaaDepthPipelines[3];

enum
{
    GAUSSIAN_BLUR_3X3,
    GAUSSIAN_BLUR_5X5,
    GAUSSIAN_BLUR_7X7,
    GAUSSIAN_BLUR_9X9,
    GAUSSIAN_BLUR_COUNT
};

static std::unique_ptr<GuestShader> g_gaussianBlurShaders[GAUSSIAN_BLUR_COUNT];

static std::unique_ptr<GuestShader> g_csdFilterShader;
static GuestShader* g_csdShader;

static std::unique_ptr<GuestShader> g_enhancedMotionBlurShader;

#ifdef UNLEASHED_RECOMP_D3D12

#define CREATE_SHADER(NAME) \
    g_device->createShader( \
        g_vulkan ? g_##NAME##_spirv : g_##NAME##_dxil, \
        g_vulkan ? sizeof(g_##NAME##_spirv) : sizeof(g_##NAME##_dxil), \
        "main", \
        g_vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL)

#else

#define CREATE_SHADER(NAME) \
    g_device->createShader(g_##NAME##_spirv, sizeof(g_##NAME##_spirv), "main", RenderShaderFormat::SPIRV)

#endif

#ifdef _WIN32
static bool DetectWine()
{
    HMODULE dllHandle = GetModuleHandle("ntdll.dll");
    return dllHandle != nullptr && GetProcAddress(dllHandle, "wine_get_version") != nullptr;
}
#endif

static constexpr size_t TEXTURE_DESCRIPTOR_SIZE = 65536;
static constexpr size_t SAMPLER_DESCRIPTOR_SIZE = 1024;

static std::unique_ptr<GuestTexture> g_imFontTexture;
static std::unique_ptr<RenderPipelineLayout> g_imPipelineLayout;
static std::unique_ptr<RenderPipeline> g_imPipeline;
static std::unique_ptr<RenderPipeline> g_imAdditivePipeline;

template<typename T>
static void ExecuteCopyCommandList(const T& function)
{
    std::lock_guard lock(g_copyMutex);

    g_copyCommandList->begin();
    function();
    g_copyCommandList->end();
    g_copyQueue->executeCommandLists(g_copyCommandList.get(), g_copyCommandFence.get());
    g_copyQueue->waitForCommandFence(g_copyCommandFence.get());
}

static constexpr uint32_t PITCH_ALIGNMENT = 0x100;
static constexpr uint32_t PLACEMENT_ALIGNMENT = 0x200;

struct ImGuiPushConstants
{
    ImVec2 boundsMin{};
    ImVec2 boundsMax{};
    ImU32 gradientTopLeft{};
    ImU32 gradientTopRight{};
    ImU32 gradientBottomRight{};
    ImU32 gradientBottomLeft{};
    uint32_t shaderModifier{};
    uint32_t texture2DDescriptorIndex{};
    ImVec2 displaySize{};
    ImVec2 inverseDisplaySize{};
    ImVec2 origin{ 0.0f, 0.0f };
    ImVec2 scale{ 1.0f, 1.0f };
    ImVec2 proceduralOrigin{ 0.0f, 0.0f };
    float outline{};
};

extern ImFontBuilderIO g_fontBuilderIO;

// AP_UPDATER_V4_BEGIN - Game-only updater. APWorld is notification-only.
// Fixed GitHub repository; release URL and SHA-256 must match the trusted manifest.
#ifdef _WIN32
namespace APUpdateV1
{
    constexpr const char* kClientVersion = "0.8.2"; // First public release after updater test.
    constexpr const char* kAPWorldVersion = "0.8.2";
    constexpr const char* kReleasePage =
        "https://github.com/ZMGBZak/Sonic-Unleashed-Recompiled-Archipelago-Updates/releases";
    constexpr const char* kAssetRoot =
        "https://github.com/ZMGBZak/Sonic-Unleashed-Recompiled-Archipelago-Updates/releases/download/";
    constexpr const wchar_t* kReleasePathRoot =
        L"/ZMGBZak/Sonic-Unleashed-Recompiled-Archipelago-Updates/releases/download/";

    // Manifest status: 0 idle / 1 checking / 2 complete / 3 error
    static std::atomic<int> status{0};
    // Binary status: 0 idle / 1 downloading / 2 verified / 3 error / 4 closing game
    static std::atomic<int> downloadStatus{0};
    static std::atomic<std::uint64_t> downloadedBytes{0};
    static std::atomic<std::uint64_t> totalBytes{0};
    static std::mutex resultMutex;
    static std::string latestClient;
    static std::string latestAPWorld;
    static std::string latestChecksum;
    static std::string latestUrl;
    static std::uint64_t latestAssetSize = 0;
    static std::string message;
    static std::string downloadMessage;

    static bool ParseVersion(const std::string& text, unsigned int (&parts)[3])
    {
        if (text.empty() || text.size() > 24) return false;
        for (char c : text) if (!((c >= '0' && c <= '9') || c == '.')) return false;
        unsigned int a = 0, b = 0, c = 0;
        char trailing = '\0';
        if (std::sscanf(text.c_str(), "%u.%u.%u%c", &a, &b, &c, &trailing) != 3) return false;
        parts[0] = a; parts[1] = b; parts[2] = c;
        return true;
    }
    static bool IsNewerVersion(const std::string& incoming, const std::string& current)
    {
        unsigned int n[3] = {}, c[3] = {};
        if (!ParseVersion(incoming, n) || !ParseVersion(current, c)) return false;
        for (int i = 0; i < 3; ++i)
        {
            if (n[i] > c[i]) return true;
            if (n[i] < c[i]) return false;
        }
        return false;
    }
    static bool IsExpectedAssetUrl(const std::string& url, const std::string& version)
    {
        unsigned int parts[3]{};
        if (!ParseVersion(version, parts)) return false;
        return url == std::string(kAssetRoot) + "V" + version + "/Sonic.X.Archipelago.V" + version + ".exe";
    }
    static std::filesystem::path StagedPath()
    {
        auto result = APSelfUpdate::CurrentExePath();
        return result.empty() ? result : std::filesystem::path(result.wstring() + L".apupdate.new");
    }

    static bool ReadManifest(std::string& out, std::string& error)
    {
        HINTERNET session = WinHttpOpen(L"SonicUnleashedArchipelagoUpdater/0.8.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { error = "Cannot initialize HTTPS"; return false; }
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
        HINTERNET connection = WinHttpConnect(session, L"raw.githubusercontent.com",
            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connection) { WinHttpCloseHandle(session); error = "Cannot connect to GitHub"; return false; }
        HINTERNET request = WinHttpOpenRequest(connection, L"GET",
            L"/ZMGBZak/Sonic-Unleashed-Recompiled-Archipelago-Updates/main/updates/manifest.json",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
        DWORD httpCode = 0, size = sizeof(httpCode);
        if (ok) ok = !!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &httpCode, &size, WINHTTP_NO_HEADER_INDEX);
        if (!ok || httpCode != 200)
        {
            error = httpCode == 404 ? "No public update manifest found" : "Could not fetch GitHub update manifest";
            ok = false;
        }
        while (ok)
        {
            char buffer[4096];
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) { ok = false; break; }
            if (!read) break;
            if (out.size() + read > 65536) { ok = false; break; }
            out.append(buffer, read);
        }
        if (request) WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (!ok && error.empty()) error = "GitHub connection interrupted";
        return ok;
    }

    static void StartCheck()
    {
        if (status.exchange(1) == 1) return;
        downloadStatus.store(0);
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            downloadMessage.clear();
        }
        std::thread([]()
        {
            std::string body, error, client, apworld, sha, url;
            std::uint64_t assetSize = 0;
            bool ok = ReadManifest(body, error);
            if (ok)
            {
                try
                {
                    auto j = nlohmann::json::parse(body);
                    if (j.at("schema_version").get<int>() != 1)
                        throw std::runtime_error("unsupported schema");
                    client = j.at("client_version").get<std::string>();
                    apworld = j.at("apworld_version").get<std::string>();
                    unsigned int test[3]{};
                    if (!ParseVersion(client, test) || !ParseVersion(apworld, test))
                        throw std::runtime_error("invalid version");
                    if (j.contains("client_sha256") && j["client_sha256"].is_string())
                        sha = j["client_sha256"].get<std::string>();
                    if (j.contains("client_download_url") && j["client_download_url"].is_string())
                        url = j["client_download_url"].get<std::string>();
                    if (j.contains("client_asset_size") && j["client_asset_size"].is_number_unsigned())
                        assetSize = j["client_asset_size"].get<std::uint64_t>();
                }
                catch (...) { ok = false; error = "Invalid update manifest"; }
            }
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                latestClient = client;
                latestAPWorld = apworld;
                latestChecksum = sha;
                latestUrl = url;
                latestAssetSize = assetSize;
                message = error;
            }
            status.store(ok ? 2 : 3, std::memory_order_release);
        }).detach();
    }

    // GitHub release asset download, streamed to the game EXE's directory.
    // The asset path is reconstructed from the parsed version; no arbitrary URL is requested.
    static bool DownloadBinary(const std::string& version,
        const std::string& expectedSha, std::uint64_t expectedSize, std::string& error)
    {
        const auto destination = StagedPath();
        if (destination.empty()) { error = "Cannot locate the game EXE"; return false; }
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        HINTERNET session = WinHttpOpen(L"SonicUnleashedArchipelagoUpdater/0.8.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { error = "Cannot initialize secure download"; return false; }
        WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
        HINTERNET connection = WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        const std::wstring path = std::wstring(kReleasePathRoot) + L"V" +
            std::wstring(version.begin(), version.end()) + L"/Sonic.X.Archipelago.V" +
            std::wstring(version.begin(), version.end()) + L".exe";
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
        DWORD code = 0, codeSize = sizeof(code);
        if (ok) ok = !!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
        if (!ok || code != 200)
        {
            error = code == 404 ? "GitHub release EXE not found" : "Release download failed";
            ok = false;
        }
        std::uint64_t advertised = 0;
        if (ok)
        {
            wchar_t lengthText[64]{};
            DWORD lengthSize = sizeof(lengthText);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
            {
                wchar_t* end = nullptr;
                advertised = std::wcstoull(lengthText, &end, 10);
                if (!end || *end != L'\0') advertised = 0;
            }
        }
        constexpr std::uint64_t kLimit = 1024ull * 1024ull * 1024ull; // 1 GiB safety cap.
        if (advertised > kLimit || expectedSize > kLimit) { ok = false; error = "Update exceeds safety size limit"; }
        if (ok && advertised && expectedSize && advertised != expectedSize)
            { ok = false; error = "Download size does not match the manifest"; }
        totalBytes.store(advertised);
        downloadedBytes.store(0);
        std::ofstream file;
        if (ok)
        {
            file.open(destination, std::ios::binary | std::ios::trunc);
            if (!file) { ok = false; error = "Cannot write beside game EXE"; }
        }
        std::uint64_t total = 0;
        while (ok)
        {
            char buffer[65536];
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer, sizeof(buffer), &read))
            {
                ok = false; error = "Download interrupted"; break;
            }
            if (!read) break;
            total += read;
            if (total > kLimit || (advertised && total > advertised))
            {
                ok = false; error = "Downloaded file has invalid size"; break;
            }
            file.write(buffer, read);
            if (!file) { ok = false; error = "Disk write failed"; break; }
            downloadedBytes.store(total);
        }
        file.close();
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (ok && total == 0) { ok = false; error = "Empty update file"; }
        if (ok && expectedSize && total != expectedSize)
            { ok = false; error = "Incomplete or incorrect update size"; }
        if (ok && advertised && total != advertised)
            { ok = false; error = "Incomplete update download"; }
        if (ok)
        {
            std::string actual;
            ok = APSelfUpdate::Sha256File(destination, actual) &&
                APSelfUpdate::LowerAscii(actual) == APSelfUpdate::LowerAscii(expectedSha);
            if (!ok) error = "SHA-256 verification failed (update not installed)";
        }
        if (!ok) std::filesystem::remove(destination, ec);
        return ok;
    }

    static void StartDownload(const std::string& version, const std::string& sha, std::uint64_t expectedSize)
    {
        if (downloadStatus.load() == 1 || downloadStatus.load() == 4) return;
        downloadStatus.store(1);
        std::thread([version, sha, expectedSize]()
        {
            std::string error;
            const bool ok = DownloadBinary(version, sha, expectedSize, error);
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                downloadMessage = error;
            }
            downloadStatus.store(ok ? 2 : 3, std::memory_order_release);
        }).detach();
    }

    static void DrawControls()
    {
        ImGui::Text("Installed Client Version: %s Beta", kClientVersion);
        ImGui::Separator();
        const bool busy = status.load() == 1 || downloadStatus.load() == 1 || downloadStatus.load() == 4;
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("CHECK FOR UPDATES", ImVec2(-1, 40))) StartCheck();
        // Existing F8 glossy border retained.
        {
            const ImVec2 p = ImGui::GetItemRectMin();
            const ImVec2 s = ImGui::GetItemRectSize();
            const ImVec2 bottom(p.x + s.x, p.y + s.y);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(ImVec2(p.x + 5.0f, p.y + 4.0f),
                ImVec2(bottom.x - 5.0f, p.y + s.y * 0.38f), IM_COL32(180,225,255,55), 10.0f);
            draw->AddRect(ImVec2(p.x + 1.0f,p.y + 1.0f), ImVec2(bottom.x - 1.0f,bottom.y - 1.0f),
                IM_COL32(160,215,245,235), 13.0f, 0, 2.5f);
            draw->AddRect(ImVec2(p.x + 4.0f,p.y + 4.0f), ImVec2(bottom.x - 4.0f,bottom.y - 4.0f),
                IM_COL32(15,55,105,210), 10.0f, 0, 1.5f);
            draw->AddLine(ImVec2(p.x + 17.0f,p.y + 2.0f), ImVec2(bottom.x - 17.0f,p.y + 2.0f),
                IM_COL32(225,245,255,170), 1.5f);
        }
        ImGui::EndDisabled();
        const int state = status.load();
        if (state == 1) ImGui::TextUnformatted("Checking GitHub...");
        if (state == 3)
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            ImGui::TextWrapped("Update check: %s", message.c_str());
        }
        if (state == 2)
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            const bool clientNew = IsNewerVersion(latestClient, kClientVersion);
            const bool worldNew = IsNewerVersion(latestAPWorld, kAPWorldVersion);
            const bool hasTrustedRelease =
                IsExpectedAssetUrl(latestUrl, latestClient) &&
                APSelfUpdate::IsHexSha256(latestChecksum);
            ImGui::Text("Client: %s / latest: %s", kClientVersion, latestClient.c_str());
            ImGui::Text("APWorld: %s / latest: %s", kAPWorldVersion, latestAPWorld.c_str());
            if (!clientNew && !worldNew) ImGui::TextUnformatted("You're up to date!");
            if (clientNew && !hasTrustedRelease)
            {
                ImGui::TextWrapped("New game version found. Automatic download is not published yet.");
                if (ImGui::Button("VIEW GAME RELEASE", ImVec2(-1, 34))) SDL_OpenURL(kReleasePage);
            }
            if (clientNew && hasTrustedRelease)
            {
                const int d = downloadStatus.load();
                if (d == 0 || d == 3)
                {
                    if (ImGui::Button("DOWNLOAD GAME UPDATE", ImVec2(-1, 38)))
                        StartDownload(latestClient, latestChecksum, latestAssetSize);
                }
                if (d == 1)
                {
                    const auto count = downloadedBytes.load();
                    const auto total = totalBytes.load();
                    const float progress = total ?
                        std::clamp(static_cast<float>(count) / static_cast<float>(total), 0.0f, 1.0f) : 0.0f;
                    ImGui::ProgressBar(progress, ImVec2(-1, 16), total ? nullptr : "Downloading...");
                    ImGui::Text("Downloaded: %.1f MB%s", count / (1024.0 * 1024.0),
                        total ? "" : " (total unknown)");
                }
                if (d == 3) ImGui::TextWrapped("Download failed: %s", downloadMessage.c_str());
                if (d == 2)
                {
                    ImGui::TextWrapped("Verified update ready. Close game to install and restart.");
                    if (ImGui::Button("INSTALL UPDATE AND RESTART", ImVec2(-1, 40)))
                    {
                        std::string error;
                        if (APSelfUpdate::LaunchHelper(StagedPath(), latestChecksum, error))
                        {
                            downloadStatus.store(4);
                            SDL_Event event{};
                            event.type = SDL_QUIT;
                            SDL_PushEvent(&event);
                        }
                        else downloadMessage = error;
                    }
                    if (!downloadMessage.empty()) ImGui::TextWrapped("%s", downloadMessage.c_str());
                }
                if (d == 4) ImGui::TextUnformatted("Closing game for update...");
            }
            if (worldNew)
            {
                ImGui::TextWrapped("New APWorld! Download and install it manually from GitHub.");
                if (ImGui::Button("VIEW APWORLD DOWNLOAD", ImVec2(-1, 38)))
                    SDL_OpenURL(kReleasePage);
            }
        }
    }
}
#endif
// AP_UPDATER_V4_END
// AP_RING_ROULETTE_V2_SKIN_BEGIN - cosmetic-only changes to V1 game rewards.
// PNG and WAV bytes are compiled into the game executable.
// No separate RingRoulette folder is needed.
extern bool g_apConnectorVisible;
namespace APRouletteSkin
{
    constexpr int kSymbolCount = 8;
    constexpr int kSoundCount = 5;
    enum SoundId { Start = 0, Tick = 1, Stop = 2, Win = 3, Jackpot = 4 };
    static std::unique_ptr<GuestTexture> artwork;
    static std::unique_ptr<GuestTexture> readyArt;
    static std::unique_ptr<GuestTexture> icons[kSymbolCount];
    static bool artworkAttempted = false;
    static bool settingsLoaded = false;
    static bool soundEnabled = true;
    static int volume = 55;
    // V0.8.2: movable/resizable HUD; percentages of the available safe screen area.
    static int hudSizePercent = 100;
    static int hudXPercent = 100;
    static int hudYPercent = 0;
    static SDL_AudioDeviceID soundDevice = 0;
    static bool soundDeviceFailed = false;
    struct SoundData { bool attempted = false; std::vector<Uint8> pcm; };
    static SoundData sounds[kSoundCount];

    static std::filesystem::path SettingsPath()
    {
        return APRingRoulette::SettingsPath().parent_path() / "ap_ring_roulette_skin.json";
    }

    static void ReadSettings()
    {
        if (settingsLoaded) return;
        settingsLoaded = true;
        try
        {
            std::ifstream file(SettingsPath());
            if (!file) return;
            const auto data = nlohmann::json::parse(file, nullptr, false);
            if (!data.is_object()) return;
            if (data.contains("sound_enabled") && data["sound_enabled"].is_boolean())
                soundEnabled = data["sound_enabled"].get<bool>();
            if (data.contains("volume") && data["volume"].is_number_integer())
                volume = std::clamp(data["volume"].get<int>(), 0, 100);
            if (data.contains("hud_size_percent") && data["hud_size_percent"].is_number_integer())
                hudSizePercent = std::clamp(data["hud_size_percent"].get<int>(), 50, 175);
            if (data.contains("hud_x_percent") && data["hud_x_percent"].is_number_integer())
                hudXPercent = std::clamp(data["hud_x_percent"].get<int>(), 0, 100);
            if (data.contains("hud_y_percent") && data["hud_y_percent"].is_number_integer())
                hudYPercent = std::clamp(data["hud_y_percent"].get<int>(), 0, 100);
        }
        catch (...) { /* Bad/missing cosmetic settings should never affect gameplay. */ }
    }

    static void SaveSettings()
    {
        try
        {
            auto path = SettingsPath();
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::trunc);
            if (file) file << nlohmann::json({
                {"sound_enabled",soundEnabled}, {"volume",volume},
                {"hud_size_percent",hudSizePercent},
                {"hud_x_percent",hudXPercent}, {"hud_y_percent",hudYPercent}
            }).dump(2) << '\n';
        }
        catch (...) { /* Cosmetic preference save is optional. */ }
    }

    static std::unique_ptr<GuestTexture> ReadImage(APRouletteEmbedded::Asset asset)
    {
        if (!asset.bytes || !asset.size) return nullptr;
        return LoadTexture(asset.bytes, asset.size);
    }

    static void LoadArtwork()
    {
        if (artworkAttempted) return;
        artworkAttempted = true;
        try
        {
            artwork = ReadImage(APRouletteEmbedded::frame);
            readyArt = ReadImage(APRouletteEmbedded::ready);
            for (int i = 0; i < kSymbolCount; ++i)
                icons[i] = ReadImage(APRouletteEmbedded::symbols[i]);
            if (!artwork) LOGN("AP ROULETTE EMBEDDED: frame decoding failed; text fallback active");
            else LOGN("AP ROULETTE EMBEDDED: artwork loaded from executable");
        }
        catch (...) { LOGN("AP ROULETTE EMBEDDED: image loading failed; text fallback active"); }
    }

    static bool HasFrame() { LoadArtwork(); return artwork != nullptr; }
    static GuestTexture* Icon(APRingRoulette::Prize prize)
    {
        const int index = static_cast<int>(prize) - 1;
        return (index >= 0 && index < kSymbolCount) ? icons[index].get() : nullptr;
    }

    static bool StartSoundDevice()
    {
        if (soundDevice) return true;
        if (soundDeviceFailed) return false;
        SDL_AudioSpec desired{};
        desired.freq = 44100;
        desired.format = AUDIO_F32SYS;
        desired.channels = 2;
        desired.samples = 1024;
        desired.callback = nullptr;
        soundDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (!soundDevice)
        {
            soundDeviceFailed = true;
            LOGFN("AP ROULETTE V2: sound disabled (SDL): {}", SDL_GetError());
            return false;
        }
        SDL_PauseAudioDevice(soundDevice, 0);
        return true;
    }

    static bool LoadSound(int id)
    {
        auto& slot = sounds[id];
        if (slot.attempted) return !slot.pcm.empty();
        slot.attempted = true;
        try
        {
            SDL_AudioSpec inputSpec{};
            Uint8* wavData = nullptr;
            Uint32 wavLength = 0;
            const auto& data = APRouletteEmbedded::sounds[id];
            if (!data.bytes || data.size == 0 || data.size > static_cast<size_t>((std::numeric_limits<int>::max)())) return false;
            SDL_RWops* rw = SDL_RWFromConstMem(data.bytes, static_cast<int>(data.size));
            if (!rw) return false;
            // SDL frees RWops after decoding; embedded data remains compiled into EXE.
            if (!SDL_LoadWAV_RW(rw, 1, &inputSpec, &wavData, &wavLength)) return false;
            SDL_AudioCVT cvt{};
            const int needConversion = SDL_BuildAudioCVT(&cvt, inputSpec.format,
                inputSpec.channels, inputSpec.freq, AUDIO_F32SYS, 2, 44100);
            if (needConversion < 0)
            {
                SDL_FreeWAV(wavData);
                return false;
            }
            if (!needConversion)
                slot.pcm.assign(wavData, wavData + wavLength);
            else
            {
                cvt.len = static_cast<int>(wavLength);
                cvt.buf = static_cast<Uint8*>(SDL_malloc(static_cast<size_t>(cvt.len) * cvt.len_mult));
                if (cvt.buf)
                {
                    std::memcpy(cvt.buf, wavData, wavLength);
                    if (SDL_ConvertAudio(&cvt) == 0)
                        slot.pcm.assign(cvt.buf, cvt.buf + cvt.len_cvt);
                    SDL_free(cvt.buf);
                }
            }
            SDL_FreeWAV(wavData);
        }
        catch (...) { slot.pcm.clear(); }
        return !slot.pcm.empty();
    }

    static void Play(SoundId id)
    {
        ReadSettings();
        if (!soundEnabled || volume <= 0 || !LoadSound(static_cast<int>(id)) || !StartSoundDevice()) return;
        const auto& pcm = sounds[static_cast<int>(id)].pcm;
        // Do not accumulate effects when the game/OS audio device stalls.
        if (SDL_GetQueuedAudioSize(soundDevice) > 44100u * 2u * sizeof(float) / 2u)
            SDL_ClearQueuedAudio(soundDevice);
        std::vector<Uint8> mixed(pcm.size(), 0);
        SDL_MixAudioFormat(mixed.data(), pcm.data(), AUDIO_F32SYS,
                           static_cast<Uint32>(pcm.size()),
                           std::clamp(volume * SDL_MIX_MAXVOLUME / 100, 0, SDL_MIX_MAXVOLUME));
        SDL_QueueAudio(soundDevice, mixed.data(), static_cast<Uint32>(mixed.size()));
    }

    static void Centered(ImDrawList* draw, const char* label, const ImVec2& center,
                         float fontSize, ImU32 color)
    {
        ImFont* font = ImGui::GetFont();
        const ImVec2 measured = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        const ImVec2 p(center.x - measured.x * .5f, center.y - measured.y * .5f);
        draw->AddText(font, fontSize, ImVec2(p.x+1.5f,p.y+2.0f), IM_COL32(0,6,24,240), label);
        draw->AddText(font, fontSize, p, color, label);
    }

    static void DrawSymbol(ImDrawList* draw, APRingRoulette::Prize prize,
                           ImVec2 min, ImVec2 max)
    {
        if (auto* tex = Icon(prize))
        {
            draw->AddImage(tex,min,max);
            return;
        }
        // Safe fallback for custom artwork not installed yet.
        const int id = static_cast<int>(prize);
        const char* txt = (id == 4) ? "HOTDOG" : (id == 6) ? "-1:00" : "?";
        Centered(draw,txt,ImVec2((min.x+max.x)*.5f,(min.y+max.y)*.5f),
                 (max.x-min.x)*.17f,IM_COL32(255,227,129,255));
    }
}

namespace APRouletteUI
{
    static SDL_GameController* pad = nullptr;
    static bool lastButton = false;
    static bool lastKeyboard = false;
    static double spinBegin = 0.0;
    static double resultUntil = 0.0;
    static int lastStoredProgress = -1;
    static double lastSaveAt = 0.0;
    static double lastTickAt = 0.0;
    static unsigned reelStopMask = 0;
    static APRingRoulette::Prize visualPrize = APRingRoulette::Prize::None;
    static const char* kButtonLabels[15] = {
        "A / Cross", "B / Circle", "X / Square", "Y / Triangle",
        "Back / Share", "Guide", "Start / Options", "L3", "R3",
        "Left Shoulder (LB)", "Right Shoulder (RB)",
        "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"
    };

    static APRingRoulette::Prize Roll()
    {
        // Original V1 odds, unchanged. 777 = 1 in 1000 spins (0.1%).
        static std::mt19937 rng(std::random_device{}());
        const int slot = std::uniform_int_distribution<int>(0, 999)(rng);
        if (slot < 250) return APRingRoulette::Prize::Ring1;       // 25%
        if (slot < 450) return APRingRoulette::Prize::Life1;       // 20%
        if (slot < 650) return APRingRoulette::Prize::Ring100;     // 20%
        if (slot < 800) return APRingRoulette::Prize::Hotdog;      // 15%
        if (slot < 900) return APRingRoulette::Prize::Life3;       // 10%
        if (slot < 969) return APRingRoulette::Prize::Minute60;    // 6.9%
        if (slot < 999) return APRingRoulette::Prize::Score50000;  // 3%
        return APRingRoulette::Prize::Jackpot777;                  // 0.1%
    }

    static void PollInput()
    {
        APRingRoulette::Load();
        APRouletteSkin::ReadSettings();
        // A interrupted/reinitialized UI must not leave spinning=true forever.
        // No reward was chosen in a recoverable frame, so retain all 300 Rings.
        if (APRingRoulette::spinning.load(std::memory_order_acquire) && spinBegin <= 0.0)
        {
            APRingRoulette::spinning.store(false, std::memory_order_release);
            APRingRoulette::Save();
            LOGFN("[RING ROULETTE] Recovered orphaned spin; 300-ring charge preserved");
        }
        if (pad && !SDL_GameControllerGetAttached(pad)) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
            lastButton = false;
        }
        if (!pad) {
            for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                if (SDL_IsGameController(i)) {
                    pad = SDL_GameControllerOpen(i);
                    if (pad) break;
                }
            }
        }
        const int chosen = APRingRoulette::button.load();
        const bool buttonPressed = pad && chosen >= 0 && chosen < 15 &&
            SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(chosen));
        const bool keyboardPressed = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F9] != 0;
        const bool freshPress = (buttonPressed && !lastButton) ||
                                (keyboardPressed && !lastKeyboard);
        lastButton = buttonPressed;
        lastKeyboard = keyboardPressed;
        if (freshPress && !::g_apConnectorVisible && APRingRoulette::StartSpin())
        {
            // Pick the exact same weighted reward at the beginning so reels can
            // visually settle one after another. The game still awards it only
            // AFTER the existing 2.6-second spin completes.
            visualPrize = Roll();
            spinBegin = ImGui::GetTime();
            lastTickAt = spinBegin;
            reelStopMask = 0;
            APRouletteSkin::Play(APRouletteSkin::Start);
        }

        const double now = ImGui::GetTime();
        if (APRingRoulette::spinning.load() && spinBegin > 0.0)
        {
            const double elapsed = now - spinBegin;
            if (elapsed < 1.8 && now - lastTickAt >= .115)
            {
                lastTickAt = now;
                APRouletteSkin::Play(APRouletteSkin::Tick);
            }
            const double stops[3] = {1.80, 2.16, 2.55};
            for (unsigned i = 0; i < 3; ++i)
                if (elapsed >= stops[i] && !(reelStopMask & (1u << i)))
                {
                    reelStopMask |= (1u << i);
                    APRouletteSkin::Play(APRouletteSkin::Stop);
                }
            if (elapsed >= 2.6)
            {
                const auto prize = visualPrize;
                if (prize == APRingRoulette::Prize::Minute60)
                {
                    // Original V1 safeguard: time reward is not hooked yet;
                    // keep 300 collected Rings, and show unavailable message.
                    APRingRoulette::spinning.store(false);
                    APRingRoulette::lastPrize.store(static_cast<int>(prize));
                }
                else APRingRoulette::SetPending(prize);
                resultUntil = now + 5.0;
                spinBegin = 0.0;
                APRingRoulette::Save();
                APRouletteSkin::Play(prize == APRingRoulette::Prize::Jackpot777
                    ? APRouletteSkin::Jackpot :
                    prize == APRingRoulette::Prize::Minute60
                    ? APRouletteSkin::Stop : APRouletteSkin::Win);
            }
        }
        // V1 save behavior remains intact.
        const int p = APRingRoulette::progress.load();
        if (p != lastStoredProgress && now - lastSaveAt >= 3.0) {
            lastStoredProgress = p;
            lastSaveAt = now;
            APRingRoulette::Save();
        }
    }

    // AP_ROULETTE_COUNTER_ARCADE_V5: self-contained vector arcade digits.
    // No font file or texture folder is needed; each glyph scales with the HUD.
    static void DrawArcadeCounter(ImDrawList* draw, const ImVec2& sheetMin,
                                  float scale, int ringCount)
    {
        // Segment layout: top, upper-right, lower-right, bottom,
        // lower-left, upper-left, center. Traces remain dim when unlit.
        static constexpr unsigned char kSegments[10] = {
            0x3f, 0x06, 0x5b, 0x4f, 0x66,
            0x6d, 0x7d, 0x07, 0x7f, 0x6f
        };
        const std::string digits = std::to_string(std::clamp(ringCount, 0, 300)) + "/300";
        constexpr float digitWidth = 39.f, digitHeight = 55.f;
        constexpr float slashWidth = 26.f, gap = 12.f;
        const float textWidth = (static_cast<float>(digits.size()) - 1.f) * gap +
            digitWidth * static_cast<float>(digits.size() - 1) + slashWidth;
        float x = 296.f - textWidth * .5f;
        constexpr float y = 238.f;
        const ImVec2 origin = sheetMin;
        const auto pos = [&](float logicalX, float logicalY) {
            return ImVec2(origin.x + logicalX * scale,
                          origin.y + logicalY * scale);
        };
        const float tick = static_cast<float>(ImGui::GetTime());
        const float shimmer = .80f + .20f * std::sin(tick * 2.8f);
        const ImU32 unlit = IM_COL32(23, 54, 91, 110);
        const ImU32 glow = IM_COL32(51, 177, 252, static_cast<int>(38.f * shimmer));
        const ImU32 blue = IM_COL32(126, 218, 255, 255);
        const ImU32 white = IM_COL32(231, 249, 255, 255);
        const ImU32 gold = IM_COL32(249, 202, 102, 255);
        const ImU32 goldGlow = IM_COL32(246, 159, 45, 34);

        for (size_t c = 0; c < digits.size(); ++c)
        {
            const char ch = digits[c];
            if (ch == '/')
            {
                // The slash is drawn rather than borrowed from the default UI font.
                const ImVec2 a = pos(x + 3.f, y + digitHeight - 2.f);
                const ImVec2 b = pos(x + slashWidth - 3.f, y + 2.f);
                draw->AddLine(a, b, IM_COL32(50, 131, 221, 47), 13.f * scale);
                draw->AddLine(a, b, IM_COL32(154, 200, 246, 245), 4.2f * scale);
                x += slashWidth + gap;
                continue;
            }
            if (ch < '0' || ch > '9') continue;
            const bool isCurrent = c < digits.find('/');
            const ImU32 lit = isCurrent ? blue : gold;
            const ImU32 halo = isCurrent ? glow : goldGlow;
            const unsigned char bits = kSegments[ch - '0'];
            // Endpoints of the seven segments, normalized to a 39 x 55 cell.
            const float segment[7][4] = {
                { 7.f,  5.f, 32.f,  5.f},
                {34.f,  9.f, 34.f, 25.f},
                {34.f, 30.f, 34.f, 46.f},
                { 7.f, 50.f, 32.f, 50.f},
                { 5.f, 30.f,  5.f, 46.f},
                { 5.f,  9.f,  5.f, 25.f},
                { 7.f, 27.5f,32.f, 27.5f}
            };
            for (int part = 0; part < 7; ++part)
            {
                const ImVec2 a = pos(x + segment[part][0], y + segment[part][1]);
                const ImVec2 b = pos(x + segment[part][2], y + segment[part][3]);
                const bool active = (bits & (1u << part)) != 0;
                if (active)
                {
                    draw->AddLine(a, b, halo, 15.f * scale);
                    draw->AddLine(a, b, lit, 7.0f * scale);
                    draw->AddLine(a, b, isCurrent ? white : IM_COL32(255, 235, 170, 245),
                                  2.0f * scale);
                }
                else draw->AddLine(a, b, unlit, 5.f * scale);
            }
            x += digitWidth + gap;
        }
    }

    // AP_RING_ROULETTE_V3_TOP_RIGHT_BEGIN
    // Cabinet is always the compact top-right HUD; no extra center-screen popup.
    // Gameplay and weighted prize selection are unchanged from V2.
    static void DrawOverlay()
    {
        if (!APRingRoulette::enabled.load()) return;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport) return;
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (!draw) return;

        // Responsive 642 x 425 Blender PNG, anchored to upper-right with margin.
        // On small resolutions, hide while F8 is open to keep the settings usable.
        if (::g_apConnectorVisible && viewport->Size.x < 1120.f) return;
        const float baselineScale = std::clamp((std::min)(viewport->Size.x / 2500.f,
                                                         viewport->Size.y / 1450.f), .39f, .88f);
        // Respect user size, while keeping the entire cabinet inside the viewport.
        const float desiredScale = baselineScale * (APRouletteSkin::hudSizePercent / 100.f);
        const float fitScale = (std::min)(viewport->Size.x / 686.f, viewport->Size.y / 485.f);
        const float scale = (std::min)(desiredScale, fitScale);
        const float marginX = (std::min)(22.f * scale, viewport->Size.x * .02f);
        const float marginY = (std::min)(25.f * scale, viewport->Size.y * .02f);
        const ImVec2 sheetSize(642.f*scale, 425.f*scale);
        const float availableX = (std::max)(0.f, viewport->Size.x - sheetSize.x - 2.f*marginX);
        const float availableY = (std::max)(0.f, viewport->Size.y - sheetSize.y - 2.f*marginY);
        const ImVec2 sheetMin(viewport->Pos.x + marginX +
                                  availableX * APRouletteSkin::hudXPercent / 100.f,
                              viewport->Pos.y + marginY +
                                  availableY * APRouletteSkin::hudYPercent / 100.f);
        const ImVec2 sheetMax(sheetMin.x+sheetSize.x, sheetMin.y+sheetSize.y);
        const auto pt = [&](float x, float y) -> ImVec2 {
            return ImVec2(sheetMin.x+x*scale, sheetMin.y+y*scale);
        };
        const auto rect = [&](float x1,float y1,float x2,float y2,ImU32 color,float r=0.f) {
            draw->AddRectFilled(pt(x1,y1),pt(x2,y2),color,r*scale);
        };
        const auto label = [&](const char* text,float x,float y,float size,ImU32 color) {
            APRouletteSkin::Centered(draw,text,pt(x,y),size*scale,color);
        };
        const double now = ImGui::GetTime();
        const bool spinning = APRingRoulette::spinning.load();
        const bool displayingResult = !spinning && now < resultUntil &&
                                     APRingRoulette::lastPrize.load() != 0;
        const bool charged = APRingRoulette::progress.load() >= APRingRoulette::kRequiredRings;
        const bool ready = APRingRoulette::Ready();
        const bool pendingReward = APRingRoulette::pending.load() != 0;
        const auto reward = spinning ? visualPrize :
            static_cast<APRingRoulette::Prize>(APRingRoulette::lastPrize.load());
        const bool jackpot = displayingResult && reward == APRingRoulette::Prize::Jackpot777;

        // Custom PNG is always the HUD (not just shown during a spin).
        if (!APRouletteSkin::HasFrame())
        {
            // Graceful fallback if embedded image decoding fails.
            rect(24,132,600,415,IM_COL32(6,16,42,233),16.f);
            draw->AddRect(pt(24,132),pt(600,415),IM_COL32(92,172,255,255),12.f*scale,0,2.f*scale);
            label("SONIC SLOT MACHINE",311,182,32.f,IM_COL32(255,181,99,255));
        }
        else
        {
            // Shadow under the cabinet only; the Blender artwork stays intact.
            draw->AddRectFilled(pt(7,153),pt(592,422),IM_COL32(0,4,22,65),16.f*scale);
            draw->AddImage(APRouletteSkin::artwork.get(),sheetMin,sheetMax);
        }

        // All custom details are strictly inside the dark cabinet screen.
        // This screen is approx x=27..566, y=147..409 on the supplied PNG.
        const float centerX = 296.f;
        const float pulse = .5f + .5f*std::sin(static_cast<float>(now*4.4));
        if (!spinning && !displayingResult && !charged)
        {
            const int progress = std::clamp(APRingRoulette::progress.load(),0,300);
            const float fill = progress / 300.f;
            // AP_ROULETTE_COUNTER_ARCADE_V5: cyan/gold vector display,
            // subtle illuminated bezel and a slimmer ring-charge bar.
            label("RING CHARGE",centerX,206,27.f,IM_COL32(255,213,141,255));
            rect(73,224,522,303,IM_COL32(0,9,30,216),12.f);
            draw->AddRect(pt(73,224),pt(522,303),
                IM_COL32(74,154,232,170),12.f*scale,0,1.8f*scale);
            draw->AddLine(pt(94,227),pt(500,227),
                IM_COL32(165,220,255,65),1.8f*scale);
            DrawArcadeCounter(draw,sheetMin,scale,progress);
            label("RINGS",centerX,317,15.f,IM_COL32(165,211,251,245));
            // Chrome / cobalt charge bar (same progress as before).
            rect(78,335,514,363,IM_COL32(0,6,20,236),10.f);
            draw->AddRect(pt(78,335),pt(514,363),
                IM_COL32(93,177,255,225),10.f*scale,0,2.1f*scale);
            rect(85,341,507,357,IM_COL32(19,44,81,255),6.f);
            if (fill > 0.f)
            {
                const float endX = 85.f + 422.f * fill;
                rect(85,341,endX,357,IM_COL32(31,166,246,255),6.f);
                if (fill >= .06f)
                    draw->AddLine(pt(91,344),pt(endX-4.f,344),
                        IM_COL32(196,242,255,188),2.5f*scale);
                draw->AddCircleFilled(pt(endX,349),5.5f*scale,
                    IM_COL32(211,250,255,245));
            }
            label("COLLECT RINGS TO UNLOCK",centerX,388,18.f,
                  IM_COL32(171,211,246,245));
            return;
        }

        if (!spinning && !displayingResult && pendingReward)
        {
            label("REWARD PENDING",centerX,264,34.f,IM_COL32(255,213,119,255));
            // Rewards are applied by safe game-thread hooks, not the ImGui renderer.
            // Explain precisely why another spin cannot start yet.
            const auto queued = static_cast<APRingRoulette::Prize>(APRingRoulette::pending.load());
            const char* instruction = queued == APRingRoulette::Prize::Hotdog
                ? "HOTDOG DELIVERY PENDING"
                : (queued == APRingRoulette::Prize::Life1 ||
                   queued == APRingRoulette::Prize::Life3 ||
                   queued == APRingRoulette::Prize::Jackpot777)
                ? "WAITING FOR LIFE UPDATE" : "WAITING FOR GAME UPDATE";
            label(instruction,centerX,307,16.f,IM_COL32(179,221,255,255));
            const std::string ringCharge = "NEXT CHARGE: " +
                std::to_string(std::clamp(APRingRoulette::progress.load(), 0, 300)) + "/300";
            label(ringCharge.c_str(),centerX,358,18.f,IM_COL32(145,230,255,255));
            return;
        }

        // READY, SPIN and RESULT share the same three aligned arcade reels.
        // At READY these are a teaser set, not a preselected jackpot outcome.
        const double elapsed=spinning ? (std::max)(0.0,now-spinBegin) : 3.0;
        for (int i=0; i<3; ++i)
        {
            const float x=58.f+164.f*i, y=199.f;
            const ImVec2 cellMin=pt(x,y), cellMax=pt(x+143.f,y+137.f);
            draw->AddRectFilled(cellMin,cellMax,IM_COL32(3,12,33,253),12.f*scale);
            const ImU32 border = jackpot ? IM_COL32(255,218,107,255) :
                ready ? IM_COL32(111,200,255,255) : IM_COL32(60,138,218,242);
            draw->AddRect(cellMin,cellMax,border,12.f*scale,0,(ready?3.0f:2.0f)*scale);
            rect(x+8,y+8,x+135,y+14,IM_COL32(202,234,255,85),3.f);
            draw->PushClipRect(pt(x+7,y+7),pt(x+136,y+130),true);
            const bool stopped = !spinning || elapsed >= (i==0 ? 1.80 : i==1 ? 2.16 : 2.55);
            if (stopped)
            {
                const APRingRoulette::Prize symbol = (!spinning && !displayingResult) ?
                    static_cast<APRingRoulette::Prize>(i==0?1:i==1?3:2) : reward;
                const float glow = jackpot ? 1.f+.05f*std::sin(static_cast<float>(now*11.f)) : 1.f;
                const float side=108.f*scale*glow;
                const ImVec2 middle=pt(x+71.5f,y+68.f);
                APRouletteSkin::DrawSymbol(draw,symbol,
                    ImVec2(middle.x-side*.5f,middle.y-side*.5f),
                    ImVec2(middle.x+side*.5f,middle.y+side*.5f));
            }
            else
            {
                const double frame = elapsed*(16.0-2.0*i) + i*2.37;
                const int index=static_cast<int>(std::floor(frame));
                const float fraction=static_cast<float>(frame-std::floor(frame));
                for (int k=-1;k<=1;++k)
                {
                    const int symbol=((index+k)%APRouletteSkin::kSymbolCount+
                        APRouletteSkin::kSymbolCount)%APRouletteSkin::kSymbolCount;
                    const ImVec2 middle=pt(x+71.5f,y+68.f+(k-fraction)*119.f);
                    APRouletteSkin::DrawSymbol(draw,
                        static_cast<APRingRoulette::Prize>(symbol+1),
                        ImVec2(middle.x-54.f*scale,middle.y-54.f*scale),
                        ImVec2(middle.x+54.f*scale,middle.y+54.f*scale));
                }
            }
            draw->PopClipRect();
        }

        if (!spinning && !displayingResult)
        {
            // Bold golden READY with a soft orange halo; no extra external font needed.
            const ImU32 halo=IM_COL32(238,126,22,static_cast<int>(65+65*pulse));
            if (!APRouletteSkin::readyArt)
            {
                const ImVec2 p=pt(centerX,369);
                for (int n=0;n<8;++n)
                {
                    const float a=n*6.2831853f/8.f;
                    APRouletteSkin::Centered(draw,"READY",
                        ImVec2(p.x+3.f*scale*std::cos(a),p.y+3.f*scale*std::sin(a)),
                        45.f*scale,halo);
                }
            }
            label("READY",centerX,369,45.f,IM_COL32(255,224,119,255));
            const char* tip = APRingRoulette::inAct.load() ? "PRESS R3 / F9 TO SPIN" :
                "ENTER A STAGE TO SPIN";
            label(tip,centerX,402,17.f,IM_COL32(194,232,255,255));
            return;
        }

        if (spinning)
        {
            label("SPINNING...",centerX,379,30.f,IM_COL32(143,220,255,255));
        }
        else if (reward == APRingRoulette::Prize::Minute60)
        {
            label("TIME BONUS UNAVAILABLE",centerX,376,21.f,IM_COL32(255,194,116,255));
            label("300 RINGS REFUNDED",centerX,402,17.f,IM_COL32(213,237,255,255));
        }
        else
        {
            label(APRingRoulette::PrizeName(reward),centerX,379,
                  jackpot?23.f:24.f,jackpot?IM_COL32(255,226,94,255):IM_COL32(164,231,255,255));
        }
        if (jackpot)
        {
            for (int i=0;i<15;++i)
            {
                const float a=i*6.2831853f/15.f+static_cast<float>(now*.55);
                const float r=158.f+11.f*std::sin(static_cast<float>(now*4.f+i));
                draw->AddCircleFilled(pt(centerX+std::cos(a)*r,
                                         265.f+std::sin(a)*r*.55f),
                    (2.5f+1.1f*std::sin(static_cast<float>(now*7.f+i)))*scale,
                    IM_COL32(255,215,90,215));
            }
        }
    }
    // AP_RING_ROULETTE_V3_TOP_RIGHT_END

    static void DrawSettings()
    {
        APRingRoulette::Load();
        APRouletteSkin::ReadSettings();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(.98f,.69f,.42f,1.f),"SONIC SLOT MACHINE  |  RING ROULETTE");
        bool e = APRingRoulette::enabled.load();
        if (ImGui::Checkbox("Enable Ring Roulette##APRouletteEnable",&e)) {
            APRingRoulette::enabled.store(e);
            APRingRoulette::Save();
        }
        ImGui::Text("Collected Rings: %d / 300",APRingRoulette::progress.load());
        ImGui::ProgressBar(APRingRoulette::progress.load()/300.f,ImVec2(-1,10),"");
        int b=APRingRoulette::button.load();
        if (ImGui::Combo("Controller button##APRouletteButton",&b,kButtonLabels,15)) {
            APRingRoulette::button.store(b);
            lastButton=true;
            APRingRoulette::Save();
        }
        if (ImGui::Checkbox("Slot machine sound effects##APRouletteSFX",&APRouletteSkin::soundEnabled))
        {
            if (!APRouletteSkin::soundEnabled && APRouletteSkin::soundDevice)
                SDL_ClearQueuedAudio(APRouletteSkin::soundDevice);
            APRouletteSkin::SaveSettings();
        }
        if (ImGui::SliderInt("SFX volume##APRouletteVolume",&APRouletteSkin::volume,0,100,"%d%%"))
            APRouletteSkin::SaveSettings();
        ImGui::Separator();
        ImGui::Text("Slot machine HUD size and position");
        if (ImGui::SliderInt("HUD size##APRouletteHudSize", &APRouletteSkin::hudSizePercent, 50, 175, "%d%%"))
            APRouletteSkin::SaveSettings();
        if (ImGui::SliderInt("Horizontal position##APRouletteHudX", &APRouletteSkin::hudXPercent, 0, 100, "%d%%"))
            APRouletteSkin::SaveSettings();
        if (ImGui::SliderInt("Vertical position##APRouletteHudY", &APRouletteSkin::hudYPercent, 0, 100, "%d%%"))
            APRouletteSkin::SaveSettings();
        if (ImGui::Button("Reset HUD placement##APRouletteHudDefault"))
        {
            APRouletteSkin::hudSizePercent = 100;
            APRouletteSkin::hudXPercent = 100;
            APRouletteSkin::hudYPercent = 0;
            APRouletteSkin::SaveSettings();
        }
        ImGui::TextDisabled("Settings are saved automatically.");
        ImGui::TextDisabled("300 Rings required to start");
        ImGui::TextDisabled("777 Jackpot: 0.1%% chance");
        // Recovery controls for interrupted sessions. No guest-memory writes.
        // Do not silently discard pending rewards.
        if (!APRingRoulette::spinning.load() && APRingRoulette::pending.load() == 0)
        {
            if (ImGui::Button("Reset roulette charge##APRouletteReset"))
                ImGui::OpenPopup("Confirm roulette reset##APRoulette");
            if (ImGui::BeginPopupModal("Confirm roulette reset##APRoulette", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Reset collected Rings to 0 / 300? This cannot be undone.");
                if (ImGui::Button("Confirm reset##APRoulette"))
                {
                    APRingRoulette::progress.store(0, std::memory_order_release);
                    APRingRoulette::lastPrize.store(0, std::memory_order_release);
                    APRingRoulette::Save();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##APRoulette")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        if (APRingRoulette::progress.load() >= 300 && !APRingRoulette::inAct.load())
            ImGui::TextDisabled("Charge saved. Enter a playable Act to spin.");
        const auto queuedPrize = static_cast<APRingRoulette::Prize>(
            APRingRoulette::pending.load(std::memory_order_acquire));
        if (queuedPrize != APRingRoulette::Prize::None)
        {
            ImGui::TextColored(ImVec4(.98f,.69f,.42f,1.f),
                "Unclaimed prize: %s", APRingRoulette::PrizeName(queuedPrize));
            if (queuedPrize == APRingRoulette::Prize::Hotdog)
                ImGui::TextWrapped("Hotdog delivery is attempted automatically in an Act when a verified game manager is available. The inventory menu remains a safe fallback.");
            else if (queuedPrize == APRingRoulette::Prize::Life1 ||
                     queuedPrize == APRingRoulette::Prize::Life3 ||
                     queuedPrize == APRingRoulette::Prize::Jackpot777)
                ImGui::TextWrapped("Waiting for the verified life-manager hook. The prize will stay queued, not discarded.");
            else
                ImGui::TextWrapped("The reward is queued until its game-thread hook applies it. No further spin can start until then.");
        }
    }
}
// AP_RING_ROULETTE_V2_SKIN_END
#include "ap_multiplayer_overlay.h" // AP_MULTIWORLD_HUD_V082

static void CreateImGuiBackend()
{
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    IM_DELETE(io.Fonts);
    io.Fonts = ImFontAtlasSnapshot::Load();
#else
    io.Fonts->AddFontDefault();
    ImFontAtlasSnapshot::GenerateGlyphRanges();

#endif

    InitImGuiUtils();
    AchievementMenu::Init();
    AchievementOverlay::Init();
    ButtonGuide::Init();
    MessageWindow::Init();
    OptionsMenu::Init();
    InstallerWizard::Init();

    ImGui_ImplSDL2_InitForOther(GameWindow::s_pWindow);

#ifdef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    g_imFontTexture = LoadTexture(
        decompressZstd(g_im_font_atlas_texture, g_im_font_atlas_texture_uncompressed_size).get(), g_im_font_atlas_texture_uncompressed_size);
#else
    io.Fonts->FontBuilderIO = &g_fontBuilderIO;
    io.Fonts->Build();

    g_imFontTexture = std::make_unique<GuestTexture>(ResourceType::Texture);

    uint8_t* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    RenderTextureDesc textureDesc;
    textureDesc.dimension = RenderTextureDimension::TEXTURE_2D;
    textureDesc.width = width;
    textureDesc.height = height;
    textureDesc.depth = 1;
    textureDesc.mipLevels = 1;
    textureDesc.arraySize = 1;
    textureDesc.format = RenderFormat::R8G8B8A8_UNORM;

    g_imFontTexture->textureHolder = g_device->createTexture(textureDesc);
    g_imFontTexture->texture = g_imFontTexture->textureHolder.get();

    uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch = (rowPitch * height + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
    uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

    if (rowPitch == (width * 4))
    {
        memcpy(mappedMemory, pixels, slicePitch);
    }
    else
    {
        for (size_t i = 0; i < height; i++)
        {
            memcpy(mappedMemory, pixels, width * 4);
            pixels += width * 4;
            mappedMemory += rowPitch;
        }
    }

    uploadBuffer->unmap();

    ExecuteCopyCommandList([&]
        {
            g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(g_imFontTexture->texture, RenderTextureLayout::COPY_DEST));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(g_imFontTexture->texture, 0),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
        });

    g_imFontTexture->layout = RenderTextureLayout::COPY_DEST;

    RenderTextureViewDesc textureViewDesc;
    textureViewDesc.format = textureDesc.format;
    textureViewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    textureViewDesc.mipLevels = 1;
    g_imFontTexture->textureView = g_imFontTexture->texture->createTextureView(textureViewDesc);

    g_imFontTexture->descriptorIndex = g_textureDescriptorAllocator.allocate();
    g_textureDescriptorSet->setTexture(g_imFontTexture->descriptorIndex, g_imFontTexture->texture, RenderTextureLayout::SHADER_READ, g_imFontTexture->textureView.get());
#endif

    io.Fonts->SetTexID(g_imFontTexture.get());

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);

    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, TEXTURE_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, TEXTURE_DESCRIPTOR_SIZE);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, SAMPLER_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, SAMPLER_DESCRIPTOR_SIZE);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    pipelineLayoutBuilder.addPushConstant(0, 2, sizeof(ImGuiPushConstants), RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);

    pipelineLayoutBuilder.end();
    g_imPipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    auto vertexShader = CREATE_SHADER(imgui_vs);
    auto pixelShader = CREATE_SHADER(imgui_ps);

    RenderInputElement inputElements[3];
    inputElements[0] = RenderInputElement("POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, pos));
    inputElements[1] = RenderInputElement("TEXCOORD", 0, 1, RenderFormat::R32G32_FLOAT, 0, offsetof(ImDrawVert, uv));
    inputElements[2] = RenderInputElement("COLOR", 0, 2, RenderFormat::R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col));

    RenderInputSlot inputSlot(0, sizeof(ImDrawVert));

    RenderGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.pipelineLayout = g_imPipelineLayout.get();
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.pixelShader = pixelShader.get();
    pipelineDesc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    pipelineDesc.renderTargetBlend[0] = RenderBlendDesc::AlphaBlend();
    pipelineDesc.renderTargetCount = 1;
    pipelineDesc.inputElements = inputElements;
    pipelineDesc.inputElementsCount = std::size(inputElements);
    pipelineDesc.inputSlots = &inputSlot;
    pipelineDesc.inputSlotsCount = 1;
    g_imPipeline = g_device->createGraphicsPipeline(pipelineDesc);

    pipelineDesc.renderTargetBlend[0].dstBlend = RenderBlend::ONE;
    g_imAdditivePipeline = g_device->createGraphicsPipeline(pipelineDesc);

#ifndef ENABLE_IM_FONT_ATLAS_SNAPSHOT
    ImFontAtlasSnapshot snapshot;
    snapshot.Snap();

    FILE* file = fopen("im_font_atlas.bin", "wb");
    if (file)
    {
        fwrite(snapshot.data.data(), 1, snapshot.data.size(), file);
        fclose(file);
    }

    ddspp::Header header;
    ddspp::HeaderDXT10 headerDX10;
    ddspp::encode_header(ddspp::R8G8B8A8_UNORM, width, height, 1, ddspp::Texture2D, 1, 1, header, headerDX10);

    file = fopen("im_font_atlas.dds", "wb");
    if (file)
    {
        fwrite(&ddspp::DDS_MAGIC, 4, 1, file);
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&headerDX10, sizeof(headerDX10), 1, file);
        fwrite(pixels, 4, width * height, file);
        fclose(file);
    }
#endif
}

static void CheckSwapChain()
{
    g_swapChain->setVsyncEnabled(Config::VSync);
    g_swapChainValid &= !g_swapChain->needsResize();

    if (!g_swapChainValid)
    {
        Video::WaitForGPU();
        g_backBuffer->framebuffers.clear();
        g_swapChainValid = g_swapChain->resize();
        g_needsResize = g_swapChainValid;
    }

    if (g_swapChainValid)
    {
        g_swapChainAcquireProfiler.Begin();
        g_swapChainValid = g_swapChain->acquireTexture(g_acquireSemaphores[g_frame].get(), &g_backBufferIndex);
        g_swapChainAcquireProfiler.End();
    }

    if (g_needsResize)
        Video::ComputeViewportDimensions();

    g_backBuffer->width = Video::s_viewportWidth;
    g_backBuffer->height = Video::s_viewportHeight;
}

static void BeginCommandList()
{
    g_renderTarget = g_backBuffer;
    g_depthStencil = nullptr;
    g_framebuffer = nullptr;

    g_pipelineState.renderTargetFormat = BACKBUFFER_FORMAT;
    g_pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;

    if (g_swapChainValid)
    {
        uint32_t width = Video::s_viewportWidth;
        uint32_t height = Video::s_viewportHeight;

        bool usingIntermediaryTexture = (width != g_swapChain->getWidth()) || (height != g_swapChain->getHeight()) ||
            Config::XboxColorCorrection || (abs(Config::Brightness - 0.5f) > 0.001f);

        if (usingIntermediaryTexture)
        {
            if (g_intermediaryBackBufferTextureWidth != width ||
                g_intermediaryBackBufferTextureHeight != height)
            {
                if (g_intermediaryBackBufferTextureDescriptorIndex == NULL)
                    g_intermediaryBackBufferTextureDescriptorIndex = g_textureDescriptorAllocator.allocate();

                Video::WaitForGPU(); // Fine to wait for GPU, this'll only happen during resize.

                g_intermediaryBackBufferTexture = g_device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET));
                g_textureDescriptorSet->setTexture(g_intermediaryBackBufferTextureDescriptorIndex, g_intermediaryBackBufferTexture.get(), RenderTextureLayout::SHADER_READ);

                g_intermediaryBackBufferTextureWidth = width;
                g_intermediaryBackBufferTextureHeight = height;

                g_backBuffer->framebuffers.clear();
            }

            g_backBuffer->texture = g_intermediaryBackBufferTexture.get();
        }
        else
        {
            g_backBuffer->texture = g_swapChain->getTexture(g_backBufferIndex);
        }
    }
    else
    {
        g_backBuffer->texture = g_backBuffer->textureHolder.get();
    }

    g_backBuffer->layout = RenderTextureLayout::UNKNOWN;

    for (size_t i = 0; i < 16; i++)
    {
        g_sharedConstants.texture2DIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
        g_sharedConstants.texture3DIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D;
        g_sharedConstants.textureCubeIndices[i] = TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE;
    }

    memset(g_textures, 0, sizeof(g_textures));

    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
        g_pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;
    else
        g_pipelineState.specConstants &= ~SPEC_CONSTANT_BICUBIC_GI_FILTER;

    auto& commandList = g_commandLists[g_frame];

    commandList->begin();
    commandList->resetQueryPool(g_queryPools[g_frame].get(), 0, NUM_QUERIES);
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 0);
    commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 1);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 2);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 3);

    g_readyForCommands = true;
    g_readyForCommands.notify_one();
}

template<typename T>
static void ApplyLowEndDefault(ConfigDef<T> &configDef, T newDefault, bool &changed)
{
    if (configDef.IsDefaultValue() && !configDef.IsLoadedFromConfig)
    {
        configDef = newDefault;
        changed = true;
    }
    
    configDef.DefaultValue = newDefault;
}

static void ApplyLowEndDefaults()
{
    bool changed = false;

    ApplyLowEndDefault(Config::AntiAliasing, EAntiAliasing::MSAA2x, changed);
    ApplyLowEndDefault(Config::ShadowResolution, EShadowResolution::Original, changed);
    ApplyLowEndDefault(Config::TransparencyAntiAliasing, false, changed);
    ApplyLowEndDefault(Config::GITextureFiltering, EGITextureFiltering::Bilinear, changed);

    if (changed) 
    {
        Config::Save();
    }
}

bool Video::CreateHostDevice(const char *sdlVideoDriver, bool graphicsApiRetry)
{
    for (uint32_t i = 0; i < 16; i++)
        g_inputSlots[i].index = i;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    GameWindow::Init(sdlVideoDriver);

#ifdef UNLEASHED_RECOMP_D3D12
    g_vulkan = DetectWine() || Config::GraphicsAPI == EGraphicsAPI::Vulkan;
#endif

    // Attempt to create the possible backends using a vector of function pointers. Whichever succeeds first will be the chosen API.
    using RenderInterfaceFunction = std::unique_ptr<RenderInterface>(void);
    std::vector<RenderInterfaceFunction *> interfaceFunctions;

#ifdef UNLEASHED_RECOMP_D3D12
    bool allowVulkanRedirection = true;

    if (graphicsApiRetry)
    {
        // If we are attempting to create again after a reboot due to a crash, swap the order.
        g_vulkan = !g_vulkan;

        // Don't allow redirection to Vulkan if we are retrying after a crash, 
        // so the user can at least boot the game with D3D12 if Vulkan fails to work.
        allowVulkanRedirection = false;
    }

    interfaceFunctions.push_back(g_vulkan ? CreateVulkanInterfaceWrapper : CreateD3D12Interface);
    interfaceFunctions.push_back(g_vulkan ? CreateD3D12Interface : CreateVulkanInterfaceWrapper);
#else
    interfaceFunctions.push_back(CreateVulkanInterfaceWrapper);
#endif

    for (size_t i = 0; i < interfaceFunctions.size(); i++)
    {
        RenderInterfaceFunction* interfaceFunction = interfaceFunctions[i];

#ifdef UNLEASHED_RECOMP_D3D12
        // Wrap the device creation in __try/__except to survive from driver crashes.
        __try
#endif
        {
            g_interface = interfaceFunction();
            if (g_interface == nullptr)
            {
                continue;
            }

            g_device = g_interface->createDevice(Config::GraphicsDevice);
            if (g_device != nullptr)
            {
                const RenderDeviceDescription &deviceDescription = g_device->getDescription();
                
#ifdef UNLEASHED_RECOMP_D3D12
                if (interfaceFunction == CreateD3D12Interface)
                {
                    if (allowVulkanRedirection)
                    {
                        bool redirectToVulkan = false;

                        if (deviceDescription.vendor == RenderDeviceVendor::AMD)
                        {
                            // AMD Drivers before this version have a known issue where MSAA resolve targets will fail to work correctly.
                            // If no specific graphics API was selected, we silently destroy this one and move to the next option as it'll
                            // just work incorrectly otherwise and result in visual glitches and 3D rendering not working in general.
                            constexpr uint64_t MinimumAMDDriverVersion = 0x1F00005DC2005CULL; // 31.0.24002.92
                            if ((Config::GraphicsAPI == EGraphicsAPI::Auto) && (deviceDescription.driverVersion < MinimumAMDDriverVersion))
                                redirectToVulkan = true;
                        }
                        else if (deviceDescription.vendor == RenderDeviceVendor::INTEL)
                        {
                            // Intel drivers on D3D12 are extremely buggy, introducing various graphical glitches.
                            // We will redirect users to Vulkan until a workaround can be found.
                            if (Config::GraphicsAPI == EGraphicsAPI::Auto)
                                redirectToVulkan = true;
                        }

                        if (redirectToVulkan)
                        {
                            g_device.reset();
                            g_interface.reset();

                            // In case Vulkan fails to initialize, we will try D3D12 again afterwards, 
                            // just to get the game to boot. This only really happens in very old Intel GPU drivers.
                            if (!g_vulkan)
                            {
                                interfaceFunctions.push_back(CreateD3D12Interface);
                                allowVulkanRedirection = false;
                            }

                            continue;
                        }
                    }

                    // Hardware resolve seems to be completely bugged on Intel D3D12 drivers.
                    g_hardwareResolve = (deviceDescription.vendor != RenderDeviceVendor::INTEL);
                    g_hardwareDepthResolve = (deviceDescription.vendor != RenderDeviceVendor::INTEL);
                }

                g_vulkan = (interfaceFunction == CreateVulkanInterfaceWrapper);
#endif
                // Enable triangle strip workaround if we are on AMD, as there is a bug where
                // restart indices cause triangles to be culled incorrectly. Converting them to degenerate triangles fixes it.
                g_triangleStripWorkaround = (deviceDescription.vendor == RenderDeviceVendor::AMD);

                break;
            }
        }
#ifdef UNLEASHED_RECOMP_D3D12
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (graphicsApiRetry)
            {
                // If we were retrying, and this also failed, then we'll show the user neither of the graphics APIs succeeded.
                return false;
            }
            else
            {
                // If this is the first crash we ran into, reboot and try the other graphics API.
                os::process::StartProcess(os::process::GetExecutablePath(), { "--graphics-api-retry" });
                std::_Exit(0);
            }
        }
#endif
    }

    if (g_device == nullptr)
    {
        return false;
    }

#ifdef UNLEASHED_RECOMP_D3D12
    if (graphicsApiRetry)
    {
        // If we managed to create a device after retrying it in a reboot, remember the one we picked.
        Config::GraphicsAPI = g_vulkan ? EGraphicsAPI::Vulkan : EGraphicsAPI::D3D12;
    }
#endif

    g_capabilities = g_device->getCapabilities();

    LoadEmbeddedResources();

    constexpr uint64_t LowEndMemoryLimit = 2048ULL * 1024ULL * 1024ULL;
    RenderDeviceDescription deviceDescription = g_device->getDescription();
    bool lowEndType = deviceDescription.type != RenderDeviceType::UNKNOWN && deviceDescription.type != RenderDeviceType::DISCRETE;
    bool lowEndMemory = deviceDescription.dedicatedVideoMemory < LowEndMemoryLimit;
    bool lowEndUMA = deviceDescription.type == RenderDeviceType::UNKNOWN && g_capabilities.uma;
    if (lowEndType || lowEndMemory || lowEndUMA)
    {
        // Switch to low end defaults if a non-discrete GPU was detected or a low amount of VRAM was detected.
        // Checking for UMA on D3D12 seems to be a reliable way to detect integrated GPUs.
        ApplyLowEndDefaults();
    }

    const RenderSampleCounts colourSampleCount = g_device->getSampleCountsSupported(RenderFormat::R16G16B16A16_FLOAT);
    const RenderSampleCounts depthSampleCount  = g_device->getSampleCountsSupported(RenderFormat::D32_FLOAT);
    const RenderSampleCounts commonSampleCount = colourSampleCount & depthSampleCount;

    // Disable specific MSAA levels if they are not supported.
    if ((commonSampleCount & RenderSampleCount::COUNT_2) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA2x);
    if ((commonSampleCount & RenderSampleCount::COUNT_4) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA4x);
    if ((commonSampleCount & RenderSampleCount::COUNT_8) == 0)
        Config::AntiAliasing.InaccessibleValues.emplace(EAntiAliasing::MSAA8x);

    // Set Anti-Aliasing to nearest supported level.
    Config::AntiAliasing.SnapToNearestAccessibleValue(false);

    g_queue = g_device->createCommandQueue(RenderCommandListType::DIRECT);

    for (auto& commandList : g_commandLists)
        commandList = g_queue->createCommandList();

    for (auto& commandFence : g_commandFences)
        commandFence = g_device->createCommandFence();

    for (auto& queryPool : g_queryPools)
        queryPool = g_device->createQueryPool(NUM_QUERIES);

    g_copyQueue = g_device->createCommandQueue(RenderCommandListType::COPY);
    g_copyCommandList = g_copyQueue->createCommandList();
    g_copyCommandFence = g_device->createCommandFence();

    uint32_t bufferCount = 2;

    switch (Config::TripleBuffering)
    {
    case ETripleBuffering::Auto:
        if (g_vulkan)
        {
            // Defaulting to 3 is fine if presentWait as supported, as the maximum frame latency allowed is only 1.
            bufferCount = g_device->getCapabilities().presentWait ? 3 : 2;
        }
        else
        {
            // Defaulting to 3 is fine on D3D12 thanks to flip discard model.
            bufferCount = 3;
        }

        break;
    case ETripleBuffering::On:
        bufferCount = 3;
        break;
    case ETripleBuffering::Off:
        bufferCount = 2;
        break;
    }

    g_swapChain = g_queue->createSwapChain(GameWindow::s_renderWindow, bufferCount, BACKBUFFER_FORMAT, Config::MaxFrameLatency);
    g_swapChain->setVsyncEnabled(Config::VSync);
    g_swapChainValid = !g_swapChain->needsResize();

    for (auto& acquireSemaphore : g_acquireSemaphores)
        acquireSemaphore = g_device->createCommandSemaphore();
    
    for (auto& renderSemaphore : g_renderSemaphores)
        renderSemaphore = g_device->createCommandSemaphore();

    RenderPipelineLayoutBuilder pipelineLayoutBuilder;
    pipelineLayoutBuilder.begin(false, true);
    
    RenderDescriptorSetBuilder descriptorSetBuilder;
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addTexture(0, TEXTURE_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, TEXTURE_DESCRIPTOR_SIZE);
    
    g_textureDescriptorSet = descriptorSetBuilder.create(g_device.get());
    
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
    {
        auto& texture = g_blankTextures[i];
        auto& textureView = g_blankTextureViews[i];

        RenderTextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.depth = 1;
        desc.mipLevels = 1;
        desc.format = RenderFormat::R8_UNORM;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO, RenderSwizzle::ZERO);
        viewDesc.mipLevels = 1;

        switch (i)
        {
        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D:
            desc.dimension = RenderTextureDimension::TEXTURE_3D;
            desc.arraySize = 1;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_3D;
            break;

        case TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE:
            desc.dimension = RenderTextureDimension::TEXTURE_2D;
            desc.arraySize = 6;
            desc.flags = RenderTextureFlag::CUBE;
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_CUBE;
            break;

        default:
            assert(false && "Unknown null descriptor dimension");
            break;
        }

        texture = g_device->createTexture(desc);
        textureView = texture->createTextureView(viewDesc);

        g_textureDescriptorSet->setTexture(i, texture.get(), RenderTextureLayout::SHADER_READ, textureView.get());
    }

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);
    
    descriptorSetBuilder.begin();
    descriptorSetBuilder.addSampler(0, SAMPLER_DESCRIPTOR_SIZE);
    descriptorSetBuilder.end(true, SAMPLER_DESCRIPTOR_SIZE);
    
    g_samplerDescriptorSet = descriptorSetBuilder.create(g_device.get());
    auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&g_samplerDescs[0], sizeof(RenderSamplerDesc))];
    descriptorIndex = 1;
    sampler = g_device->createSampler(g_samplerDescs[0]);
    g_samplerDescriptorSet->setSampler(0, sampler.get());

    pipelineLayoutBuilder.addDescriptorSet(descriptorSetBuilder);

    if (g_vulkan)
    {
        pipelineLayoutBuilder.addPushConstant(0, 4, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    }
    else
    {
        pipelineLayoutBuilder.addRootDescriptor(0, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(1, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addRootDescriptor(2, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
        pipelineLayoutBuilder.addPushConstant(3, 4, 4, RenderShaderStageFlag::PIXEL); // For copy/resolve shaders.
    }
    pipelineLayoutBuilder.end();
    
    g_pipelineLayout = pipelineLayoutBuilder.create(g_device.get());

    g_copyShader = CREATE_SHADER(copy_vs);
    g_copyColorShader = CREATE_SHADER(copy_color_ps);
    auto copyDepthShader = CREATE_SHADER(copy_depth_ps);

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = copyDepthShader.get();
    desc.depthFunction = RenderComparisonFunction::ALWAYS;
    desc.depthEnabled = true;
    desc.depthWriteEnabled = true;
    desc.depthTargetFormat = RenderFormat::D32_FLOAT;
    g_copyDepthPipeline = g_device->createGraphicsPipeline(desc);

    g_resolveMsaaColorShaders[0] = CREATE_SHADER(resolve_msaa_color_2x);
    g_resolveMsaaColorShaders[1] = CREATE_SHADER(resolve_msaa_color_4x);
    g_resolveMsaaColorShaders[2] = CREATE_SHADER(resolve_msaa_color_8x);

    for (size_t i = 0; i < std::size(g_resolveMsaaDepthPipelines); i++)
    {
        std::unique_ptr<RenderShader> pixelShader;
        switch (i)
        {
        case 0:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_2x);
            break;
        case 1:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_4x);
            break;
        case 2:
            pixelShader = CREATE_SHADER(resolve_msaa_depth_8x);
            break;
        }

        desc = {};
        desc.pipelineLayout = g_pipelineLayout.get();
        desc.vertexShader = g_copyShader.get();
        desc.pixelShader = pixelShader.get();
        desc.depthFunction = RenderComparisonFunction::ALWAYS;
        desc.depthEnabled = true;
        desc.depthWriteEnabled = true;
        desc.depthTargetFormat = RenderFormat::D32_FLOAT;
        g_resolveMsaaDepthPipelines[i] = g_device->createGraphicsPipeline(desc);
    }

    for (auto& shader : g_gaussianBlurShaders)
        shader = std::make_unique<GuestShader>(ResourceType::PixelShader);

    g_gaussianBlurShaders[GAUSSIAN_BLUR_3X3]->shader = CREATE_SHADER(gaussian_blur_3x3);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_5X5]->shader = CREATE_SHADER(gaussian_blur_5x5);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_7X7]->shader = CREATE_SHADER(gaussian_blur_7x7);
    g_gaussianBlurShaders[GAUSSIAN_BLUR_9X9]->shader = CREATE_SHADER(gaussian_blur_9x9);

    g_csdFilterShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_csdFilterShader->shader = CREATE_SHADER(csd_filter_ps);

    g_enhancedMotionBlurShader = std::make_unique<GuestShader>(ResourceType::PixelShader);
    g_enhancedMotionBlurShader->shader = CREATE_SHADER(enhanced_motion_blur_ps);

    CreateImGuiBackend();

    auto gammaCorrectionShader = CREATE_SHADER(gamma_correction_ps);

    desc = {};
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = g_copyShader.get();
    desc.pixelShader = gammaCorrectionShader.get();
    desc.renderTargetFormat[0] = BACKBUFFER_FORMAT;
    desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
    desc.renderTargetCount = 1;
    g_gammaCorrectionPipeline = g_device->createGraphicsPipeline(desc);

    // NOTE: We initially allocate this on host memory to make the installer work, even if the 4 GB memory allocation fails.
    g_backBufferHolder = std::make_unique<GuestSurface>(ResourceType::RenderTarget);

    g_backBuffer = g_backBufferHolder.get();
    g_backBuffer->width = 1280;
    g_backBuffer->height = 720;
    g_backBuffer->format = BACKBUFFER_FORMAT;
    g_backBuffer->textureHolder = g_device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, BACKBUFFER_FORMAT, RenderTextureFlag::RENDER_TARGET));

    Video::ComputeViewportDimensions();
    CheckSwapChain();
    BeginCommandList();

    RenderTextureBarrier blankTextureBarriers[TEXTURE_DESCRIPTOR_NULL_COUNT];
    for (size_t i = 0; i < TEXTURE_DESCRIPTOR_NULL_COUNT; i++)
        blankTextureBarriers[i] = RenderTextureBarrier(g_blankTextures[i].get(), RenderTextureLayout::SHADER_READ);

    g_commandLists[g_frame]->barriers(RenderBarrierStage::NONE, blankTextureBarriers, std::size(blankTextureBarriers));

    return true;
}

static uint32_t g_waitForGPUCount = 0;

void Video::WaitForGPU()
{
    g_waitForGPUCount++;

    // Wait for all queued frames to finish.
    for (size_t i = 0; i < NUM_FRAMES; i++)
    {
        if (g_commandListStates[i])
        {
            g_queue->waitForCommandFence(g_commandFences[i].get());
            g_commandListStates[i] = false;
        }
    }

    // Execute an empty command list and wait for it to end to guarantee that any remaining presentation has finished.
    g_commandLists[0]->begin();
    g_commandLists[0]->end();
    g_queue->executeCommandLists(g_commandLists[0].get(), g_commandFences[0].get());
    g_queue->waitForCommandFence(g_commandFences[0].get());
}

static uint32_t CreateDevice(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, be<uint32_t>* a6)
{
    g_xdbfTextureCache = std::unordered_map<uint16_t, GuestTexture *>();

    for (auto &achievement : g_xdbfWrapper.GetAchievements(XDBF_LANGUAGE_ENGLISH))
    {
        // huh?
        if (!achievement.pImageBuffer || !achievement.ImageBufferSize)
            continue;

        g_xdbfTextureCache[achievement.ID] =
            LoadTexture((uint8_t *)achievement.pImageBuffer, achievement.ImageBufferSize).release();
    }

    // Move backbuffer to guest memory.
    assert(!g_memory.IsInMemoryRange(g_backBuffer) && g_backBufferHolder != nullptr);
    g_backBuffer = g_userHeap.AllocPhysical<GuestSurface>(std::move(*g_backBufferHolder));

    // Check for stale reference. BeginCommandList() gets called before CreateDevice() which is where the assignment happens.
    if (g_renderTarget == g_backBufferHolder.get()) g_renderTarget = g_backBuffer;
    if (g_depthStencil == g_backBufferHolder.get()) g_depthStencil = g_backBuffer;

    // Free the host backbuffer.
    g_backBufferHolder = nullptr;

    auto device = g_userHeap.AllocPhysical<GuestDevice>();
    memset(device, 0, sizeof(*device));

    // Append render state functions to the end of guest function table.
    uint32_t functionOffset = PPC_CODE_BASE + PPC_CODE_SIZE;
    g_memory.InsertFunction(functionOffset, HostToGuestFunction<SetRenderStateUnimplemented>);

    for (size_t i = 0; i < std::size(device->setRenderStateFunctions); i++)
        device->setRenderStateFunctions[i] = functionOffset;

    for (auto& [state, function] : g_setRenderStateFunctions)
    {
        functionOffset += 4;
        g_memory.InsertFunction(functionOffset, function);
        device->setRenderStateFunctions[state / 4] = functionOffset;
    }

    for (size_t i = 0; i < std::size(device->setSamplerStateFunctions); i++)
        device->setSamplerStateFunctions[i] = *reinterpret_cast<uint32_t*>(g_memory.Translate(0x8330F3DC + i * 0xC));

    device->viewport.width = 1280.0f;
    device->viewport.height = 720.0f;
    device->viewport.maxZ = 1.0f;

    *a6 = g_memory.MapVirtual(device);

    return 0;
}

static void DestructResource(GuestResource* resource) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::DestructResource;
    cmd.destructResource.resource = resource;
    g_renderQueue.enqueue(cmd);
}

static void ProcDestructResource(const RenderCommand& cmd)
{
    const auto& args = cmd.destructResource;
    g_tempResources[g_frame].push_back(args.resource);
}

static uint32_t ComputeTexturePitch(GuestTexture* texture)
{
    return (texture->width * RenderFormatSize(texture->format) + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
}

static void LockTextureRect(GuestTexture* texture, uint32_t, GuestLockedRect* lockedRect) 
{
    uint32_t pitch = ComputeTexturePitch(texture);
    uint32_t slicePitch = pitch * texture->height;

    if (texture->mappedMemory == nullptr)
        texture->mappedMemory = g_userHeap.AllocPhysical(slicePitch, 0x10);

    lockedRect->pitch = pitch;
    lockedRect->bits = g_memory.MapVirtual(texture->mappedMemory);
}

static void UnlockTextureRect(GuestTexture* texture) 
{
    assert(std::this_thread::get_id() == g_presentThreadId);

    RenderCommand cmd;
    cmd.type = RenderCommandType::UnlockTextureRect;
    cmd.unlockTextureRect.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void ProcUnlockTextureRect(const RenderCommand& cmd)
{
    const auto& args = cmd.unlockTextureRect;

    AddBarrier(args.texture, RenderTextureLayout::COPY_DEST);
    FlushBarriers();

    uint32_t pitch = ComputeTexturePitch(args.texture);
    uint32_t slicePitch = pitch * args.texture->height;

    auto allocation = g_uploadAllocators[g_frame].allocate(slicePitch, PLACEMENT_ALIGNMENT);
    memcpy(allocation.memory, args.texture->mappedMemory, slicePitch);

    g_commandLists[g_frame]->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(args.texture->texture, 0),
        RenderTextureCopyLocation::PlacedFootprint(allocation.buffer, args.texture->format, args.texture->width, args.texture->height, 1, pitch / RenderFormatSize(args.texture->format), allocation.offset));
}

static void* LockBuffer(GuestBuffer* buffer, uint32_t flags)
{
    buffer->lockedReadOnly = (flags & 0x10) != 0;

    if (buffer->mappedMemory == nullptr)
        buffer->mappedMemory = g_userHeap.AllocPhysical(buffer->dataSize, 0x10);

    return buffer->mappedMemory;
}

static void* LockVertexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags)
{
    return LockBuffer(buffer, flags);
}

static std::atomic<uint32_t> g_bufferUploadCount = 0;

template<typename T>
static void UnlockBuffer(GuestBuffer* buffer, bool useCopyQueue)
{
    auto copyBuffer = [&](T* dest)
        {
            auto src = reinterpret_cast<const T*>(buffer->mappedMemory);

            for (size_t i = 0; i < buffer->dataSize; i += sizeof(T))
            {
                *dest = ByteSwap(*src);
                ++dest;
                ++src;
            }
        };

    if (useCopyQueue && g_capabilities.gpuUploadHeap)
    {
        copyBuffer(reinterpret_cast<T*>(buffer->buffer->map()));
        buffer->buffer->unmap();
    }
    else
    {
        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(buffer->dataSize));
        copyBuffer(reinterpret_cast<T*>(uploadBuffer->map()));
        uploadBuffer->unmap();

        if (useCopyQueue)
        {
            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
                });
        }
        else
        {
            auto& commandList = g_commandLists[g_frame];

            commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::WRITE));
            commandList->copyBufferRegion(buffer->buffer->at(0), uploadBuffer->at(0), buffer->dataSize);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(buffer->buffer.get(), RenderBufferAccess::READ));

            g_tempBuffers[g_frame].emplace_back(std::move(uploadBuffer));
        }
    }

    g_bufferUploadCount++;
}

template<typename T>
static void UnlockBuffer(GuestBuffer* buffer)
{
    if (!buffer->lockedReadOnly)
    {
        if (std::this_thread::get_id() == g_presentThreadId)
        {
            RenderCommand cmd;
            cmd.type = (sizeof(T) == 2) ? RenderCommandType::UnlockBuffer16 : RenderCommandType::UnlockBuffer32;
            cmd.unlockBuffer.buffer = buffer;
            g_renderQueue.enqueue(cmd);
        }
        else
        {
            UnlockBuffer<T>(buffer, true);
        }
    }
}

static void ProcUnlockBuffer16(const RenderCommand& cmd)
{
    UnlockBuffer<uint16_t>(cmd.unlockBuffer.buffer, false);
}

static void ProcUnlockBuffer32(const RenderCommand& cmd)
{
    UnlockBuffer<uint32_t>(cmd.unlockBuffer.buffer, false);
}

static void UnlockVertexBuffer(GuestBuffer* buffer)
{
    UnlockBuffer<uint32_t>(buffer);
}

static void GetVertexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc) 
{
    desc->size = buffer->dataSize;
}

static void* LockIndexBuffer(GuestBuffer* buffer, uint32_t, uint32_t, uint32_t flags) 
{
    return LockBuffer(buffer, flags);
}

static void UnlockIndexBuffer(GuestBuffer* buffer) 
{
    if (buffer->guestFormat == D3DFMT_INDEX32)
        UnlockBuffer<uint32_t>(buffer);
    else
        UnlockBuffer<uint16_t>(buffer);
}

static void GetIndexBufferDesc(GuestBuffer* buffer, GuestBufferDesc* desc)
{
    desc->format = buffer->guestFormat;
    desc->size = buffer->dataSize;
}

static void GetSurfaceDesc(GuestSurface* surface, GuestSurfaceDesc* desc) 
{
    desc->width = surface->width;
    desc->height = surface->height;
}

static void GetVertexDeclaration(GuestVertexDeclaration* vertexDeclaration, GuestVertexElement* vertexElements, be<uint32_t>* count) 
{
    memcpy(vertexElements, vertexDeclaration->vertexElements.get(), vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement));
    *count = vertexDeclaration->vertexElementCount;
}

static uint32_t HashVertexDeclaration(uint32_t vertexDeclaration) 
{
    // Vertex declarations are cached on host side, so the pointer itself can be used.
    return vertexDeclaration;
}

static const char *DeviceTypeName(RenderDeviceType type)
{
    switch (type) 
    {
    case RenderDeviceType::INTEGRATED:
        return "Integrated";
    case RenderDeviceType::DISCRETE:
        return "Discrete";
    case RenderDeviceType::VIRTUAL:
        return "Virtual";
    case RenderDeviceType::CPU:
        return "CPU";
    default:
        return "Unknown";
    }
}

static void DrawProfiler()
{
    bool toggleProfiler = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_F1] != 0;

    if (!g_profilerWasToggled && toggleProfiler)
    {
        g_profilerVisible = !g_profilerVisible;

        GameWindow::SetFullscreenCursorVisibility(App::s_isInit ? g_profilerVisible : true);
    }

    g_profilerWasToggled = toggleProfiler;

    if (!g_profilerVisible)
        return;

    ImFont* font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
    float defaultScale = font->Scale;
    font->Scale = ImGui::GetDefaultFont()->FontSize / font->FontSize;
    ImGui::PushFont(font);

    if (ImGui::Begin("Profiler", &g_profilerVisible))
    {
        g_applicationValues[g_profilerValueIndex] = App::s_deltaTime * 1000.0;

        const double applicationAvg = std::accumulate(g_applicationValues, g_applicationValues + PROFILER_VALUE_COUNT, 0.0) / PROFILER_VALUE_COUNT;
        double gpuFrameAvg = g_gpuFrameProfiler.UpdateAndReturnAverage();
        double presentAvg = g_presentProfiler.UpdateAndReturnAverage();
        double updateDirectorAvg = g_updateDirectorProfiler.UpdateAndReturnAverage();
        double renderDirectorAvg = g_renderDirectorProfiler.UpdateAndReturnAverage();
        double frameFenceAvg = g_frameFenceProfiler.UpdateAndReturnAverage();
        double presentWaitAvg = g_presentWaitProfiler.UpdateAndReturnAverage();
        double swapChainAcquireAvg = g_swapChainAcquireProfiler.UpdateAndReturnAverage();

        if (ImPlot::BeginPlot("Frame Time"))
        {
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 20.0);
            ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_None);
            ImPlot::PlotLine<double>("Application", g_applicationValues, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("GPU Frame", g_gpuFrameProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Present", g_presentProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Update Director", g_updateDirectorProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Render Director", g_renderDirectorProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Frame Fence", g_frameFenceProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Present Wait", g_presentWaitProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::PlotLine<double>("Swap Chain Acquire", g_swapChainAcquireProfiler.values, PROFILER_VALUE_COUNT, 1.0, 0.0, ImPlotLineFlags_None, g_profilerValueIndex);
            ImPlot::EndPlot();
        }

        g_profilerValueIndex = (g_profilerValueIndex + 1) % PROFILER_VALUE_COUNT;

        ImGui::Text("Current Application: %g ms (%g FPS)", App::s_deltaTime * 1000.0, 1.0 / App::s_deltaTime);
        ImGui::Text("Current GPU Frame: %g ms (%g FPS)", g_gpuFrameProfiler.value.load(), 1000.0 / g_gpuFrameProfiler.value.load());
        ImGui::Text("Current Present: %g ms (%g FPS)", g_presentProfiler.value.load(), 1000.0 / g_presentProfiler.value.load());
        ImGui::Text("Current Update Director: %g ms (%g FPS)", g_updateDirectorProfiler.value.load(), 1000.0 / g_updateDirectorProfiler.value.load());
        ImGui::Text("Current Render Director: %g ms (%g FPS)", g_renderDirectorProfiler.value.load(), 1000.0 / g_renderDirectorProfiler.value.load());
        ImGui::Text("Current Frame Fence: %g ms", g_frameFenceProfiler.value.load());
        ImGui::Text("Current Present Wait: %g ms", g_presentWaitProfiler.value.load());
        ImGui::Text("Current Swap Chain Acquire: %g ms", g_swapChainAcquireProfiler.value.load());

        ImGui::NewLine();

        ImGui::Text("Average Application: %g ms (%g FPS)", applicationAvg, 1000.0 / applicationAvg);
        ImGui::Text("Average GPU Frame: %g ms (%g FPS)", gpuFrameAvg, 1000.0 / gpuFrameAvg);
        ImGui::Text("Average Present: %g ms (%g FPS)", presentAvg, 1000.0 / presentAvg);
        ImGui::Text("Average Update Director: %g ms (%g FPS)", updateDirectorAvg, 1000.0 / updateDirectorAvg);
        ImGui::Text("Average Render Director: %g ms (%g FPS)", renderDirectorAvg, 1000.0 / renderDirectorAvg);
        ImGui::Text("Average Frame Fence: %g ms", frameFenceAvg);
        ImGui::Text("Average Present Wait: %g ms", presentWaitAvg);
        ImGui::Text("Average Swap Chain Acquire: %g ms", swapChainAcquireAvg);

        ImGui::NewLine();

        if (g_userHeap.heap != nullptr && g_userHeap.physicalHeap != nullptr)
        {
            O1HeapDiagnostics diagnostics, physicalDiagnostics;
            {
                std::lock_guard lock(g_userHeap.mutex);
                diagnostics = o1heapGetDiagnostics(g_userHeap.heap);
            }
            {
                std::lock_guard lock(g_userHeap.physicalMutex);
                physicalDiagnostics = o1heapGetDiagnostics(g_userHeap.physicalHeap);
            }

            ImGui::Text("Heap Allocated: %d MB", int32_t(diagnostics.allocated / (1024 * 1024)));
            ImGui::Text("Physical Heap Allocated: %d MB", int32_t(physicalDiagnostics.allocated / (1024 * 1024)));
        }

        ImGui::Text("GPU Waits: %d", int32_t(g_waitForGPUCount));
        ImGui::Text("Buffer Uploads: %d", int32_t(g_bufferUploadCount));
        ImGui::NewLine();

        ImGui::Text("Present Wait: %s", g_capabilities.presentWait ? "Supported" : "Unsupported");
        ImGui::Text("Triangle Fan: %s", g_capabilities.triangleFan ? "Supported" : "Unsupported");
        ImGui::Text("Dynamic Depth Bias: %s", g_capabilities.dynamicDepthBias ? "Supported" : "Unsupported");
        ImGui::Text("Triangle Strip Workaround: %s", g_triangleStripWorkaround ? "Enabled" : "Disabled");
        ImGui::Text("Hardware Resolve: %s", g_hardwareResolve ? "Enabled" : "Disabled");
        ImGui::Text("Hardware Depth Resolve: %s", g_hardwareDepthResolve ? "Enabled" : "Disabled");
        ImGui::NewLine();

        ImGui::Text("API: %s", g_vulkan ? "Vulkan" : "D3D12");
        ImGui::Text("Device: %s", g_device->getDescription().name.c_str());
        ImGui::Text("Device Type: %s", DeviceTypeName(g_device->getDescription().type));
        ImGui::Text("VRAM: %.2f MiB", (double)(g_device->getDescription().dedicatedVideoMemory) / (1024.0 * 1024.0));
        ImGui::Text("UMA: %s", g_capabilities.uma ? "Supported" : "Unsupported");
        ImGui::Text("GPU Upload Heap: %s", g_capabilities.gpuUploadHeap ? "Supported" : "Unsupported");

        const char* sdlVideoDriver = SDL_GetCurrentVideoDriver();
        if (sdlVideoDriver != nullptr)
            ImGui::Text("SDL Video Driver: %s", sdlVideoDriver);

        ImGui::NewLine();
        ImGui::Checkbox("Show FPS", &Config::ShowFPS.Value);
        ImGui::NewLine();

        if (ImGui::TreeNode("Device Names"))
        {
            ImGui::Indent();

            uint32_t deviceIndex = 0;
            for (const std::string &deviceName : g_interface->getDeviceNames())
            {
                ImGui::Text("Option #%d: %s", deviceIndex++, deviceName.c_str());
            }

            ImGui::Unindent();
            ImGui::TreePop();
        }
    }
    ImGui::End();

    ImGui::PopFont();
    font->Scale = defaultScale;
}


// AP_MEDALS_HUD_V1: 141-frame looping Blender animation at 60 FPS.
static std::unique_ptr<GuestTexture> g_apMedalsAtlas;
static bool g_apMedalsAtlasAttempted = false;
static void DrawAPMedalsHud()
{
    const int medalCount = APClient::GetMedalGoalCount();
    if (medalCount < 0) return;
    // AP_MEDALS_EMBEDDED_RESOURCE_V1
    if (!g_apMedalsAtlasAttempted)
    {
        g_apMedalsAtlasAttempted = true;

#ifdef _WIN32
        HMODULE module = GetModuleHandleW(nullptr);

        HRSRC resource = FindResourceA(
            module,
            MAKEINTRESOURCEA(2001),
            RT_RCDATA
        );

        if (resource)
        {
            DWORD resourceSize = SizeofResource(module, resource);
            HGLOBAL loaded = LoadResource(module, resource);

            if (loaded && resourceSize > 0)
            {
                const void* data = LockResource(loaded);

                if (data)
                {
                    g_apMedalsAtlas = LoadTexture(
                        static_cast<const uint8_t*>(data),
                        static_cast<size_t>(resourceSize)
                    );
                }
            }
        }
#endif

        if (!g_apMedalsAtlas)
        {
            std::printf(
                "AP MEDALS HUD: embedded atlas missing or invalid\n"
            );
        }
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(display.y / 1080.0f, 0.65f, 1.6f);
    const float iconSize = 104.0f * scale;
    const float x = 22.0f * scale;
    const float y = display.y - iconSize - 20.0f * scale;
    auto* draw = ImGui::GetForegroundDrawList();
    if (g_apMedalsAtlas)
    {
        const int frame = static_cast<int>(std::floor(ImGui::GetTime() * 60.0)) % 141;
        const int col = frame % 12, row = frame / 12;
        const ImVec2 uv0(col / 12.0f, row / 12.0f);
        const ImVec2 uv1((col + 1) / 12.0f, (row + 1) / 12.0f);
        draw->AddImage(g_apMedalsAtlas.get(), ImVec2(x,y), ImVec2(x+iconSize,y+iconSize), uv0, uv1);
    }
    // AP_MEDALS_TEXT_V3: floating metallic silver text, no background.
    const int medalTarget = APClient::GetMedalGoalTarget();
    const std::string medalText = fmt::format("{} / {}", medalCount, medalTarget);
    ImFont* medalFont = ImGui::GetFont();
    const float medalFontSize = 33.0f * scale;
    const ImVec2 medalPos(x + iconSize + 12.0f * scale, y + 27.0f * scale);
    const ImVec2 medalExtent = medalFont->CalcTextSizeA(medalFontSize, FLT_MAX, 0.0f, medalText.c_str());
    const ImU32 silverTop = medalCount >= medalTarget ? IM_COL32(255, 242, 174, 255) : IM_COL32(250, 253, 255, 255);
    const ImU32 silverBottom = medalCount >= medalTarget ? IM_COL32(219, 154, 39, 255) : IM_COL32(133, 164, 193, 255);
    // A small dark outline improves legibility without a panel.
    const float outline = 1.4f * scale;
    draw->AddText(medalFont, medalFontSize, ImVec2(medalPos.x - outline, medalPos.y), IM_COL32(6, 13, 24, 240), medalText.c_str());
    draw->AddText(medalFont, medalFontSize, ImVec2(medalPos.x + outline, medalPos.y), IM_COL32(6, 13, 24, 240), medalText.c_str());
    draw->AddText(medalFont, medalFontSize, ImVec2(medalPos.x, medalPos.y - outline), IM_COL32(6, 13, 24, 240), medalText.c_str());
    draw->AddText(medalFont, medalFontSize, ImVec2(medalPos.x, medalPos.y + outline), IM_COL32(6, 13, 24, 240), medalText.c_str());
    const float splitY = medalPos.y + medalExtent.y * 0.48f;
    draw->PushClipRect(ImVec2(medalPos.x - 2.0f, medalPos.y - 2.0f),
                       ImVec2(medalPos.x + medalExtent.x + 3.0f, splitY), true);
    draw->AddText(medalFont, medalFontSize, medalPos, silverTop, medalText.c_str());
    draw->PopClipRect();
    draw->PushClipRect(ImVec2(medalPos.x - 2.0f, splitY),
                       ImVec2(medalPos.x + medalExtent.x + 3.0f, medalPos.y + medalExtent.y + 3.0f), true);
    draw->AddText(medalFont, medalFontSize, medalPos, silverBottom, medalText.c_str());
    draw->PopClipRect();
}

static void DrawFPS()
{
    if (!Config::ShowFPS)
        return;

    double time = ImGui::GetTime();
    static double updateTime = time;
    static double fps = 0;
    static double totalDeltaTime = 0.0;
    static uint32_t totalDeltaCount = 0;

    totalDeltaTime += g_presentProfiler.value.load();
    totalDeltaCount++;

    if (time - updateTime >= 1.0f)
    {
        fps = 1000.0 / std::max(totalDeltaTime / double(totalDeltaCount), 1.0);
        updateTime = time;
        totalDeltaTime = 0.0;
        totalDeltaCount = 0;
    }

    auto drawList = ImGui::GetBackgroundDrawList();

    auto fmt = fmt::format("FPS: {:.2f}", fps);
    auto font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
    auto fontSize = Scale(10);
    auto textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, fmt.c_str());

    ImVec2 min = { Scale(40), Scale(30) };
    ImVec2 max = { min.x + std::max(Scale(75), textSize.x + Scale(10)), min.y + Scale(15) };
    ImVec2 textPos = { min.x + Scale(2), CENTRE_TEXT_VERT(min, max, textSize) + Scale(0.2f) };

    drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 200));
    drawList->AddText(font, fontSize, textPos, IM_COL32_WHITE, fmt.c_str());
}

// AP_MEDAL_RADAR_AUDIO_V1_BEGIN
// SDL queue audio is confined to the ImGui/video thread. CustomMusic and
// the game's audio/mixer channels are not touched by this feature.
namespace APMedalRadarAudio
{
    static constexpr float kDetectionRadius = 50.0f;
    static constexpr int kFrequency = 44100;
    static constexpr int kChannels = 2;
    static constexpr int kBeepDurationMs = 70;
    static constexpr float kPi = 3.14159265358979323846f;
    static bool enabled = false;   // OFF on first launch
    static int volume = 35;       // percent; separate from game/music volume
    static bool loaded = false;
    static SDL_AudioDeviceID device = 0;
    static std::chrono::steady_clock::time_point lastBeep{};
    static unsigned beepCount = 0;
    static bool deviceErrorLogged = false;

    static std::filesystem::path SettingsPath()
    {
        // Always use the same user config directory as Unleashed Recompiled.
        return Config::GetConfigPath().parent_path() / "archipelago_medal_radar.json";
    }

    static void Load()
    {
        if (loaded) return;
        loaded = true;
        try
        {
            std::ifstream stream(SettingsPath());
            if (!stream) return;
            const auto data = nlohmann::json::parse(stream, nullptr, false);
            if (!data.is_object()) return;
            if (data.contains("enabled") && data["enabled"].is_boolean())
                enabled = data["enabled"].get<bool>();
            if (data.contains("volume") && data["volume"].is_number_integer())
                volume = std::clamp(data["volume"].get<int>(), 0, 100);
        }
        catch (...)
        {
            // Damaged/missing preference files should never stop gameplay.
            enabled = false;
            volume = 35;
        }
    }

    static void Save()
    {
        try
        {
            const auto path = SettingsPath();
            std::filesystem::create_directories(path.parent_path());
            const auto tmp = path.string() + ".tmp";
            {
                std::ofstream stream(tmp, std::ios::trunc);
                if (!stream) return;
                stream << nlohmann::json({{"enabled", enabled}, {"volume", volume}}).dump(2) << '\n';
                stream.flush();
                if (!stream) return;
            }
            // Rename may not replace the target on Windows. Remove the prior
            // small preferences file only after the new one is flushed.
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                ec.clear();
                std::filesystem::remove(path, ec);
                ec.clear();
                std::filesystem::rename(tmp, path, ec);
            }
        }
        catch (...) { /* non-critical settings write */ }
    }

    static void StopAudio()
    {
        if (device)
        {
            SDL_ClearQueuedAudio(device);
            SDL_CloseAudioDevice(device);
            device = 0;
        }
        lastBeep = {};
    }

    static bool StartDevice()
    {
        if (device) return true;
        if (deviceErrorLogged) return false;
        SDL_AudioSpec desired{};
        desired.freq = kFrequency;
        desired.format = AUDIO_F32SYS;
        desired.channels = kChannels;
        desired.samples = 1024;
        desired.callback = nullptr; // SDL_QueueAudio (push mode)
        device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (!device)
        {
            // A single diagnostic in the normal bug report, not every frame.
            deviceErrorLogged = true;
            LOGFN("AP MEDAL RADAR AUDIO: SDL_OpenAudioDevice failed: {}", SDL_GetError());
            return false;
        }
        SDL_PauseAudioDevice(device, 0);
        LOGN("AP MEDAL RADAR AUDIO: device ready (44.1kHz stereo float)");
        return true;
    }

    static void QueueBeep(float distance)
    {
        if (!StartDevice()) return;
        // Audible, brief sine pulse with fade-in/out to avoid clicks.
        // Faster and slightly higher pitched when nearer the medal.
        const float proximity = 1.0f - std::clamp(distance / kDetectionRadius, 0.0f, 1.0f);
        const float pitch = 740.0f + 240.0f * proximity;
        const float amplitude = 0.20f * (static_cast<float>(volume) / 100.0f);
        constexpr int frames = kFrequency * kBeepDurationMs / 1000;
        std::vector<float> samples(static_cast<size_t>(frames) * kChannels);
        for (int i = 0; i < frames; ++i)
        {
            const float t = static_cast<float>(i) / kFrequency;
            const float attack = std::min(1.0f, t / 0.004f);
            const float release = std::min(1.0f, (frames - 1 - i) / (kFrequency * 0.014f));
            const float env = std::min(attack, release);
            const float wave = amplitude * env * std::sin(2.0f * kPi * pitch * t);
            samples[static_cast<size_t>(i) * 2] = wave;
            samples[static_cast<size_t>(i) * 2 + 1] = wave;
        }
        // Limit output buffering: audio must stop quickly on stage changes.
        if (SDL_GetQueuedAudioSize(device) > static_cast<Uint32>(kFrequency * kChannels * sizeof(float) / 5))
            return;
        if (SDL_QueueAudio(device, samples.data(), static_cast<Uint32>(samples.size() * sizeof(float))) == 0)
        {
            if (++beepCount <= 3 || beepCount % 50 == 0)
                LOGFN("AP MEDAL RADAR AUDIO: beep #{} distance={:.2f} volume={}%", beepCount, distance, volume);
        }
    }

    static void Tick()
    {
        Load();
        if (!enabled || volume <= 0)
        {
            if (device) StopAudio();
            return;
        }
        float distance = -1.0f;
        if (!APMedalRadarBridge::GetFreshDistance(distance) || distance > kDetectionRadius)
        {
            // Clear previously queued sound immediately when no medal exists.
            if (device && SDL_GetQueuedAudioSize(device) != 0)
                SDL_ClearQueuedAudio(device);
            lastBeep = {};
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float farFactor = std::clamp(distance / kDetectionRadius, 0.0f, 1.0f);
        const float intervalSeconds = 0.12f + 0.85f * std::sqrt(farFactor);
        if (lastBeep.time_since_epoch().count() != 0 &&
            std::chrono::duration<float>(now - lastBeep).count() < intervalSeconds)
            return;
        if (!StartDevice()) return;
        lastBeep = now;
        QueueBeep(distance);
    }
}
// AP_MEDAL_RADAR_AUDIO_V1_END

static void DrawImGui()
{
    ImGui_ImplSDL2_NewFrame();

    auto& io = ImGui::GetIO();
    io.DisplaySize = { float(Video::s_viewportWidth), float(Video::s_viewportHeight) };

    // ImGui doesn't know that we center the screen for specific aspect ratio
    // settings, which causes mouse events to not work correctly. To fix this, 
    // we can adjust the mouse events before ImGui processes them.
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float mousePosScaleX = float(width) / float(GameWindow::s_width);
    float mousePosScaleY = float(height) / float(GameWindow::s_height);
    float mousePosOffsetX = (width - Video::s_viewportWidth) / 2.0f;
    float mousePosOffsetY = (height - Video::s_viewportHeight) / 2.0f;
    for (int i = 0; i < io.Ctx->InputEventsQueue.Size; i++)
    {
        auto& e = io.Ctx->InputEventsQueue[i];
        if (e.Type == ImGuiInputEventType_MousePos)
        {
            if (e.MousePos.PosX != -FLT_MAX)
            {
                e.MousePos.PosX *= mousePosScaleX;
                e.MousePos.PosX -= mousePosOffsetX;
            }

            if (e.MousePos.PosY != -FLT_MAX)
            {
                e.MousePos.PosY *= mousePosScaleY;
                e.MousePos.PosY -= mousePosOffsetY;
            }
        }
    }

    ImGui::NewFrame();

    APRouletteUI::PollInput();
    APRouletteUI::DrawOverlay();

    ResetImGuiCallbacks();

#ifdef ASYNC_PSO_DEBUG
    if (ImGui::Begin("Async PSO Stats"))
    {
        ImGui::Text("Pipelines Created In Render Thread: %d", g_pipelinesCreatedInRenderThread.load());
        ImGui::Text("Pipelines Created Asynchronously: %d", g_pipelinesCreatedAsynchronously.load());
        ImGui::Text("Pipelines Dropped: %d", g_pipelinesDropped.load());
        ImGui::Text("Pipelines Currently Compiling: %d", g_pipelinesCurrentlyCompiling.load());
        ImGui::Text("Compiling Pipeline Task Count: %d", g_compilingPipelineTaskCount.load());
        ImGui::Text("Pending Pipeline Task Count: %d", g_pendingPipelineTaskCount.load());

        std::lock_guard lock(g_debugMutex);
        ImGui::TextUnformatted(g_pipelineDebugText.c_str());
    }
    ImGui::End();
#endif

    // AP Connector : menu Archipelago integre.
    extern bool g_apConnectorVisible;

    // AP Sliding Menu Animation
    static float apSlideProgress = 0.0f;

    const float apSlideDelta =
        ImGui::GetIO().DeltaTime * 3.0f;

    if (g_apConnectorVisible)
    {
        apSlideProgress += apSlideDelta;
        if (apSlideProgress > 1.0f)
            apSlideProgress = 1.0f;
    }
    else
    {
        apSlideProgress -= apSlideDelta;
        if (apSlideProgress < 0.0f)
            apSlideProgress = 0.0f;
    }

    const float apSmooth =
        apSlideProgress * apSlideProgress *
        (3.0f - 2.0f * apSlideProgress);

    // AP Attached Sliding F8 Handle
    {
        const ImGuiViewport* apViewport = ImGui::GetMainViewport();

        // La poignee suit exactement la position du panneau.
        // Poignee attachee au coin superieur droit du panneau.
        // Ferme : F8 reste visible au bord gauche de l'ecran.
        // Ouvert : F8 accompagne le panneau vers la droite.
        const float apPanelX =
            -600.0f + 660.0f * apSmooth;

        const float apHandleX =
            apViewport->Pos.x + apPanelX + 600.0f;

        const float apHandleY =
            apViewport->Pos.y + 70.0f;

        ImGui::SetNextWindowPos(
            ImVec2(apHandleX, apHandleY),
            ImGuiCond_Always
        );

        ImGui::SetNextWindowSize(
            ImVec2(64.0f, 76.0f),
            ImGuiCond_Always
        );

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);

        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            ImVec4(0.025f, 0.08f, 0.18f, 0.97f)
        );

        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(0.25f, 0.70f, 1.0f, 1.0f)
        );

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.08f, 0.30f, 0.57f, 1.0f)
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.17f, 0.52f, 0.85f, 1.0f)
        );

        ImGui::Begin(
            "##APF8Tab",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoCollapse
        );

        ImFont* apTabFont =
            ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");

        ImGui::PushFont(apTabFont);
        ImGui::SetWindowFontScale(1.35f);

        if (ImGui::Button("F8", ImVec2(52.0f, 64.0f)))
        {
            g_apConnectorVisible = !g_apConnectorVisible;
        }

        ImGui::PopFont();

        // Reflets metalliques de la poignee.
        {
            ImVec2 p = ImGui::GetItemRectMin();
            ImVec2 s = ImGui::GetItemRectSize();

            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRect(
                p,
                ImVec2(p.x + s.x, p.y + s.y),
                IM_COL32(145, 220, 255, 230),
                7.0f, 0, 2.0f
            );

            draw->AddLine(
                ImVec2(p.x + 9.0f, p.y + 3.0f),
                ImVec2(p.x + s.x - 9.0f, p.y + 3.0f),
                IM_COL32(225, 245, 255, 180),
                2.0f
            );
        }

        ImGui::End();

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(4);
    }
    if (apSlideProgress > 0.0f)
    {
        SDL_ShowCursor(SDL_ENABLE);
        // Archipelago Connector - Midnight Blue Metal Theme.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 9.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 9.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.065f, 0.13f, 0.97f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.48f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.055f, 0.12f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.09f, 0.23f, 0.43f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.36f, 0.67f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.58f, 0.94f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.07f, 0.28f, 0.55f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.15f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.11f, 0.23f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.14f, 0.30f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.95f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.22f, 0.49f, 0.74f, 0.75f));

                // AP Sliding Window Position
        const float apMenuWidth = 600.0f;
        const float apHiddenX = -apMenuWidth;
        const float apVisibleX = 60.0f;

        const float apMenuX =
            apHiddenX +
            (apVisibleX - apHiddenX) * apSmooth;

        ImGui::SetNextWindowPos(
            ImVec2(apMenuX, 50.0f),
            ImGuiCond_Always
        );

        ImGui::SetNextWindowSize(
            ImVec2(600.0f, 900.0f),
            ImGuiCond_Always
        );

        if (ImGui::Begin(
            "Sonic Unleashed - Archipelago Connector",
            &g_apConnectorVisible,
            ImGuiWindowFlags_NoCollapse))
        {
            // AP Midnight Metal Background
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();

                ImVec2 p = ImGui::GetWindowPos();
                ImVec2 s = ImGui::GetWindowSize();

                const float x = p.x;
                const float y = p.y;
                const float w = s.x;
                const float h = s.y;

                // Texture metallique : fines lignes horizontales.
                for (float i = 48.0f; i < h - 12.0f; i += 7.0f)
                {
                    draw->AddLine(
                        ImVec2(x + 12.0f, y + i),
                        ImVec2(x + w - 12.0f, y + i),
                        IM_COL32(55, 125, 195, 17),
                        1.0f
                    );
                }

                // Encadrement interieur lumineux.
                draw->AddRect(
                    ImVec2(x + 6.0f, y + 30.0f),
                    ImVec2(x + w - 6.0f, y + h - 6.0f),
                    IM_COL32(35, 150, 245, 135),
                    12.0f, 0, 1.5f
                );

                // Trois griffures diagonales inspirees du Werehog.
                for (int i = 0; i < 3; ++i)
                {
                    float offset = static_cast<float>(i) * 29.0f;

                    ImVec2 a(
                        x + w - 105.0f + offset,
                        y + h * 0.61f
                    );

                    ImVec2 b(
                        x + w - 205.0f + offset,
                        y + h - 32.0f
                    );

                    // Halo bleu autour de chaque griffure.
                    draw->AddLine(
                        a, b,
                        IM_COL32(0, 110, 255, 38),
                        10.0f
                    );

                    // Trait principal.
                    draw->AddLine(
                        a, b,
                        IM_COL32(25, 145, 255, 115),
                        3.0f
                    );

                    // Reflet central.
                    draw->AddLine(
                        a, b,
                        IM_COL32(145, 225, 255, 145),
                        1.0f
                    );
                }

                // Accents lumineux dans les coins.
                draw->AddLine(
                    ImVec2(x + 22.0f, y + 34.0f),
                    ImVec2(x + 110.0f, y + 34.0f),
                    IM_COL32(60, 205, 255, 185),
                    2.0f
                );

                draw->AddLine(
                    ImVec2(x + w - 110.0f, y + h - 12.0f),
                    ImVec2(x + w - 22.0f, y + h - 12.0f),
                    IM_COL32(60, 205, 255, 185),
                    2.0f
                );
            }

            // Texte plus grand pour le menu Archipelago.
            ImFont* apBubbleFont =
                ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");

            ImGui::PushFont(apBubbleFont);
            ImGui::SetWindowFontScale(0.85f);

            // AP Metallic Header
            {
                ImVec2 headerPos = ImGui::GetCursorScreenPos();
                float headerWidth = ImGui::GetContentRegionAvail().x;
                float headerHeight = 100.0f;

                ImDrawList* draw = ImGui::GetWindowDrawList();

                ImVec2 headerEnd(
                    headerPos.x + headerWidth,
                    headerPos.y + headerHeight
                );

                // Fond metallique en degrade bleu nuit.
                draw->AddRectFilledMultiColor(
                    headerPos,
                    headerEnd,
                    IM_COL32(19, 66, 116, 255),
                    IM_COL32(5, 24, 56, 255),
                    IM_COL32(3, 13, 35, 255),
                    IM_COL32(10, 38, 77, 255)
                );

                // Encadrement chrome.
                draw->AddRect(
                    headerPos,
                    headerEnd,
                    IM_COL32(125, 195, 240, 230),
                    8.0f, 0, 2.0f
                );

                // Ligne lumineuse superieure.
                draw->AddLine(
                    ImVec2(headerPos.x + 15.0f, headerPos.y + 3.0f),
                    ImVec2(headerEnd.x - 15.0f, headerPos.y + 3.0f),
                    IM_COL32(180, 235, 255, 190),
                    2.0f
                );

                // Petit embleme technologique.
                ImVec2 emblem(
                    headerPos.x + 35.0f,
                    headerPos.y + 42.0f
                );

                draw->AddCircleFilled(
                    emblem,
                    24.0f,
                    IM_COL32(5, 24, 53, 255),
                    32
                );

                draw->AddCircle(
                    emblem,
                    23.0f,
                    IM_COL32(80, 190, 255, 230),
                    32, 2.5f
                );

                draw->AddCircle(
                    emblem,
                    16.0f,
                    IM_COL32(160, 225, 255, 190),
                    32, 1.5f
                );

                // Lignes du globe Archipelago.
                draw->AddLine(
                    ImVec2(emblem.x - 16.0f, emblem.y),
                    ImVec2(emblem.x + 16.0f, emblem.y),
                    IM_COL32(110, 215, 255, 220),
                    1.5f
                );

                draw->AddLine(
                    ImVec2(emblem.x, emblem.y - 16.0f),
                    ImVec2(emblem.x, emblem.y + 16.0f),
                    IM_COL32(110, 215, 255, 220),
                    1.5f
                );

                // Titres du panneau.
                draw->AddText(
                    ImVec2(headerPos.x + 72.0f, headerPos.y + 17.0f),
                    IM_COL32(155, 210, 255, 255),
                    "SONIC UNLEASHED"
                );

                draw->AddText(
                    ImVec2(headerPos.x + 72.0f, headerPos.y + 43.0f),
                    IM_COL32(240, 248, 255, 255),
                    "ARCHIPELAGO CONNECTOR"
                );

                // Credits du createur, toujours visibles dans l'en-tete F8.
                draw->AddText(
                    ImVec2(headerPos.x + 74.0f, headerPos.y + 62.0f),
                    IM_COL32(155, 225, 255, 255),
                    "Created by ZMGB_Zak"
                );

                // Barre lumineuse sous le titre.
                draw->AddLine(
                    ImVec2(headerPos.x + 73.0f, headerPos.y + 87.0f),
                    ImVec2(headerEnd.x - 18.0f, headerPos.y + 87.0f),
                    IM_COL32(40, 175, 255, 170),
                    2.0f
                );

                // Reserver l'espace pour les autres controles.
                ImGui::Dummy(ImVec2(0.0f, headerHeight));
            }
            // AP_F8_REORDER_V1: update checker near connector title.
#ifdef _WIN32
            APUpdateV1::DrawControls();
#endif
            ImGui::Separator();

            ImGui::Spacing();
            // AP Animated Connection Status
            ImGui::TextColored(
                ImVec4(0.40f, 0.82f, 1.0f, 1.0f),
                "STATUS"
            );

            const auto apState = APClient::GetConnectionState();

            const char* apStatusText = "Disconnected";
            ImVec4 apStatusColor(1.0f, 0.25f, 0.25f, 1.0f);

            switch (apState)
            {
            case APClient::ConnectionState::Connected:
                apStatusText = "Connected";
                apStatusColor = ImVec4(0.10f, 1.0f, 0.25f, 1.0f);
                break;

            case APClient::ConnectionState::Connecting:
                apStatusText = "Connecting...";
                apStatusColor = ImVec4(1.0f, 0.70f, 0.10f, 1.0f);
                break;

            case APClient::ConnectionState::Disconnecting:
                apStatusText = "Disconnecting...";
                apStatusColor = ImVec4(1.0f, 0.55f, 0.10f, 1.0f);
                break;

            default:
                break;
            }

            // Animation douce : cycle d'environ 2.8 secondes.
            const float apPulse =
                0.5f + 0.5f * sinf(
                    static_cast<float>(ImGui::GetTime()) * 2.25f
                );

            ImGui::BeginChild(
                "##APStatusPanel",
                ImVec2(0.0f, 65.0f),
                true,
                ImGuiWindowFlags_NoScrollbar
            );

            ImDrawList* apStatusDraw = ImGui::GetWindowDrawList();
            ImVec2 apPanelPos = ImGui::GetWindowPos();

            // AP Neon Status Frame
            ImVec2 apPanelSize = ImGui::GetWindowSize();

            apStatusDraw->AddRectFilled(
                ImVec2(apPanelPos.x + 2.0f, apPanelPos.y + 2.0f),
                ImVec2(
                    apPanelPos.x + apPanelSize.x - 2.0f,
                    apPanelPos.y + apPanelSize.y - 2.0f
                ),
                IM_COL32(3, 17, 39, 215),
                9.0f
            );

            // Halo bleu discret.
            apStatusDraw->AddRect(
                ImVec2(apPanelPos.x + 3.0f, apPanelPos.y + 3.0f),
                ImVec2(
                    apPanelPos.x + apPanelSize.x - 3.0f,
                    apPanelPos.y + apPanelSize.y - 3.0f
                ),
                IM_COL32(0, 125, 255, 65),
                9.0f, 0, 4.0f
            );

            // Contour cyan metallique.
            apStatusDraw->AddRect(
                ImVec2(apPanelPos.x + 3.0f, apPanelPos.y + 3.0f),
                ImVec2(
                    apPanelPos.x + apPanelSize.x - 3.0f,
                    apPanelPos.y + apPanelSize.y - 3.0f
                ),
                IM_COL32(65, 185, 255, 210),
                9.0f, 0, 1.5f
            );

            // Reflet sur le bord superieur.
            apStatusDraw->AddLine(
                ImVec2(apPanelPos.x + 20.0f, apPanelPos.y + 4.0f),
                ImVec2(
                    apPanelPos.x + apPanelSize.x - 20.0f,
                    apPanelPos.y + 4.0f
                ),
                IM_COL32(145, 225, 255, 100),
                1.0f
            );

            ImVec2 apLedCenter(
                apPanelPos.x + 30.0f,
                apPanelPos.y + 32.0f
            );

            // Halo lumineux pulsant.
            apStatusDraw->AddCircleFilled(
                apLedCenter,
                19.0f + apPulse * 5.0f,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(
                        apStatusColor.x,
                        apStatusColor.y,
                        apStatusColor.z,
                        0.06f + apPulse * 0.12f
                    )
                ),
                32
            );

            // Corps de la LED.
            apStatusDraw->AddCircleFilled(
                apLedCenter,
                11.0f,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(
                        apStatusColor.x,
                        apStatusColor.y,
                        apStatusColor.z,
                        0.65f + apPulse * 0.35f
                    )
                ),
                32
            );

            // Petit reflet blanc.
            apStatusDraw->AddCircleFilled(
                ImVec2(apLedCenter.x - 3.0f, apLedCenter.y - 3.0f),
                3.0f,
                IM_COL32(235, 250, 255, 160),
                16
            );

            ImGui::SetCursorPos(ImVec2(55.0f, 19.0f));
            ImGui::TextColored(apStatusColor, "%s", apStatusText);

            ImGui::EndChild();

            ImGui::Spacing();

            const bool apCanConnect =
                apState == APClient::ConnectionState::Disconnected;

            ImGui::BeginDisabled(!apCanConnect);

            bool apConnectRequested = false;
            if (ImGui::Button("CONNECT", ImVec2(175, 40)))
            {
                apConnectRequested = true;
            }

            // Reflet brillant sur le bouton CONNECT.
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(p.x + s.x - 5.0f, p.y + s.y * 0.38f),
                    IM_COL32(180, 225, 255, 55),
                    10.0f
                );
            }

            // AP Metallic Button Border
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImVec2 bottom(p.x + s.x, p.y + s.y);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                // Bordure exterieure chrome argente bleute.
                draw->AddRect(
                    ImVec2(p.x + 1.0f, p.y + 1.0f),
                    ImVec2(bottom.x - 1.0f, bottom.y - 1.0f),
                    IM_COL32(160, 215, 245, 235),
                    13.0f, 0, 2.5f
                );

                // Contour interieur bleu fonce.
                draw->AddRect(
                    ImVec2(p.x + 4.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 4.0f, bottom.y - 4.0f),
                    IM_COL32(15, 55, 105, 210),
                    10.0f, 0, 1.5f
                );

                // Lumiere metallique sur le bord superieur.
                draw->AddLine(
                    ImVec2(p.x + 17.0f, p.y + 2.0f),
                    ImVec2(bottom.x - 17.0f, p.y + 2.0f),
                    IM_COL32(225, 245, 255, 170),
                    1.5f
                );
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            const bool apCanDisconnect =
                apState == APClient::ConnectionState::Connected;

            ImGui::BeginDisabled(!apCanDisconnect);

            if (ImGui::Button("DISCONNECT", ImVec2(200, 40)))
            {
                APClient::Disconnect();
            }

            // Reflet brillant sur le bouton DISCONNECT.
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(p.x + s.x - 5.0f, p.y + s.y * 0.38f),
                    IM_COL32(180, 225, 255, 55),
                    10.0f
                );
            }

            // AP Metallic Button Border
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImVec2 bottom(p.x + s.x, p.y + s.y);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                // Bordure exterieure chrome argente bleute.
                draw->AddRect(
                    ImVec2(p.x + 1.0f, p.y + 1.0f),
                    ImVec2(bottom.x - 1.0f, bottom.y - 1.0f),
                    IM_COL32(160, 215, 245, 235),
                    13.0f, 0, 2.5f
                );

                // Contour interieur bleu fonce.
                draw->AddRect(
                    ImVec2(p.x + 4.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 4.0f, bottom.y - 4.0f),
                    IM_COL32(15, 55, 105, 210),
                    10.0f, 0, 1.5f
                );

                // Lumiere metallique sur le bord superieur.
                draw->AddLine(
                    ImVec2(p.x + 17.0f, p.y + 2.0f),
                    ImVec2(bottom.x - 17.0f, p.y + 2.0f),
                    IM_COL32(225, 245, 255, 170),
                    1.5f
                );
            }

            ImGui::EndDisabled();
            ImGui::Spacing();

            // AP Connector : parametres de connexion.
            static char apServer[128] = {};
            static char apPort[16] = {};
            static char apSlot[128] = {};
            static char apPassword[128] = "";
            static bool apFieldsInitialized = false;

            if (!apFieldsInitialized)
            {
                snprintf(apServer, sizeof(apServer), "%s",
                    Config::ArchipelagoServer.Value.c_str());

                snprintf(apPort, sizeof(apPort), "%d",
                    Config::ArchipelagoPort.Value);

                snprintf(apSlot, sizeof(apSlot), "%s",
                    Config::ArchipelagoSlot.Value.c_str());

                apFieldsInitialized = true;
            }

            // AP Neon Connection Settings Panel
            ImGui::TextColored(
                ImVec4(0.40f, 0.82f, 1.0f, 1.0f),
                "CONNECTION SETTINGS"
            );

            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                ImVec4(0.015f, 0.055f, 0.12f, 0.94f)
            );

            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(0.25f, 0.70f, 1.0f, 0.90f)
            );

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(15.0f, 12.0f)
            );

            ImGui::BeginChild(
                "##APConnectionSettings",
                ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_Border |
                ImGuiChildFlags_AutoResizeY
            );

            // Ligne lumineuse dans la partie superieure du panneau.
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetWindowPos();
                ImVec2 s = ImGui::GetWindowSize();

                draw->AddLine(
                    ImVec2(p.x + 18.0f, p.y + 3.0f),
                    ImVec2(p.x + s.x - 18.0f, p.y + 3.0f),
                    IM_COL32(130, 220, 255, 115),
                    1.5f
                );
            }

            ImGui::TextUnformatted("SERVER");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##APServer", apServer, sizeof(apServer));

            ImGui::Spacing();

            ImGui::TextUnformatted("PORT");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##APPort", apPort, sizeof(apPort));

            ImGui::Spacing();

            ImGui::TextUnformatted("PLAYER");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##APSlot", apSlot, sizeof(apSlot));

            ImGui::Spacing();

            ImGui::TextUnformatted("PASSWORD (OPTIONAL)");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText(
                "##APPassword",
                apPassword,
                sizeof(apPassword),
                ImGuiInputTextFlags_Password
            );

            ImGui::EndChild();

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);

            // AP_F8_USABILITY_V4: visible saving next to connection inputs.
            ImGui::Spacing();
            ImGui::TextDisabled("CONNECT saves settings automatically.");
            // Interdire les changements de configuration pendant une connexion.
            ImGui::BeginDisabled(!apCanConnect);

            static int apSaveMessage = 0;

            const bool apClickedSaveSettings = ImGui::Button("SAVE SETTINGS", ImVec2(-1, 38));
            if (apClickedSaveSettings || apConnectRequested)
            {
                char* apPortEnd = nullptr;
                const long parsedPort = std::strtol(apPort, &apPortEnd, 10);

                if (apServer[0] != '\0' &&
                    apSlot[0] != '\0' &&
                    apPortEnd != apPort &&
                    *apPortEnd == '\0' &&
                    parsedPort >= 1 &&
                    parsedPort <= 65535)
                {
                    Config::ArchipelagoServer.Value = apServer;
                    Config::ArchipelagoPort.Value = static_cast<int32_t>(parsedPort);
                    Config::ArchipelagoSlot.Value = apSlot;

                    Config::Save();
                    apSaveMessage = 1;
                    if (apConnectRequested)
                        APClient::Start();
                }
                else
                {
                    apSaveMessage = 2;
                }
            }

            // AP Glossy Metallic Button
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImVec2 bottom(p.x + s.x, p.y + s.y);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                // Reflet brillant.
                draw->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 5.0f, p.y + s.y * 0.38f),
                    IM_COL32(180, 225, 255, 55),
                    10.0f
                );

                // Contour chrome argente bleute.
                draw->AddRect(
                    ImVec2(p.x + 1.0f, p.y + 1.0f),
                    ImVec2(bottom.x - 1.0f, bottom.y - 1.0f),
                    IM_COL32(160, 215, 245, 235),
                    13.0f, 0, 2.5f
                );

                // Contour interieur bleu fonce.
                draw->AddRect(
                    ImVec2(p.x + 4.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 4.0f, bottom.y - 4.0f),
                    IM_COL32(15, 55, 105, 210),
                    10.0f, 0, 1.5f
                );

                // Reflet metallique superieur.
                draw->AddLine(
                    ImVec2(p.x + 17.0f, p.y + 2.0f),
                    ImVec2(bottom.x - 17.0f, p.y + 2.0f),
                    IM_COL32(225, 245, 255, 170),
                    1.5f
                );
            }

            ImGui::EndDisabled();

            if (apSaveMessage == 1)
            {
                ImGui::TextColored(
                    ImVec4(0.30f, 0.95f, 0.55f, 1.0f),
                    "Settings saved successfully!");
            }
            else if (apSaveMessage == 2)
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Invalid server, port or player name.");
            }
            // AP_F8_USABILITY_V4_DONE
            // AP DEATH LINK SETTINGS - Animated toggles
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.40f, 0.82f, 1.0f, 1.0f),
                "DEATH LINK"
            );

            static float apDayToggleAnim = -1.0f;
            static float apNightToggleAnim = -1.0f;

            auto apDrawDeathLinkToggle =
                [](const char* label, const char* id,
                   bool& enabled, float& anim,
                   ImVec4 activeColor)
            {
                if (anim < 0.0f)
                    anim = enabled ? 1.0f : 0.0f;

                ImGui::PushID(id);

                ImGui::TextUnformatted(label);
                ImGui::SameLine(440.0f);

                const ImVec2 size(43.0f, 23.0f);
                const ImVec2 pos = ImGui::GetCursorScreenPos();

                if (ImGui::InvisibleButton("##toggle", size))
                {
                    enabled = !enabled;
                    Config::Save();
                }

                const float target = enabled ? 1.0f : 0.0f;
                const float delta = ImGui::GetIO().DeltaTime;

                // Animation fluide sur environ 200 ms.
                const float speed = 5.0f;
                const float step = delta * speed;

                if (anim < target)
                    anim = (anim + step < target) ? anim + step : target;
                else if (anim > target)
                    anim = (anim - step > target) ? anim - step : target;

                const ImVec4 offColor(0.30f, 0.35f, 0.43f, 1.0f);

                const ImVec4 color(
                    offColor.x + (activeColor.x - offColor.x) * anim,
                    offColor.y + (activeColor.y - offColor.y) * anim,
                    offColor.z + (activeColor.z - offColor.z) * anim,
                    1.0f
                );

                ImDrawList* draw = ImGui::GetWindowDrawList();

                draw->AddRectFilled(
                    pos,
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    ImGui::ColorConvertFloat4ToU32(color),
                    size.y * 0.5f
                );

                const float knobX =
                    pos.x + 11.5f + anim * 20.0f;

                draw->AddCircleFilled(
                    ImVec2(knobX, pos.y + 11.5f),
                    8.5f,
                    IM_COL32(255, 255, 255, 255)
                );

                ImGui::PopID();
            };

            apDrawDeathLinkToggle(
                "Sonic",
                "DayDeathLink",
                Config::DeathLinkSonicDay.Value,
                apDayToggleAnim,
                ImVec4(0.98f, 0.45f, 0.08f, 1.0f)
            );

            apDrawDeathLinkToggle(
                "Werehog",
                "NightDeathLink",
                Config::DeathLinkWerehog.Value,
                apNightToggleAnim,
                ImVec4(0.58f, 0.20f, 0.92f, 1.0f)
            );

            ImGui::Spacing();
            ImGui::Separator();

            // AP CUSTOM MUSIC - Bouton du menu F8.
            static bool apCustomMusicOpen = false;
            static bool apCustomMusicVisible = false;
            static float apCustomMusicAnim = 0.0f;

            if (ImGui::Button("CUSTOM MUSIC", ImVec2(-1, 42)))
                {
                    apCustomMusicOpen = true;
                    apCustomMusicVisible = true;
                }

            // AP Glossy Metallic CUSTOM MUSIC Button
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImVec2 bottom(p.x + s.x, p.y + s.y);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                draw->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 5.0f, p.y + s.y * 0.38f),
                    IM_COL32(180, 225, 255, 55),
                    10.0f
                );

                draw->AddRect(
                    ImVec2(p.x + 1.0f, p.y + 1.0f),
                    ImVec2(bottom.x - 1.0f, bottom.y - 1.0f),
                    IM_COL32(160, 215, 245, 235),
                    13.0f, 0, 2.5f
                );

                draw->AddRect(
                    ImVec2(p.x + 4.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 4.0f, bottom.y - 4.0f),
                    IM_COL32(15, 55, 105, 210),
                    10.0f, 0, 1.5f
                );

                draw->AddLine(
                    ImVec2(p.x + 17.0f, p.y + 2.0f),
                    ImVec2(bottom.x - 17.0f, p.y + 2.0f),
                    IM_COL32(225, 245, 255, 170),
                    1.5f
                );
            }

            ImGui::Spacing();

            // AP_MEDAL_RADAR_F8_V1_BEGIN
            // Simple controls in the F8 main menu (not the APWorld).
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.40f, 0.82f, 1.0f, 1.0f), "MEDAL RADAR");
            APMedalRadarAudio::Load();
            bool apRadarEnabled = APMedalRadarAudio::enabled;
            if (ImGui::Checkbox("Enable Medal Radar##APRadarEnabled", &apRadarEnabled))
            {
                APMedalRadarAudio::enabled = apRadarEnabled;
                if (!apRadarEnabled) APMedalRadarAudio::StopAudio();
                APMedalRadarAudio::Save();
            }
            int apRadarVolume = APMedalRadarAudio::volume;
            if (ImGui::SliderInt("Radar Volume##APRadarVolume", &apRadarVolume, 0, 100, "%d%%"))
                APMedalRadarAudio::volume = apRadarVolume;
            if (ImGui::IsItemDeactivatedAfterEdit())
                APMedalRadarAudio::Save();
            float apRadarDistance = 0.0f;
            if (APMedalRadarBridge::GetFreshDistance(apRadarDistance))
                ImGui::TextDisabled("Nearest uncollected medal: %.1f units", apRadarDistance);
            else
                ImGui::TextDisabled("No uncollected medal currently tracked");
            ImGui::TextDisabled("Beeps speed up within 50 units. OFF by default.");
            ImGui::Spacing();
            // AP_MEDAL_RADAR_F8_V1_END

            // AP_RING_ROULETTE_V1_F8_BEGIN
            APRouletteUI::DrawSettings();
            APMultiplayerOverlay::DrawSettings(); // AP_MULTIWORLD_HUD_V082_F8
            ImGui::Spacing();
            // AP_RING_ROULETTE_V1_F8_END

            // Ouvrir le sous-menu Custom Music.
            if (apCustomMusicVisible)
            {
                const float apAnimSpeed = ImGui::GetIO().DeltaTime / 0.30f;

                if (apCustomMusicOpen)
                    apCustomMusicAnim += apAnimSpeed;
                else
                    apCustomMusicAnim -= apAnimSpeed;

                if (apCustomMusicAnim < 0.0f)
                    apCustomMusicAnim = 0.0f;

                if (apCustomMusicAnim > 1.0f)
                    apCustomMusicAnim = 1.0f;

                if (!apCustomMusicOpen && apCustomMusicAnim <= 0.0f)
                    apCustomMusicVisible = false;

                // Ease-out cubique.
                const float apT = apCustomMusicAnim;
                const float apEase = 1.0f - (1.0f - apT) * (1.0f - apT) * (1.0f - apT);

                ImGui::SetNextWindowSize(ImVec2(850.0f, 750.0f), ImGuiCond_Always);

                const ImVec2 apDisplay = ImGui::GetIO().DisplaySize;
                const ImVec2 apTarget(
                    (apDisplay.x - 850.0f) * 0.5f,
                    (apDisplay.y - 750.0f) * 0.5f
                );

                ImGui::SetNextWindowPos(
                    ImVec2(apTarget.x - (75.0f * (1.0f - apEase)), apTarget.y),
                    ImGuiCond_Always
                );

                ImGui::SetNextWindowBgAlpha(0.98f * apEase);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, apEase);

                if (ImGui::Begin("Archipelago - Custom Music", &apCustomMusicOpen))
                {
                    ImGui::Text("CUSTOM MUSIC");
                    ImGui::Separator();

                    bool apCustomMusicEnabled = CustomMusic::IsEnabled();
                    if (ImGui::Checkbox("Enable Custom Music", &apCustomMusicEnabled))
{
    CustomMusic::SetEnabled(apCustomMusicEnabled);
}

                    if (ImGui::BeginTabBar("APCustomMusicTabs"))
                    {
                        if (ImGui::BeginTabItem("ACTS"))
{
    // AP_DAY_NIGHT_TABS_V1
    if (ImGui::BeginTabBar("APActsDayNightTabs"))
    {
        if (ImGui::BeginTabItem("DAY"))
        {
    ImGui::TextColored(
        ImVec4(0.40f, 0.82f, 1.0f, 1.0f),
        "DAY STAGE MUSIC"
    );

    ImGui::TextDisabled("One custom WAV for all Day Acts in each region.");
    ImGui::Spacing();
    ImGui::Separator();

    static const char* apDayRegions[] = {
        "Apotos",
        "Spagonia",
        "Mazuri",
        "Holoska",
        "Chun-nan",
        "Adabat",
        "Shamar",
        "Empire City",
        "Eggmanland"
    };

    // AP_ACTS_CLEAN_LIST_V1 - same single-scroll layout as TOWNS.
    ImGui::BeginChild("##APDayMusicList", ImVec2(0.0f, 520.0f), true);
    for (int i = 0; i < 9; ++i)
    {
        ImGui::PushID(i);
        ImGui::Text("%s - DAY", apDayRegions[i]);
        if (ImGui::Button("Browse WAV...", ImVec2(220.0f, 40.0f)))
        {
            if (NFD_Init() == NFD_OKAY)
            {
                nfdu8char_t* selectedPath = nullptr;
                const nfdu8filteritem_t filters[] = { { "WAV Files", "wav" } };
                const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);
                if (result == NFD_OKAY && selectedPath)
                {
                    CustomMusic::SetDayMusicPath(static_cast<CustomMusic::DayRegion>(i), selectedPath);
                    NFD_FreePathU8(selectedPath);
                }
                NFD_Quit();
            }
        }
        const char* selectedWav = CustomMusic::GetDayMusicPath(static_cast<CustomMusic::DayRegion>(i));
        if (selectedWav && *selectedWav)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.65f, 1.0f), "WAV Selected");
            ImGui::TextWrapped("%s", selectedWav);
        }
        else
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Default music");
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    // END AP_ACTS_CLEAN_LIST_V1
        ImGui::EndTabItem();
            }

        if (ImGui::BeginTabItem("NIGHT"))
        {
            ImGui::TextColored(
                ImVec4(0.65f, 0.45f, 1.0f, 1.0f),
                "NIGHT STAGE MUSIC"
            );
            ImGui::TextDisabled("One custom WAV for all Night Acts in each region.");

            // AP_NIGHT_BROWSE_UI_V1
            ImGui::Separator();
            ImGui::BeginChild("##APNightMusicList", ImVec2(0.0f, 520.0f), true);

            static const char* apNightRegions[] = {
                "Apotos", "Spagonia", "Mazuri",
                "Holoska", "Chun-nan", "Adabat",
                "Shamar", "Empire City", "Eggmanland"
            };

            for (int i = 0; i < 9; ++i)
            {
                ImGui::PushID(i);

                ImGui::TextColored(
                    ImVec4(0.65f, 0.45f, 1.0f, 1.0f),
                    "%s - NIGHT",
                    apNightRegions[i]
                );

                if (ImGui::Button("Browse WAV...", ImVec2(220.0f, 40.0f)))
                {
                    if (NFD_Init() == NFD_OKAY)
                    {
                        nfdu8char_t* selectedPath = nullptr;

                        const nfdu8filteritem_t filters[] = {
                            { "WAV Files", "wav" }
                        };

                        const nfdresult_t result =
                            NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);

                        if (result == NFD_OKAY && selectedPath)
                        {
                            CustomMusic::SetNightMusicPath(
                                static_cast<CustomMusic::DayRegion>(i),
                                selectedPath
                            );

                            NFD_FreePathU8(selectedPath);
                        }

                        NFD_Quit();
                    }
                }

                const char* selectedWav =
                    CustomMusic::GetNightMusicPath(
                        static_cast<CustomMusic::DayRegion>(i)
                    );

                if (selectedWav && *selectedWav)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        ImVec4(0.40f, 1.0f, 0.65f, 1.0f),
                        "WAV Selected"
                    );
                    ImGui::TextWrapped("%s", selectedWav);
                }
                else
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Default music");
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            ImGui::EndChild();
            // END AP_NIGHT_BROWSE_UI_V1
        ImGui::EndTabItem();
        }
    ImGui::EndTabBar();
    }
    // END AP_DAY_NIGHT_TABS_V1
ImGui::EndTabItem();
}
if (ImGui::BeginTabItem("BOSSES"))
                        {
                            // AP_BOSS_MUSIC_V1
                            ImGui::Text("Custom WAV for boss battles.");
                            ImGui::Separator();
                            static const char* bossNames[] = {
                                "Boss Day", "Boss Night", "Boss Night - Eggmanland", "Final Boss - Air Phase"
                            };
                            ImGui::BeginChild("##APBossMusicList", ImVec2(0.0f, 520.0f), true);
                            for (int i = 0; i < 4; ++i)
                            {
                                ImGui::PushID(i);
                                ImGui::Text("%s", bossNames[i]);
                                if (ImGui::Button("Browse WAV...", ImVec2(220.0f, 40.0f)))
                                {
                                    if (NFD_Init() == NFD_OKAY)
                                    {
                                        nfdu8char_t* selectedPath = nullptr;
                                        const nfdu8filteritem_t filters[] = { { "WAV Files", "wav" } };
                                        const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);
                                        if (result == NFD_OKAY && selectedPath)
                                        {
                                            CustomMusic::SetBossMusicPath(static_cast<CustomMusic::BossTrack>(i), selectedPath);
                                            NFD_FreePathU8(selectedPath);
                                        }
                                        NFD_Quit();
                                    }
                                }
                                const char* selectedWav = CustomMusic::GetBossMusicPath(static_cast<CustomMusic::BossTrack>(i));
                                if (selectedWav && *selectedWav)
                                {
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.65f, 1.0f), "WAV Selected");
                                    ImGui::TextWrapped("%s", selectedWav);
                                }
                                else
                                {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("Default music");
                                }
                                ImGui::Separator();
                                ImGui::PopID();
                            }
                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }

                        if (ImGui::BeginTabItem("TOWNS"))
                        {
                            // AP_TOWN_BROWSE_UI_V2
                            ImGui::Text("Custom WAV for Day and Night towns.");
                            ImGui::Separator();
                            static const char* townNames[] = {
                                "Apotos - Day", "Apotos - Night",
                                "Spagonia - Day", "Spagonia - Night",
                                "Mazuri - Day", "Mazuri - Night",
                                "Chun-nan - Day", "Chun-nan - Night",
                                "Holoska - Day", "Holoska - Night",
                                "Adabat - Day", "Adabat - Night",
                                "Shamar - Day", "Shamar - Night",
                                "Empire City - Day", "Empire City - Night",
                                "Eggmanland - Day"
                            };
                            ImGui::BeginChild("##APTownMusicList", ImVec2(0.0f, 520.0f), true);
                            for (int i = 0; i < 17; ++i)
                            {
                                ImGui::PushID(i);
                                ImGui::Text("%s", townNames[i]);
                                if (ImGui::Button("Browse WAV...", ImVec2(220.0f, 40.0f)))
                                {
                                    if (NFD_Init() == NFD_OKAY)
                                    {
                                        nfdu8char_t* selectedPath = nullptr;
                                        const nfdu8filteritem_t filters[] = { { "WAV Files", "wav" } };
                                        const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);
                                        if (result == NFD_OKAY && selectedPath)
                                        {
                                            CustomMusic::SetTownMusicPath(static_cast<CustomMusic::TownTrack>(i), selectedPath);
                                            NFD_FreePathU8(selectedPath);
                                        }
                                        NFD_Quit();
                                    }
                                }
                                const char* selectedWav = CustomMusic::GetTownMusicPath(static_cast<CustomMusic::TownTrack>(i));
                                if (selectedWav && *selectedWav)
                                {
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.65f, 1.0f), "WAV Selected");
                                    ImGui::TextWrapped("%s", selectedWav);
                                }
                                else
                                {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("Default music");
                                }
                                ImGui::Separator();
                                ImGui::PopID();
                            }
                            ImGui::EndChild();
                            // END AP_TOWN_BROWSE_UI_V2
                            ImGui::EndTabItem();
                        }

// AP_LOOP_EDITOR_WAVEFORM_V1
                        if (ImGui::BeginTabItem("LOOP EDITOR"))
                        {
                            static std::string loopEditorPath;
                            static std::vector<float> loopEditorPeaks;
                            static std::int64_t loopEditorDurationMs = 0;
                            static std::int64_t loopEditorStartMs = 0;
                            static std::int64_t loopEditorEndMs = 0;
                            static bool loopEditorEnabled = true;
                            static float loopEditorZoom = 1.0f;
                            static float loopEditorOffset = 0.0f;
                            static int loopEditorDrag = 0;
                            static std::string loopEditorMessage;
                            // AP_AUDIO_PREVIEW_V1: isolated SDL audio device; never uses the game's Mix_Music channel.
                            static SDL_AudioDeviceID previewDevice = 0;
                            static Uint8* previewWavData = nullptr;
                            static Uint32 previewWavBytes = 0;
                            static SDL_AudioSpec previewSpec{};
                            static Uint64 previewCursor = 0; // next byte to queue
                            static bool previewPlaying = false;
                            static bool previewPaused = false;
                            static bool previewTestLoop = false;
                            auto previewStop = [&]() {
                                if (previewDevice) { SDL_ClearQueuedAudio(previewDevice); SDL_CloseAudioDevice(previewDevice); previewDevice = 0; }
                                if (previewWavData) { SDL_FreeWAV(previewWavData); previewWavData = nullptr; }
                                previewWavBytes = 0;
                                previewCursor = 0;
                                previewPlaying = false;
                                previewPaused = false;
                                previewTestLoop = false;
                            };
                            auto previewBytesPerSecond = [&]() -> Uint64 {
                                return static_cast<Uint64>(previewSpec.freq) * previewSpec.channels * (SDL_AUDIO_BITSIZE(previewSpec.format) / 8);
                            };
                            auto previewSeek = [&](std::int64_t ms) {
                                if (!previewDevice || !previewWavData) return;
                                const Uint64 frameSize = static_cast<Uint64>(previewSpec.channels) * (SDL_AUDIO_BITSIZE(previewSpec.format) / 8);
                                const Uint64 bytePos = static_cast<Uint64>(std::max<std::int64_t>(0, ms)) * previewBytesPerSecond() / 1000;
                                previewCursor = std::min<Uint64>(previewWavBytes, frameSize ? bytePos / frameSize * frameSize : 0);
                                SDL_ClearQueuedAudio(previewDevice);
                            };
                            if (ImGui::Button("Open WAV...", ImVec2(180.0f, 38.0f)))
                            {
                                if (NFD_Init() == NFD_OKAY)
                                {
                                    nfdu8char_t* selectedPath = nullptr;
                                    const nfdu8filteritem_t filters[] = {{"WAV Files", "wav"}};
                                    const nfdresult_t result = NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);
                                    if (result == NFD_OKAY && selectedPath)
                                    {
                                        previewStop();
                                        loopEditorPath = selectedPath;
                                        NFD_FreePathU8(selectedPath);
                                        loopEditorPeaks.clear();
                                        loopEditorDurationMs = 0;
                                        loopEditorZoom = 1.0f;
                                        loopEditorOffset = 0.0f;
                                        loopEditorMessage.clear();
                                        SDL_AudioSpec wavSpec{};
                                        Uint8* wavData = nullptr;
                                        Uint32 wavBytes = 0;
                                        if (SDL_LoadWAV(loopEditorPath.c_str(), &wavSpec, &wavData, &wavBytes))
                                        {
                                            const int bits = SDL_AUDIO_BITSIZE(wavSpec.format);
                                            const int bytesPerSample = bits / 8;
                                            const int frameBytes = bytesPerSample * wavSpec.channels;
                                            const std::size_t frames = frameBytes > 0 ? wavBytes / frameBytes : 0;
                                            if (wavSpec.freq > 0 && frames > 0 && (bits == 8 || bits == 16 || bits == 32) &&
                                                (!SDL_AUDIO_ISFLOAT(wavSpec.format) || bits == 32))
                                            {
                                                loopEditorDurationMs = static_cast<std::int64_t>(frames * 1000ULL / static_cast<unsigned>(wavSpec.freq));
                                                const std::size_t bins = 16000;
                                                loopEditorPeaks.assign(bins, 0.0f);
                                                for (std::size_t bin = 0; bin < bins; ++bin)
                                                {
                                                    const std::size_t first = bin * frames / bins;
                                                    const std::size_t last = (bin + 1) * frames / bins;
                                                    float peak = 0.0f;
                                                    const std::size_t stride = std::max<std::size_t>(1, (last - first) / 32);
                                                    for (std::size_t f = first; f < last; f += stride)
                                                    {
                                                        for (int ch = 0; ch < wavSpec.channels; ++ch)
                                                        {
                                                            const Uint8* p = wavData + f * frameBytes + ch * bytesPerSample;
                                                            float sample = 0.0f;
                                                            if (bits == 8)
                                                                sample = SDL_AUDIO_ISSIGNED(wavSpec.format) ? std::abs(static_cast<float>(static_cast<std::int8_t>(*p)) / 128.0f) : std::abs((static_cast<float>(*p) - 128.0f) / 128.0f);
                                                            else if (bits == 16)
                                                            {
                                                                std::uint16_t v = SDL_AUDIO_ISBIGENDIAN(wavSpec.format) ? (static_cast<std::uint16_t>(p[0]) << 8 | p[1]) : (static_cast<std::uint16_t>(p[1]) << 8 | p[0]);
                                                                sample = std::abs(static_cast<float>(static_cast<std::int16_t>(v)) / 32768.0f);
                                                            }
                                                            else if (SDL_AUDIO_ISFLOAT(wavSpec.format))
                                                            {
                                                                float v = 0.0f;
                                                                std::memcpy(&v, p, sizeof(v));
                                                                sample = std::isfinite(v) ? std::min(1.0f, std::abs(v)) : 0.0f;
                                                            }
                                                            else
                                                            {
                                                                std::uint32_t v = 0;
                                                                std::memcpy(&v, p, sizeof(v));
                                                                sample = SDL_AUDIO_ISSIGNED(wavSpec.format) ? std::abs(static_cast<double>(static_cast<std::int32_t>(v)) / 2147483648.0) : std::abs((static_cast<double>(v) - 2147483648.0) / 2147483648.0);
                                                            }
                                                            peak = std::max(peak, sample);
                                                        }
                                                    }
                                                    loopEditorPeaks[bin] = std::min(1.0f, peak);
                                                }
                                                loopEditorStartMs = 0;
                                                loopEditorEndMs = loopEditorDurationMs;
                                                loopEditorEnabled = true;
                                                CustomMusic::LoopPoints saved{};
                                                if (CustomMusic::GetSavedLoop(loopEditorPath.c_str(), saved))
                                                {
                                                    loopEditorStartMs = std::clamp(saved.startMs, std::int64_t(0), loopEditorDurationMs);
                                                    loopEditorEndMs = std::clamp(saved.endMs, std::int64_t(0), loopEditorDurationMs);
                                                    loopEditorEnabled = saved.enabled;
                                                    loopEditorMessage = "Saved markers loaded.";
                                                }
                                            }
                                            else loopEditorMessage = "Unsupported WAV sample format.";
                                            SDL_FreeWAV(wavData);
                                        }
                                        else loopEditorMessage = "Could not open WAV file.";
                                    }
                                    NFD_Quit();
                                }
                            }
                            if (!loopEditorPath.empty()) ImGui::TextWrapped("%s", loopEditorPath.c_str());
                            if (!loopEditorPeaks.empty() && loopEditorDurationMs > 0)
                            {
                                // Separate preview device: SDL_mixer/gameplay music is not interrupted.
                                if (previewPlaying && previewDevice && !previewPaused)
                                {
                                    const Uint64 bytesPerSecond = previewBytesPerSecond();
                                    const Uint64 frameBytes = static_cast<Uint64>(previewSpec.channels) * (SDL_AUDIO_BITSIZE(previewSpec.format) / 8);
                                    const Uint64 queued = SDL_GetQueuedAudioSize(previewDevice);
                                    const Uint64 targetQueue = bytesPerSecond / 8; // ~125 ms, low latency
                                    if (queued < targetQueue && bytesPerSecond && frameBytes)
                                    {
                                        const Uint64 loopStartByte = (static_cast<Uint64>(loopEditorStartMs) * bytesPerSecond / 1000 / frameBytes) * frameBytes;
                                        const Uint64 loopEndByte = (static_cast<Uint64>(loopEditorEndMs) * bytesPerSecond / 1000 / frameBytes) * frameBytes;
                                        const Uint64 boundary = previewTestLoop && loopEndByte > loopStartByte ? std::min<Uint64>(loopEndByte, previewWavBytes) : previewWavBytes;
                                        if (previewCursor >= boundary)
                                        {
                                            if (previewTestLoop && loopEndByte > loopStartByte) previewCursor = loopStartByte;
                                            else { previewPlaying = false; SDL_PauseAudioDevice(previewDevice, 1); }
                                        }
                                        if (previewPlaying && previewCursor < boundary)
                                        {
                                            const Uint64 count = std::min<Uint64>(boundary - previewCursor, targetQueue - queued);
                                            const Uint64 aligned = count / frameBytes * frameBytes;
                                            if (aligned && SDL_QueueAudio(previewDevice, previewWavData + previewCursor, static_cast<Uint32>(aligned)) == 0)
                                                previewCursor += aligned;
                                        }
                                    }
                                }
                                const Uint64 previewQueued = previewDevice ? SDL_GetQueuedAudioSize(previewDevice) : 0;
                                const Uint64 previewBps = previewBytesPerSecond();
                                const Uint64 playedBytes = previewCursor > previewQueued ? previewCursor - previewQueued : 0;
                                const std::int64_t previewPositionMs = previewBps ? static_cast<std::int64_t>(playedBytes * 1000 / previewBps) : 0;
                                ImGui::Text("Duration: %.3f seconds", loopEditorDurationMs / 1000.0);
                                ImGui::SliderFloat("Zoom", &loopEditorZoom, 1.0f, 100.0f, "%.1fx", ImGuiSliderFlags_Logarithmic);
                                const float visibleFraction = 1.0f / loopEditorZoom;
                                loopEditorOffset = std::clamp(loopEditorOffset, 0.0f, 1.0f - visibleFraction);
                                ImGui::SliderFloat("Scroll", &loopEditorOffset, 0.0f, 1.0f - visibleFraction, "%.4f");
                                const ImVec2 canvasSize(std::max(250.0f, ImGui::GetContentRegionAvail().x), 220.0f);
                                const ImVec2 origin = ImGui::GetCursorScreenPos();
                                ImGui::InvisibleButton("##LoopWaveform", canvasSize);
                                const bool hovered = ImGui::IsItemHovered();
                                ImDrawList* draw = ImGui::GetWindowDrawList();
                                const ImVec2 end(origin.x + canvasSize.x, origin.y + canvasSize.y);
                                draw->AddRectFilled(origin, end, IM_COL32(14, 20, 32, 255));
                                const float center = origin.y + canvasSize.y * 0.5f;
                                draw->AddLine(ImVec2(origin.x, center), ImVec2(end.x, center), IM_COL32(90, 105, 130, 255));
                                const int width = static_cast<int>(canvasSize.x);
                                for (int x = 0; x < width; ++x)
                                {
                                    const float fraction = loopEditorOffset + (static_cast<float>(x) / canvasSize.x) * visibleFraction;
                                    const std::size_t index = std::min(loopEditorPeaks.size() - 1, static_cast<std::size_t>(fraction * loopEditorPeaks.size()));
                                    const float amplitude = loopEditorPeaks[index] * (canvasSize.y * 0.44f);
                                    draw->AddLine(ImVec2(origin.x + x, center - amplitude), ImVec2(origin.x + x, center + amplitude), IM_COL32(75, 190, 235, 255));
                                }
                                auto markerX = [&](std::int64_t ms) { return origin.x + (static_cast<float>(ms) / loopEditorDurationMs - loopEditorOffset) / visibleFraction * canvasSize.x; };
                                const float startX = markerX(loopEditorStartMs);
                                const float endX = markerX(loopEditorEndMs);
                                if (startX >= origin.x && startX <= end.x) draw->AddLine(ImVec2(startX, origin.y), ImVec2(startX, end.y), IM_COL32(70, 255, 120, 255), 3.0f);
                                if (endX >= origin.x && endX <= end.x) draw->AddLine(ImVec2(endX, origin.y), ImVec2(endX, end.y), IM_COL32(255, 125, 80, 255), 3.0f);
                                if (previewDevice)
                                {
                                    const float playX = markerX(previewPositionMs);
                                    if (playX >= origin.x && playX <= end.x)
                                    {
                                        draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, end.y), IM_COL32(255, 255, 255, 255), 2.0f);
                                        draw->AddCircleFilled(ImVec2(playX, origin.y + 7.0f), 5.0f, IM_COL32(255, 255, 255, 255));
                                    }
                                }
                                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                                {
                                    const float mouseX = ImGui::GetIO().MousePos.x;
                                    loopEditorDrag = std::abs(mouseX - startX) <= std::abs(mouseX - endX) ? 1 : 2;
                                }
                                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) loopEditorDrag = 0;
                                if (loopEditorDrag != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                                {
                                    const float fraction = std::clamp(loopEditorOffset + (ImGui::GetIO().MousePos.x - origin.x) / canvasSize.x * visibleFraction, 0.0f, 1.0f);
                                    const std::int64_t ms = static_cast<std::int64_t>(fraction * loopEditorDurationMs);
                                    if (loopEditorDrag == 1) loopEditorStartMs = std::min(ms, loopEditorEndMs);
                                    else loopEditorEndMs = std::max(ms, loopEditorStartMs);
                                }
                                // Right-click the waveform to seek without moving loop markers.
                                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && previewDevice)
                                {
                                    const float fraction = std::clamp(loopEditorOffset + (ImGui::GetIO().MousePos.x - origin.x) / canvasSize.x * visibleFraction, 0.0f, 1.0f);
                                    previewSeek(static_cast<std::int64_t>(fraction * loopEditorDurationMs));
                                }
                                ImGui::Text("Preview: %02lld:%06.3f / %02lld:%06.3f", static_cast<long long>(previewPositionMs / 60000), (previewPositionMs % 60000) / 1000.0, static_cast<long long>(loopEditorDurationMs / 60000), (loopEditorDurationMs % 60000) / 1000.0);
                                if (ImGui::Button(previewPlaying && !previewPaused ? "PAUSE" : "PLAY", ImVec2(100.0f, 34.0f)))
                                {
                                    if (!previewDevice)
                                    {
                                        SDL_AudioSpec desired{};
                                        if (SDL_LoadWAV(loopEditorPath.c_str(), &desired, &previewWavData, &previewWavBytes))
                                        {
                                            previewSpec = desired;
                                            previewSpec.callback = nullptr;
                                            previewDevice = SDL_OpenAudioDevice(nullptr, 0, &previewSpec, nullptr, 0);
                                            if (!previewDevice) { loopEditorMessage = std::string("Preview device error: ") + SDL_GetError(); previewStop(); }
                                        }
                                        else loopEditorMessage = std::string("Preview WAV error: ") + SDL_GetError();
                                    }
                                    if (previewDevice)
                                    {
                                        if (!previewPlaying && previewCursor >= previewWavBytes) previewSeek(0);
                                        previewPlaying = true;
                                        previewPaused = !previewPaused && SDL_GetQueuedAudioSize(previewDevice) > 0 ? true : false;
                                        SDL_PauseAudioDevice(previewDevice, previewPaused ? 1 : 0);
                                    }
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("STOP", ImVec2(100.0f, 34.0f))) previewStop();
                                ImGui::SameLine();
                                if (ImGui::Button(previewTestLoop ? "TEST LOOP: ON" : "TEST LOOP", ImVec2(150.0f, 34.0f)))
                                {
                                    previewTestLoop = !previewTestLoop;
                                    if (previewTestLoop && previewDevice) previewSeek(std::max<std::int64_t>(loopEditorStartMs, loopEditorEndMs - 2500));
                                }
                                if (ImGui::Button("SET START HERE")) loopEditorStartMs = std::clamp(previewPositionMs, std::int64_t(0), loopEditorEndMs);
                                ImGui::SameLine();
                                if (ImGui::Button("SET END HERE")) loopEditorEndMs = std::clamp(previewPositionMs, loopEditorStartMs, loopEditorDurationMs);
                                ImGui::TextDisabled("Right-click waveform to seek. Test Loop repeats between green and orange markers.");
                                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "GREEN = Loop Start");
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "ORANGE = Loop End");
                                ImGui::TextDisabled("Click or drag the nearest marker. Zoom and scroll for precision.");
                                double startSeconds = loopEditorStartMs / 1000.0;
                                double endSeconds = loopEditorEndMs / 1000.0;
                                if (ImGui::InputDouble("Loop Start (seconds)", &startSeconds, 0.001, 0.1, "%.3f"))
                                    loopEditorStartMs = std::clamp(static_cast<std::int64_t>(startSeconds * 1000.0), std::int64_t(0), loopEditorEndMs);
                                if (ImGui::InputDouble("Loop End (seconds)", &endSeconds, 0.001, 0.1, "%.3f"))
                                    loopEditorEndMs = std::clamp(static_cast<std::int64_t>(endSeconds * 1000.0), loopEditorStartMs, loopEditorDurationMs);
                                ImGui::Checkbox("Enable this loop", &loopEditorEnabled);
                                if (ImGui::Button("SAVE LOOP", ImVec2(200.0f, 42.0f)))
                                {
                                    if (loopEditorEndMs > loopEditorStartMs && CustomMusic::SaveLoop(loopEditorPath.c_str(), loopEditorStartMs, loopEditorEndMs, loopEditorEnabled))
                                        loopEditorMessage = "Loop saved to CustomMusic/loops.ini!";
                                    else loopEditorMessage = "Save failed: check marker positions and file permissions.";
                                }
                                ImGui::TextDisabled("Audio preview only. Applying saved loops to gameplay music comes next.");
                            }
                            if (!loopEditorMessage.empty()) ImGui::TextWrapped("%s", loopEditorMessage.c_str());
                            ImGui::EndTabItem();
                        }
                        // END AP_LOOP_EDITOR_WAVEFORM_V1

                        ImGui::EndTabBar();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Loop Editor: waveform, audio preview and persistent markers. Gameplay looping comes next.");
                }

                ImGui::End();
                ImGui::PopStyleVar();
            }

            ImGui::Spacing();

            // AP Bug Report - bouton metallique.
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            static std::string apBugReportPath;

            if (ImGui::Button("BUG REPORT", ImVec2(-1, 38)))
            {
                apBugReportPath = os::logger::ExportBugReport();
                ImGui::OpenPopup("AP Bug Report");
            }

            // Meme finition metallique que SAVE SETTINGS.
            {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 s = ImGui::GetItemRectSize();
                ImVec2 bottom(p.x + s.x, p.y + s.y);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                draw->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 5.0f, p.y + s.y * 0.38f),
                    IM_COL32(180, 225, 255, 55),
                    10.0f
                );

                draw->AddRect(
                    ImVec2(p.x + 1.0f, p.y + 1.0f),
                    ImVec2(bottom.x - 1.0f, bottom.y - 1.0f),
                    IM_COL32(160, 215, 245, 235),
                    13.0f, 0, 2.5f
                );

                draw->AddRect(
                    ImVec2(p.x + 4.0f, p.y + 4.0f),
                    ImVec2(bottom.x - 4.0f, bottom.y - 4.0f),
                    IM_COL32(15, 55, 105, 210),
                    10.0f, 0, 1.5f
                );

                draw->AddLine(
                    ImVec2(p.x + 17.0f, p.y + 2.0f),
                    ImVec2(bottom.x - 17.0f, p.y + 2.0f),
                    IM_COL32(225, 245, 255, 170),
                    1.5f
                );
            }

            if (ImGui::BeginPopupModal(
                "AP Bug Report", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (!apBugReportPath.empty())
                {
                    ImGui::TextColored(
                        ImVec4(0.30f, 0.95f, 0.55f, 1.0f),
                        "BUG REPORT CREATED!");

                    ImGui::Spacing();
                    ImGui::TextWrapped("Log saved to:");
                    ImGui::TextWrapped("%s", apBugReportPath.c_str());

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextWrapped(
                        "Please send this log file to Zakary on Discord.");
                }
                else
                {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                        "Failed to create bug report.");

                    ImGui::TextWrapped(
                        "Please check the game folder permissions.");
                }

                if (ImGui::Button("OK", ImVec2(180, 38)))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }
            // AP SPECIAL THANKS - Credits dans une fenetre independante.
            if (ImGui::Button("SPECIAL THANKS", ImVec2(-1, 42)))
                ImGui::OpenPopup("AP Special Thanks");

            // AP SPECIAL THANKS - Bouton metallique et contour arc-en-ciel anime.
            {
                const ImVec2 p = ImGui::GetItemRectMin();
                const ImVec2 q = ImGui::GetItemRectMax();
                const float w = q.x - p.x;
                const float h = q.y - p.y;
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const float t = static_cast<float>(ImGui::GetTime());
                const bool hovered = ImGui::IsItemHovered();
                const float pulse = 0.5f + 0.5f * sinf(t * 2.4f);
                const float brightness = (hovered ? 1.0f : 0.65f) + 0.18f * pulse;

                // Reflet metallique discret, comme les autres boutons F8.
                draw->AddRectFilled(
                    ImVec2(p.x + 5.0f, p.y + 4.0f),
                    ImVec2(q.x - 5.0f, p.y + h * 0.39f),
                    IM_COL32(185, 225, 255, 52), 9.0f
                );
                draw->AddRect(
                    ImVec2(p.x + 3.0f, p.y + 3.0f),
                    ImVec2(q.x - 3.0f, q.y - 3.0f),
                    IM_COL32(175, 205, 235, 130), 8.0f, 0, 1.0f
                );

                // LiserÃ© arc-en-ciel mobile sur les quatre cotes du bouton.
                // Un halo doux et un trait net restent a l'interieur du rectangle.
                const float inset = 1.5f;
                const float left = p.x + inset, right = q.x - inset;
                const float top = p.y + inset, bottom = q.y - inset;
                const float iw = right - left, ih = bottom - top;
                const float perimeter = 2.0f * (iw + ih);
                const int segments = 120;
                for (int i = 0; i < segments; ++i)
                {
                    const float d0 = perimeter * i / segments;
                    const float d1 = perimeter * (i + 1) / segments;
                    auto pointOnEdge = [&](float d) -> ImVec2
                    {
                        if (d <= iw) return ImVec2(left + d, top);
                        d -= iw;
                        if (d <= ih) return ImVec2(right, top + d);
                        d -= ih;
                        if (d <= iw) return ImVec2(right - d, bottom);
                        d -= iw;
                        return ImVec2(left, bottom - d);
                    };
                    const ImVec2 a = pointOnEdge(d0);
                    const ImVec2 b = pointOnEdge(d1);
                    float hue = (static_cast<float>(i) / segments) - t * 0.09f;
                    hue -= floorf(hue);
                    const ImVec4 rgb = ImColor::HSV(hue, 0.85f, 1.0f);
                    const ImU32 glow = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(rgb.x, rgb.y, rgb.z, 0.13f * brightness));
                    const ImU32 rim = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(rgb.x, rgb.y, rgb.z, 0.88f * brightness));
                    draw->AddLine(a, b, glow, 5.0f);
                    draw->AddLine(a, b, rim, 2.0f);
                }
            }

            ImGui::SetNextWindowSize(ImVec2(510.0f, 650.0f), ImGuiCond_FirstUseEver);
            if (ImGui::BeginPopupModal("AP Special Thanks", nullptr, ImGuiWindowFlags_NoCollapse))
            {
                ImGui::TextColored(ImVec4(0.40f, 0.82f, 1.0f, 1.0f), "SPECIAL THANKS");
                ImGui::TextWrapped("Thank you to everyone who supported this project!");
                ImGui::Separator();
                ImGui::BeginChild("##APThanksNames", ImVec2(0.0f, -58.0f), true);
                ImGui::TextUnformatted("absoluteSpacehead");
                ImGui::TextUnformatted("Adly66");
                ImGui::TextUnformatted("Astreachan");
                ImGui::TextUnformatted("Azzy");
                ImGui::TextUnformatted("ChrisCTRD");
                ImGui::TextUnformatted("Cutless");
                ImGui::TextUnformatted("DonGT");
                ImGui::TextUnformatted("Enerjak");
                ImGui::TextUnformatted("Flocon");
                ImGui::TextUnformatted("girovan1");
                ImGui::TextUnformatted("HothRaka");
                ImGui::TextUnformatted("Hubster");
                ImGui::TextUnformatted("HumMation");
                ImGui::TextUnformatted("JEJE");
                ImGui::TextUnformatted("Kalib_32");
                ImGui::TextUnformatted("Kerolune");
                ImGui::TextUnformatted("Kombos22");
                ImGui::TextUnformatted("Lara the Lazy Prodigy (Harry)");
                ImGui::TextUnformatted("Robin");
                ImGui::TextUnformatted("Scrungip");
                ImGui::TextUnformatted("Shadyrix");
                ImGui::TextUnformatted("SimplyTJ");
                ImGui::TextUnformatted("Sonicboy90");
                ImGui::TextUnformatted("Stella Rorae");
                ImGui::TextUnformatted("strugen");
                ImGui::TextUnformatted("TwinklePillar");
                ImGui::TextUnformatted("Valyssa");
                ImGui::TextUnformatted("Vaporeon");
                ImGui::EndChild();
                if (ImGui::Button("CLOSE", ImVec2(-1, 40)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Spacing();

            // AP_F8_REORDER_V1_DONE
            ImGui::TextDisabled("F8: Show / Hide");
        }

        ImGui::End();
        ImGui::PopFont();

        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar(6);
    }
    AchievementMenu::Draw();
    OptionsMenu::Draw();
    AchievementOverlay::Draw();
    InstallerWizard::Draw();
    MessageWindow::Draw();
    ButtonGuide::Draw();
    Fader::Draw();
    BlackBar::Draw();

    assert(ImGui::GetBackgroundDrawList()->_ClipRectStack.Size == 1 && "Some clip rects were not removed from the stack!");

    APMedalRadarAudio::Tick(); // AP_MEDAL_RADAR_AUDIO_V1
    DrawAPMedalsHud();
    APMultiplayerOverlay::DrawOverlay(); // AP_MULTIWORLD_HUD_V082_RENDER
    DrawFPS();
    DrawProfiler();
    ImGui::Render();

    auto drawData = ImGui::GetDrawData();
    if (drawData->CmdListsCount != 0)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::DrawImGui;
        g_renderQueue.enqueue(cmd);
    }
}

static void SetFramebuffer(GuestSurface *renderTarget, GuestSurface *depthStencil, bool settingForClear);

static void ProcDrawImGui(const RenderCommand& cmd)
{
    // Make sure the backbuffer is the current target.
    AddBarrier(g_backBuffer, RenderTextureLayout::COLOR_WRITE);
    FlushBarriers();
    SetFramebuffer(g_backBuffer, nullptr, false);

    auto& commandList = g_commandLists[g_frame];
    auto pipeline = g_imPipeline.get();

    commandList->setGraphicsPipelineLayout(g_imPipelineLayout.get());
    commandList->setPipeline(pipeline);
    commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
    commandList->setGraphicsDescriptorSet(g_samplerDescriptorSet.get(), 1);

    auto& drawData = *ImGui::GetDrawData();
    commandList->setViewports(RenderViewport(drawData.DisplayPos.x, drawData.DisplayPos.y, drawData.DisplaySize.x, drawData.DisplaySize.y));

    ImGuiPushConstants pushConstants{};
    pushConstants.displaySize = drawData.DisplaySize;
    pushConstants.inverseDisplaySize = { 1.0f / drawData.DisplaySize.x, 1.0f / drawData.DisplaySize.y };
    commandList->setGraphicsPushConstants(0, &pushConstants);

    size_t pushConstantRangeMin = ~0;
    size_t pushConstantRangeMax = 0;

    auto setPushConstants = [&](void* destination, const void* source, size_t size)
        {
            bool dirty = memcmp(destination, source, size) != 0;

            memcpy(destination, source, size);

            if (dirty)
            {
                size_t offset = reinterpret_cast<size_t>(destination) - reinterpret_cast<size_t>(&pushConstants);
                pushConstantRangeMin = std::min(pushConstantRangeMin, offset);
                pushConstantRangeMax = std::max(pushConstantRangeMax, offset + size);
            }
        };

    ImRect clipRect{};

    for (int i = 0; i < drawData.CmdListsCount; i++)
    {
        auto& drawList = drawData.CmdLists[i];

        auto vertexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert), alignof(ImDrawVert));
        auto indexBufferAllocation = g_uploadAllocators[g_frame].allocate<false>(drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(uint16_t), alignof(uint16_t));

        const RenderVertexBufferView vertexBufferView(vertexBufferAllocation.buffer->at(vertexBufferAllocation.offset), drawList->VtxBuffer.Size * sizeof(ImDrawVert));
        const RenderInputSlot inputSlot(0, sizeof(ImDrawVert));
        commandList->setVertexBuffers(0, &vertexBufferView, 1, &inputSlot);

        const RenderIndexBufferView indexBufferView(indexBufferAllocation.buffer->at(indexBufferAllocation.offset), drawList->IdxBuffer.Size * sizeof(uint16_t), RenderFormat::R16_UINT);
        commandList->setIndexBuffer(&indexBufferView);

        for (int j = 0; j < drawList->CmdBuffer.Size; j++)
        {
            auto& drawCmd = drawList->CmdBuffer[j];
            if (drawCmd.UserCallback != nullptr)
            {
                auto callbackData = reinterpret_cast<const ImGuiCallbackData*>(drawCmd.UserCallbackData);

                switch (static_cast<ImGuiCallback>(reinterpret_cast<size_t>(drawCmd.UserCallback)))
                {
                case ImGuiCallback::SetGradient:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setGradient, sizeof(callbackData->setGradient));
                    break;       
                case ImGuiCallback::SetShaderModifier:
                    setPushConstants(&pushConstants.shaderModifier, &callbackData->setShaderModifier, sizeof(callbackData->setShaderModifier));
                    break;
                case ImGuiCallback::SetOrigin:
                    setPushConstants(&pushConstants.origin, &callbackData->setOrigin, sizeof(callbackData->setOrigin));
                    break;
                case ImGuiCallback::SetScale:
                    setPushConstants(&pushConstants.scale, &callbackData->setScale, sizeof(callbackData->setScale));
                    break;       
                case ImGuiCallback::SetMarqueeFade:
                    setPushConstants(&pushConstants.boundsMin, &callbackData->setMarqueeFade, sizeof(callbackData->setMarqueeFade));
                    break;
                case ImGuiCallback::SetOutline:
                    setPushConstants(&pushConstants.outline, &callbackData->setOutline, sizeof(callbackData->setOutline));
                    break;
                case ImGuiCallback::SetProceduralOrigin:
                    setPushConstants(&pushConstants.proceduralOrigin, &callbackData->setProceduralOrigin, sizeof(callbackData->setProceduralOrigin));
                    break;
                case ImGuiCallback::SetAdditive:
                {
                    auto pipelineToSet = callbackData->setAdditive.enabled ? g_imAdditivePipeline.get() : g_imPipeline.get();
                    if (pipeline != pipelineToSet)
                    {
                        commandList->setPipeline(pipelineToSet);
                        pipeline = pipelineToSet;
                    }
                    break;
                }
                default:
                    assert(false && "Unknown ImGui callback type.");
                    break;
                }
            }
            else
            {
                if (drawCmd.ClipRect.z <= drawCmd.ClipRect.x || drawCmd.ClipRect.w <= drawCmd.ClipRect.y)
                    continue;

                auto texture = reinterpret_cast<GuestTexture*>(drawCmd.TextureId);
                uint32_t descriptorIndex = TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D;
                if (texture != nullptr)
                {
                    if (texture->layout != RenderTextureLayout::SHADER_READ)
                    {
                        commandList->barriers(RenderBarrierStage::GRAPHICS | RenderBarrierStage::COPY,
                            RenderTextureBarrier(texture->texture, RenderTextureLayout::SHADER_READ));

                        texture->layout = RenderTextureLayout::SHADER_READ;
                    }

                    descriptorIndex = texture->descriptorIndex;

                    if (texture == g_imFontTexture.get())
                        descriptorIndex |= 0x80000000;

                    setPushConstants(&pushConstants.texture2DDescriptorIndex, &descriptorIndex, sizeof(descriptorIndex));
                }

                if (pushConstantRangeMin < pushConstantRangeMax)
                {
                    commandList->setGraphicsPushConstants(0, reinterpret_cast<const uint8_t*>(&pushConstants) + pushConstantRangeMin, pushConstantRangeMin, pushConstantRangeMax - pushConstantRangeMin);
                    pushConstantRangeMin = ~0;
                    pushConstantRangeMax = 0;
                }

                if (memcmp(&clipRect, &drawCmd.ClipRect, sizeof(clipRect)) != 0)
                {
                    commandList->setScissors(RenderRect(int32_t(drawCmd.ClipRect.x), int32_t(drawCmd.ClipRect.y), int32_t(drawCmd.ClipRect.z), int32_t(drawCmd.ClipRect.w)));
                    clipRect = drawCmd.ClipRect;
                }

                commandList->drawIndexedInstanced(drawCmd.ElemCount, 1, drawCmd.IdxOffset, drawCmd.VtxOffset, 0);
            }
        }
    }
}

// We have to check for this to properly handle the following situation:
// 1. Wait on swap chain.
// 2. Create loading thread.
// 3. Loading thread also waits on swap chain.
// 4. Loading thread presents and quits.
// 5. After the loading thread quits, application also presents.
static bool g_pendingWaitOnSwapChain = true;

void Video::WaitOnSwapChain()
{
    if (g_pendingWaitOnSwapChain)
    {
        if (g_swapChainValid)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait();
            g_presentWaitProfiler.End();
        }

        g_pendingWaitOnSwapChain = false;
    }
}

static bool g_shouldPrecompilePipelines;
static std::atomic<bool> g_executedCommandList;

void Video::Present() 
{
    g_readyForCommands = false;

    RenderCommand cmd;
    cmd.type = RenderCommandType::ExecutePendingStretchRectCommands;
    g_renderQueue.enqueue(cmd);

    DrawImGui();

    cmd.type = RenderCommandType::ExecuteCommandList;
    g_renderQueue.enqueue(cmd);

    // All the shaders are available at this point. We can precompile embedded PSOs then.
    if (g_shouldPrecompilePipelines)
    {
        EnqueuePipelineTask(PipelineTaskType::PrecompilePipelines, {});
        g_shouldPrecompilePipelines = false;
    }

    g_executedCommandList.wait(false);
    g_executedCommandList = false;

    if (g_swapChainValid)
    {
        if (g_pendingWaitOnSwapChain)
        {
            g_presentWaitProfiler.Begin();
            g_swapChain->wait(); // Never gonna happen outside loading threads as explained above.
            g_presentWaitProfiler.End();
        }

        RenderCommandSemaphore* signalSemaphores[] = { g_renderSemaphores[g_frame].get() };
        g_swapChainValid = g_swapChain->present(g_backBufferIndex, signalSemaphores, std::size(signalSemaphores));
    }

    g_pendingWaitOnSwapChain = true;

    g_frame = g_nextFrame;
    g_nextFrame = (g_frame + 1) % NUM_FRAMES;

    if (g_commandListStates[g_frame])
    {
        g_frameFenceProfiler.Begin();
        g_queue->waitForCommandFence(g_commandFences[g_frame].get());
        g_frameFenceProfiler.End();
        g_commandListStates[g_frame] = false;

        // Update the GPU profiler with the results from the timestamps of the frame.
        g_queryPools[g_frame]->queryResults();
        const uint64_t *frameTimestamps = g_queryPools[g_frame]->getResults();
        g_gpuFrameProfiler.Set(double(frameTimestamps[1] - frameTimestamps[0]) / 1000000.0);
    }

    g_dirtyStates = DirtyStates(true);
    g_uploadAllocators[g_frame].reset();
    g_intermediaryUploadAllocator.reset();
    g_triangleFanIndexData.reset();
    g_quadIndexData.reset();

    CheckSwapChain();

    cmd.type = RenderCommandType::BeginCommandList;
    g_renderQueue.enqueue(cmd);

    if (Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX)
    {
        using namespace std::chrono_literals;

        static std::chrono::steady_clock::time_point s_next;

        auto now = std::chrono::steady_clock::now();

        if (now < s_next)
        {
            std::this_thread::sleep_for(std::chrono::floor<std::chrono::milliseconds>(s_next - now - 2ms));

            while ((now = std::chrono::steady_clock::now()) < s_next)
                std::this_thread::yield();
        }
        else
        {
            s_next = now;
        }

        s_next += 1000000000ns / Config::FPS;
    }

    g_presentProfiler.Reset();
}

void Video::StartPipelinePrecompilation()
{
    g_shouldPrecompilePipelines = true;
}

static void SetRootDescriptor(const UploadAllocation& allocation, size_t index)
{
    auto& commandList = g_commandLists[g_frame];

    if (g_vulkan)
        commandList->setGraphicsPushConstants(0, &allocation.deviceAddress, 8 * index, 8);
    else
        commandList->setGraphicsRootDescriptor(allocation.buffer->at(allocation.offset), index);
}

static void ProcExecuteCommandList(const RenderCommand& cmd)
{    
    if (g_swapChainValid)
    {
        auto swapChainTexture = g_swapChain->getTexture(g_backBufferIndex);
        if (g_backBuffer->texture == g_intermediaryBackBufferTexture.get())
        {
            struct
            {
                float gammaR;
                float gammaG;
                float gammaB;
                uint32_t textureDescriptorIndex;

                int32_t viewportOffsetX;
                int32_t viewportOffsetY;
                int32_t viewportWidth;
                int32_t viewportHeight;
            } constants;

            if (Config::XboxColorCorrection)
            {
                constants.gammaR = 1.2f;
                constants.gammaG = 1.17f;
                constants.gammaB = 0.98f;
            }
            else
            {
                constants.gammaR = 1.0f;
                constants.gammaG = 1.0f;
                constants.gammaB = 1.0f;
            }

            float offset = (Config::Brightness - 0.5f) * 1.2f;

            constants.gammaR = 1.0f / std::clamp(constants.gammaR + offset, 0.1f, 4.0f);
            constants.gammaG = 1.0f / std::clamp(constants.gammaG + offset, 0.1f, 4.0f);
            constants.gammaB = 1.0f / std::clamp(constants.gammaB + offset, 0.1f, 4.0f);
            constants.textureDescriptorIndex = g_intermediaryBackBufferTextureDescriptorIndex;

            constants.viewportOffsetX = (int32_t(g_swapChain->getWidth()) - int32_t(Video::s_viewportWidth)) / 2;
            constants.viewportOffsetY = (int32_t(g_swapChain->getHeight()) - int32_t(Video::s_viewportHeight)) / 2;
            constants.viewportWidth = Video::s_viewportWidth;
            constants.viewportHeight = Video::s_viewportHeight;

            auto &framebuffer = g_backBuffer->framebuffers[swapChainTexture];
            if (!framebuffer)
            {
                RenderFramebufferDesc desc;
                desc.colorAttachments = const_cast<const RenderTexture **>(&swapChainTexture);
                desc.colorAttachmentsCount = 1;
                framebuffer = g_device->createFramebuffer(desc);
            }

            RenderTextureBarrier srcBarriers[] =
            {
                RenderTextureBarrier(g_intermediaryBackBufferTexture.get(), RenderTextureLayout::SHADER_READ),
                RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE)
            };

            auto &commandList = g_commandLists[g_frame];
            commandList->barriers(RenderBarrierStage::GRAPHICS, srcBarriers, std::size(srcBarriers));
            commandList->setGraphicsPipelineLayout(g_pipelineLayout.get());
            commandList->setPipeline(g_gammaCorrectionPipeline.get());
            commandList->setGraphicsDescriptorSet(g_textureDescriptorSet.get(), 0);
            SetRootDescriptor(g_uploadAllocators[g_frame].allocate<false>(&constants, sizeof(constants), 0x100), 2);
            commandList->setFramebuffer(framebuffer.get());
            commandList->setViewports(RenderViewport(0.0f, 0.0f, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->setScissors(RenderRect(0, 0, g_swapChain->getWidth(), g_swapChain->getHeight()));
            commandList->drawInstanced(6, 1, 0, 0);
            commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
        }
        else
        {
            AddBarrier(g_backBuffer, RenderTextureLayout::PRESENT);
            FlushBarriers();
        }
    }

    auto &commandList = g_commandLists[g_frame];
    commandList->writeTimestamp(g_queryPools[g_frame].get(), 1);
    commandList->end();

    if (g_swapChainValid)
    {
        const RenderCommandList *commandLists[] = { commandList.get() };
        RenderCommandSemaphore *waitSemaphores[] = { g_acquireSemaphores[g_frame].get() };
        RenderCommandSemaphore *signalSemaphores[] = { g_renderSemaphores[g_frame].get() };

        g_queue->executeCommandLists(
            commandLists, std::size(commandLists),
            waitSemaphores, std::size(waitSemaphores),
            signalSemaphores, std::size(signalSemaphores),
            g_commandFences[g_frame].get());
    }
    else
    {
        g_queue->executeCommandLists(commandList.get(), g_commandFences[g_frame].get());
    }

    g_commandListStates[g_frame] = true;

    g_executedCommandList = true;
    g_executedCommandList.notify_one();
}

static void ProcBeginCommandList(const RenderCommand& cmd)
{
    DestructTempResources();
    BeginCommandList();
}

static GuestSurface* GetBackBuffer() 
{
    g_backBuffer->AddRef();
    return g_backBuffer;
}

void Video::ComputeViewportDimensions()
{
    uint32_t width = g_swapChain->getWidth();
    uint32_t height = g_swapChain->getHeight();
    float aspectRatio = float(width) / float(height);

    switch (Config::AspectRatio)
    {
    case EAspectRatio::Wide:
    {
        if (aspectRatio > WIDE_ASPECT_RATIO)
        {
            s_viewportWidth = height * 16 / 9;
            s_viewportHeight = height;
        }
        else
        {
            s_viewportWidth = width;
            s_viewportHeight = width * 9 / 16;
        }

        break;
    }

    case EAspectRatio::Narrow:
    case EAspectRatio::OriginalNarrow:
    {
        if (aspectRatio > NARROW_ASPECT_RATIO)
        {
            s_viewportWidth = height * 4 / 3;
            s_viewportHeight = height;
        }
        else
        {
            s_viewportWidth = width;
            s_viewportHeight = width * 3 / 4;
        }

        break;
    }

    default:
        s_viewportWidth = width;
        s_viewportHeight = height;
        break;
    }

    AspectRatioPatches::ComputeOffsets();
}

static RenderFormat ConvertFormat(uint32_t format)
{
    switch (format)
    {
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_A16B16G16R16F_2:
        return RenderFormat::R16G16B16A16_FLOAT;
    case D3DFMT_A8B8G8R8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
        return RenderFormat::D32_FLOAT;
    case D3DFMT_G16R16F:
    case D3DFMT_G16R16F_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DFMT_INDEX16:
        return RenderFormat::R16_UINT;
    case D3DFMT_INDEX32:
        return RenderFormat::R32_UINT;
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        return RenderFormat::R8_UNORM;
    default:
        assert(false && "Unknown format");
        return RenderFormat::R16G16B16A16_FLOAT;
    }
}

static GuestTexture* CreateTexture(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool, uint32_t type) 
{
    const auto texture = g_userHeap.AllocPhysical<GuestTexture>(type == 17 ? ResourceType::VolumeTexture : ResourceType::Texture);

    RenderTextureDesc desc;
    desc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureDimension::TEXTURE_3D : RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.depth = depth;
    desc.mipLevels = levels;
    desc.arraySize = 1;
    desc.format = ConvertFormat(format);

    if (desc.format == RenderFormat::D32_FLOAT)
        desc.flags = RenderTextureFlag::DEPTH_TARGET;
    else if (usage != 0)
        desc.flags = RenderTextureFlag::RENDER_TARGET;
    else
        desc.flags = RenderTextureFlag::NONE;

    texture->textureHolder = g_device->createTexture(desc);
    texture->texture = texture->textureHolder.get();

    RenderTextureViewDesc viewDesc;
    viewDesc.format = desc.format;
    viewDesc.dimension = texture->type == ResourceType::VolumeTexture ? RenderTextureViewDimension::TEXTURE_3D : RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = levels;

    switch (format)
    {
    case D3DFMT_D24FS8:
    case D3DFMT_D24S8:
    case D3DFMT_L8:
    case D3DFMT_L8_2:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::R, RenderSwizzle::ONE);
        break;

    case D3DFMT_X8R8G8B8:
        viewDesc.componentMapping = RenderComponentMapping(RenderSwizzle::G, RenderSwizzle::B, RenderSwizzle::A, RenderSwizzle::ONE);
        break;
    }

    texture->textureView = texture->texture->createTextureView(viewDesc);

    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->format = desc.format;
    texture->viewDimension = viewDesc.dimension;
    texture->descriptorIndex = g_textureDescriptorAllocator.allocate();

    g_textureDescriptorSet->setTexture(texture->descriptorIndex, texture->texture, RenderTextureLayout::SHADER_READ, texture->textureView.get());
   
#ifdef _DEBUG 
    texture->texture->setName(fmt::format("Texture {:X}", g_memory.MapVirtual(texture)));
#endif

    return texture;
}

static RenderHeapType GetBufferHeapType()
{
    return g_capabilities.gpuUploadHeap ? RenderHeapType::GPU_UPLOAD : RenderHeapType::DEFAULT;
}

static GuestBuffer* CreateVertexBuffer(uint32_t length) 
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::VertexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::VertexBuffer(length, GetBufferHeapType(), RenderBufferFlag::INDEX));
    buffer->dataSize = length;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Vertex Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static GuestBuffer* CreateIndexBuffer(uint32_t length, uint32_t, uint32_t format)
{
    auto buffer = g_userHeap.AllocPhysical<GuestBuffer>(ResourceType::IndexBuffer);
    buffer->buffer = g_device->createBuffer(RenderBufferDesc::IndexBuffer(length, GetBufferHeapType()));
    buffer->dataSize = length;
    buffer->format = ConvertFormat(format);
    buffer->guestFormat = format;
#ifdef _DEBUG 
    buffer->buffer->setName(fmt::format("Index Buffer {:X}", g_memory.MapVirtual(buffer)));
#endif
    return buffer;
}

static GuestSurface* CreateSurface(uint32_t width, uint32_t height, uint32_t format, uint32_t multiSample) 
{
    RenderTextureDesc desc;
    desc.dimension = RenderTextureDimension::TEXTURE_2D;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.multisampling.sampleCount = multiSample != 0 && Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : RenderSampleCount::COUNT_1;
    desc.format = ConvertFormat(format);
    desc.flags = desc.format == RenderFormat::D32_FLOAT ? RenderTextureFlag::DEPTH_TARGET : RenderTextureFlag::RENDER_TARGET;

    auto surface = g_userHeap.AllocPhysical<GuestSurface>(desc.format == RenderFormat::D32_FLOAT ? 
        ResourceType::DepthStencil : ResourceType::RenderTarget);

    surface->textureHolder = g_device->createTexture(desc);
    surface->texture = surface->textureHolder.get();
    surface->width = width;
    surface->height = height;
    surface->format = desc.format;
    surface->guestFormat = format;
    surface->sampleCount = desc.multisampling.sampleCount;

    RenderTextureViewDesc viewDesc;
    viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.format = desc.format;
    viewDesc.mipLevels = 1;
    surface->textureView = surface->textureHolder->createTextureView(viewDesc);
    surface->descriptorIndex = g_textureDescriptorAllocator.allocate();
    g_textureDescriptorSet->setTexture(surface->descriptorIndex, surface->textureHolder.get(), RenderTextureLayout::SHADER_READ, surface->textureView.get());

#ifdef _DEBUG 
    surface->texture->setName(fmt::format("{} {:X}", desc.flags & RenderTextureFlag::RENDER_TARGET ? "Render Target" : "Depth Stencil", g_memory.MapVirtual(surface)));
#endif

    return surface;
}

static void FlushViewport()
{
    auto& commandList = g_commandLists[g_frame];

    if (g_dirtyStates.viewport)
    {
        auto viewport = g_viewport;

        if (viewport.minDepth > viewport.maxDepth)
            std::swap(viewport.minDepth, viewport.maxDepth);

        commandList->setViewports(viewport);

        g_dirtyStates.viewport = false;
    }

    if (g_dirtyStates.scissorRect)
    {
        auto scissorRect = g_scissorTestEnable ? g_scissorRect : RenderRect(
            g_viewport.x,
            g_viewport.y,
            g_viewport.x + g_viewport.width,
            g_viewport.y + g_viewport.height);

        commandList->setScissors(scissorRect);

        g_dirtyStates.scissorRect = false;
    }
}

static void StretchRect(GuestDevice* device, uint32_t flags, uint32_t, GuestTexture* texture)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::StretchRect;
    cmd.stretchRect.flags = flags;
    cmd.stretchRect.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture);
static void SetSurface(uint32_t index, GuestSurface* surface);

static void ProcStretchRect(const RenderCommand& cmd)
{
    const auto& args = cmd.stretchRect;

    const bool isDepthStencil = (args.flags & 0x4) != 0;
    const auto surface = isDepthStencil ? g_depthStencil : g_renderTarget;

    // Erase previous pending command so it doesn't cause the texture to be overriden.
    if (args.texture->sourceSurface != nullptr)
        args.texture->sourceSurface->destinationTextures.erase(args.texture);

    args.texture->sourceSurface = surface;
    surface->destinationTextures.emplace(args.texture);

    // If the texture is assigned to any slots, set it again. This'll also push the barrier.
    for (uint32_t i = 0; i < std::size(g_textures); i++)
    {
        if (g_textures[i] == args.texture)
        {
            // Set the original texture for MSAA textures as they always get resolved.
            if (surface->sampleCount != RenderSampleCount::COUNT_1)
            {
                SetTextureInRenderThread(i, args.texture);
                g_pendingMsaaResolves.emplace(surface);
            }
            else
            {
                SetSurface(i, surface);
            }
        }
    }

    // Remember to clear later.
    g_pendingSurfaceCopies.emplace(surface);
}

static void SetDefaultViewport(GuestDevice* device, GuestSurface* surface)
{
    if (surface != nullptr)
    {
        RenderCommand cmd;
        cmd.type = RenderCommandType::SetViewport;
        cmd.setViewport.x = 0.0f;
        cmd.setViewport.y = 0.0f;
        cmd.setViewport.width = float(surface->width);
        cmd.setViewport.height = float(surface->height);
        cmd.setViewport.minDepth = 0.0f;
        cmd.setViewport.maxDepth = 1.0f;
        g_renderQueue.enqueue(cmd);

        device->viewport.x = 0.0f;
        device->viewport.y = 0.0f;
        device->viewport.width = float(surface->width);
        device->viewport.height = float(surface->height);
        device->viewport.minZ = 0.0f;
        device->viewport.maxZ = 1.0f;
    }
}

static void SetRenderTarget(GuestDevice* device, uint32_t index, GuestSurface* renderTarget) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetRenderTarget;
    cmd.setRenderTarget.renderTarget = renderTarget;
    g_renderQueue.enqueue(cmd);

    SetDefaultViewport(device, renderTarget);
}

static void ProcSetRenderTarget(const RenderCommand& cmd)
{
    const auto& args = cmd.setRenderTarget;

    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_renderTarget, args.renderTarget);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.renderTargetFormat, args.renderTarget != nullptr ? args.renderTarget->format : RenderFormat::UNKNOWN);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.sampleCount, args.renderTarget != nullptr ? args.renderTarget->sampleCount : RenderSampleCount::COUNT_1);

    // When alpha to coverage is enabled, update the alpha test mode as it's dependent on sample count.
    SetAlphaTestMode((g_pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0);
}

static void SetDepthStencilSurface(GuestDevice* device, GuestSurface* depthStencil) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetDepthStencilSurface;
    cmd.setDepthStencilSurface.depthStencil = depthStencil;
    g_renderQueue.enqueue(cmd);

    SetDefaultViewport(device, depthStencil);
}

static void ProcSetDepthStencilSurface(const RenderCommand& cmd)
{
    const auto& args = cmd.setDepthStencilSurface;

    SetDirtyValue(g_dirtyStates.renderTargetAndDepthStencil, g_depthStencil, args.depthStencil);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthStencilFormat, args.depthStencil != nullptr ? args.depthStencil->format : RenderFormat::UNKNOWN);
}

static bool PopulateBarriersForStretchRect(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    bool addedAny = false;

    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;

            RenderTextureLayout srcLayout;
            RenderTextureLayout dstLayout;
            bool shaderResolve = true;

            if (multiSampling && g_hardwareResolve)
            {
                // Hardware depth resolve is only supported on D3D12 when programmable sample positions are available.
                bool hardwareDepthResolveAvailable = g_hardwareDepthResolve && !g_vulkan && g_capabilities.sampleLocations;

                if (surface->format != RenderFormat::D32_FLOAT || hardwareDepthResolveAvailable)
                {
                    srcLayout = RenderTextureLayout::RESOLVE_SOURCE;
                    dstLayout = RenderTextureLayout::RESOLVE_DEST;
                    shaderResolve = false;
                }
            }
            
            if (shaderResolve)
            {
                srcLayout = RenderTextureLayout::SHADER_READ;
                dstLayout = (surface->format == RenderFormat::D32_FLOAT ? RenderTextureLayout::DEPTH_WRITE : RenderTextureLayout::COLOR_WRITE);
            }

            AddBarrier(surface, srcLayout);

            for (const auto texture : surface->destinationTextures)
                AddBarrier(texture, dstLayout);

            addedAny = true;
        }
    }

    return addedAny;
}

static void ExecutePendingStretchRectCommands(GuestSurface* renderTarget, GuestSurface* depthStencil)
{
    auto& commandList = g_commandLists[g_frame];

    for (const auto surface : { renderTarget, depthStencil })
    {
        if (surface != nullptr && !surface->destinationTextures.empty())
        {
            const bool multiSampling = surface->sampleCount != RenderSampleCount::COUNT_1;

            for (const auto texture : surface->destinationTextures)
            {
                bool shaderResolve = true;

                if (multiSampling && g_hardwareResolve)
                {
                    bool hardwareDepthResolveAvailable = g_hardwareDepthResolve && !g_vulkan && g_capabilities.sampleLocations;

                    if (surface->format != RenderFormat::D32_FLOAT || hardwareDepthResolveAvailable)
                    {
                        if (surface->format == RenderFormat::D32_FLOAT)
                            commandList->resolveTextureRegion(texture->texture, 0, 0, surface->texture, nullptr, RenderResolveMode::MIN);
                        else
                            commandList->resolveTexture(texture->texture, surface->texture);

                        shaderResolve = false;
                    }
                }

                if (shaderResolve)
                {
                    RenderPipeline* pipeline = nullptr;

                    if (multiSampling)
                    {
                        uint32_t pipelineIndex = 0;

                        switch (surface->sampleCount)
                        {
                        case RenderSampleCount::COUNT_2:
                            pipelineIndex = 0;
                            break;
                        case RenderSampleCount::COUNT_4:
                            pipelineIndex = 1;
                            break;
                        case RenderSampleCount::COUNT_8:
                            pipelineIndex = 2;
                            break;
                        default:
                            assert(false && "Unsupported MSAA sample count");
                            break;
                        }

                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            pipeline = g_resolveMsaaDepthPipelines[pipelineIndex].get();
                        }
                        else
                        {
                            auto& resolveMsaaColorPipeline = g_resolveMsaaColorPipelines[surface->format][pipelineIndex];
                            if (resolveMsaaColorPipeline == nullptr)
                            {
                                RenderGraphicsPipelineDesc desc;
                                desc.pipelineLayout = g_pipelineLayout.get();
                                desc.vertexShader = g_copyShader.get();
                                desc.pixelShader = g_resolveMsaaColorShaders[pipelineIndex].get();
                                desc.renderTargetFormat[0] = texture->format;
                                desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                                desc.renderTargetCount = 1;
                                resolveMsaaColorPipeline = g_device->createGraphicsPipeline(desc);
                            }

                            pipeline = resolveMsaaColorPipeline.get();
                        }
                    }
                    else
                    {
                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            pipeline = g_copyDepthPipeline.get();
                        }
                        else
                        {
                            auto& copyColorPipeline = g_copyColorPipelines[surface->format];
                            if (copyColorPipeline == nullptr)
                            {
                                RenderGraphicsPipelineDesc desc;
                                desc.pipelineLayout = g_pipelineLayout.get();
                                desc.vertexShader = g_copyShader.get();
                                desc.pixelShader = g_copyColorShader.get();
                                desc.renderTargetFormat[0] = texture->format;
                                desc.renderTargetBlend[0] = RenderBlendDesc::Copy();
                                desc.renderTargetCount = 1;
                                copyColorPipeline = g_device->createGraphicsPipeline(desc);
                            }

                            pipeline = copyColorPipeline.get();
                        }
                    }

                    if (texture->framebuffer == nullptr)
                    {
                        if (texture->format == RenderFormat::D32_FLOAT)
                        {
                            RenderFramebufferDesc desc;
                            desc.depthAttachment = texture->texture;
                            texture->framebuffer = g_device->createFramebuffer(desc);
                        }
                        else
                        {
                            RenderFramebufferDesc desc;
                            desc.colorAttachments = const_cast<const RenderTexture**>(&texture->texture);
                            desc.colorAttachmentsCount = 1;
                            texture->framebuffer = g_device->createFramebuffer(desc);
                        }
                    }

                    if (g_framebuffer != texture->framebuffer.get())
                    {
                        commandList->setFramebuffer(texture->framebuffer.get());
                        g_framebuffer = texture->framebuffer.get();
                    }

                    commandList->setPipeline(pipeline);
                    commandList->setViewports(RenderViewport(0.0f, 0.0f, float(texture->width), float(texture->height), 0.0f, 1.0f));
                    commandList->setScissors(RenderRect(0, 0, texture->width, texture->height));
                    commandList->setGraphicsPushConstants(0, &surface->descriptorIndex, 0, sizeof(uint32_t));
                    commandList->drawInstanced(6, 1, 0, 0);

                    g_dirtyStates.renderTargetAndDepthStencil = true;
                    g_dirtyStates.viewport = true;
                    g_dirtyStates.pipelineState = true;
                    g_dirtyStates.scissorRect = true;

                    if (g_vulkan)
                    {
                        g_dirtyStates.vertexShaderConstants = true; // The push constant call invalidates vertex shader constants.
                        g_dirtyStates.depthBias = true; // Static depth bias in copy pipeline invalidates dynamic depth bias.
                    }
                }

                texture->sourceSurface = nullptr;

                // Check if any texture slots had this texture assigned, and make it point back at the original texture.
                for (uint32_t i = 0; i < std::size(g_textures); i++)
                {
                    if (g_textures[i] == texture)
                        SetTextureInRenderThread(i, texture);
                }
            }

            surface->destinationTextures.clear();
        }
    }
}

static void ProcExecutePendingStretchRectCommands(const RenderCommand& cmd)
{
    bool foundAny = false;

    for (const auto surface : g_pendingSurfaceCopies)
    {
        // Depth stencil textures in this game are guaranteed to be transient.
        if (surface->format != RenderFormat::D32_FLOAT)
            foundAny |= PopulateBarriersForStretchRect(surface, nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();

        for (const auto surface : g_pendingSurfaceCopies)
        {
            if (surface->format != RenderFormat::D32_FLOAT)
                ExecutePendingStretchRectCommands(surface, nullptr);

            for (const auto texture : surface->destinationTextures)
                texture->sourceSurface = nullptr;

            surface->destinationTextures.clear();
        }
    }

    g_pendingSurfaceCopies.clear();
    g_pendingMsaaResolves.clear();
}

static void SetFramebuffer(GuestSurface* renderTarget, GuestSurface* depthStencil, bool settingForClear)
{
    if (settingForClear || g_dirtyStates.renderTargetAndDepthStencil)
    {
        GuestSurface* framebufferContainer = nullptr;
        RenderTexture* framebufferKey = nullptr;

        if (renderTarget != nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil; // Backbuffer texture changes per frame so we can't use the depth stencil as the key.
            framebufferKey = renderTarget->texture;
        }
        else if (renderTarget != nullptr && depthStencil == nullptr)
        {
            framebufferContainer = renderTarget;
            framebufferKey = renderTarget->texture; // Backbuffer texture changes per frame so we can't assume nullptr for it.
        }
        else if (renderTarget == nullptr && depthStencil != nullptr)
        {
            framebufferContainer = depthStencil;
            framebufferKey = nullptr;
        }

        auto& commandList = g_commandLists[g_frame];

        if (framebufferContainer != nullptr)
        {
            auto& framebuffer = framebufferContainer->framebuffers[framebufferKey];

            if (framebuffer == nullptr)
            {
                RenderFramebufferDesc desc;

                if (renderTarget != nullptr)
                {
                    desc.colorAttachments = const_cast<const RenderTexture**>(&renderTarget->texture);
                    desc.colorAttachmentsCount = 1;
                }

                if (depthStencil != nullptr)
                    desc.depthAttachment = depthStencil->texture;

                framebuffer = g_device->createFramebuffer(desc);
            }

            if (g_framebuffer != framebuffer.get())
            {
                commandList->setFramebuffer(framebuffer.get());
                g_framebuffer = framebuffer.get();
            }
        }
        else if (g_framebuffer != nullptr)
        {
            commandList->setFramebuffer(nullptr);
            g_framebuffer = nullptr;
        }

        if (g_framebuffer != nullptr)
        {
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetX, 1.0f / float(g_framebuffer->getWidth()));
            SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.halfPixelOffsetY, -1.0f / float(g_framebuffer->getHeight()));
        }

        g_dirtyStates.renderTargetAndDepthStencil = settingForClear;
    }
}

static void Clear(GuestDevice* device, uint32_t flags, uint32_t, be<float>* color, double z) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::Clear;
    cmd.clear.flags = flags;
    cmd.clear.color[0] = color[0];
    cmd.clear.color[1] = color[1];
    cmd.clear.color[2] = color[2];
    cmd.clear.color[3] = color[3];
    cmd.clear.z = float(z);
    g_renderQueue.enqueue(cmd);
}

static void ProcClear(const RenderCommand& cmd)
{
    const auto& args = cmd.clear;

    if (PopulateBarriersForStretchRect(g_renderTarget, g_depthStencil))
    {
        FlushBarriers();
        ExecutePendingStretchRectCommands(g_renderTarget, g_depthStencil);
    }

    AddBarrier(g_renderTarget, RenderTextureLayout::COLOR_WRITE);
    AddBarrier(g_depthStencil, RenderTextureLayout::DEPTH_WRITE);
    FlushBarriers();

    bool canClearInOnePass = (g_renderTarget == nullptr) || (g_depthStencil == nullptr) ||
        (g_renderTarget->width == g_depthStencil->width && g_renderTarget->height == g_depthStencil->height);

    if (canClearInOnePass)
        SetFramebuffer(g_renderTarget, g_depthStencil, true);

    auto& commandList = g_commandLists[g_frame];

    if (g_renderTarget != nullptr && (args.flags & D3DCLEAR_TARGET) != 0)
    {
        if (!canClearInOnePass)
            SetFramebuffer(g_renderTarget, nullptr, true);

        commandList->clearColor(0, RenderColor(args.color[0], args.color[1], args.color[2], args.color[3]));
    }

    if (g_depthStencil != nullptr && (args.flags & D3DCLEAR_ZBUFFER) != 0)
    {
        if (!canClearInOnePass)
            SetFramebuffer(nullptr, g_depthStencil, true);

        commandList->clearDepth(true, args.z);
    }
}

static void SetViewport(GuestDevice* device, GuestViewport* viewport)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetViewport;
    cmd.setViewport.x = viewport->x;
    cmd.setViewport.y = viewport->y;
    cmd.setViewport.width = viewport->width;
    cmd.setViewport.height = viewport->height;
    cmd.setViewport.minDepth = viewport->minZ;
    cmd.setViewport.maxDepth = viewport->maxZ;
    g_renderQueue.enqueue(cmd);

    device->viewport.x = float(viewport->x);
    device->viewport.y = float(viewport->y);
    device->viewport.width = float(viewport->width);
    device->viewport.height = float(viewport->height);
    device->viewport.minZ = viewport->minZ;
    device->viewport.maxZ = viewport->maxZ;
}

static void ProcSetViewport(const RenderCommand& cmd)
{
    const auto& args = cmd.setViewport;

    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.x, args.x);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.y, args.y);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.width, args.width);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.height, args.height);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.minDepth, args.minDepth);
    SetDirtyValue<float>(g_dirtyStates.viewport, g_viewport.maxDepth, args.maxDepth);
    
    uint32_t specConstants = g_pipelineState.specConstants;
    if (args.minDepth > args.maxDepth)
        specConstants |= SPEC_CONSTANT_REVERSE_Z;
    else 
        specConstants &= ~SPEC_CONSTANT_REVERSE_Z;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);

    g_dirtyStates.scissorRect |= g_dirtyStates.viewport;
}

static void SetTexture(GuestDevice* device, uint32_t index, GuestTexture* texture) 
{
    auto isPlayStation = Config::ControllerIcons == EControllerIcons::PlayStation;

    if (Config::ControllerIcons == EControllerIcons::Auto)
        isPlayStation = hid::g_inputDeviceController == hid::EInputDevice::PlayStation;

    if (isPlayStation && texture != nullptr && texture->patchedTexture != nullptr)
        texture = texture->patchedTexture.get();

    RenderCommand cmd;
    cmd.type = RenderCommandType::SetTexture;
    cmd.setTexture.index = index;
    cmd.setTexture.texture = texture;
    g_renderQueue.enqueue(cmd);
}

static void SetTextureInRenderThread(uint32_t index, GuestTexture* texture)
{
    AddBarrier(texture, RenderTextureLayout::SHADER_READ);

    auto viewDimension = texture != nullptr ? texture->viewDimension : RenderTextureViewDimension::UNKNOWN;

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index],
        viewDimension == RenderTextureViewDimension::TEXTURE_2D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_2D);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture3DIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_3D ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D);

    // Check if there's a cubemap texture we recreated and assign it if it's valid. The shader will pick whichever is correct.
    if (viewDimension == RenderTextureViewDimension::TEXTURE_2D && texture->recreatedCubeMapTexture != nullptr)
    {
        texture = texture->recreatedCubeMapTexture.get();
        AddBarrier(texture, RenderTextureLayout::SHADER_READ);
        viewDimension = texture->viewDimension;
    }

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], texture != nullptr &&
        viewDimension == RenderTextureViewDimension::TEXTURE_CUBE ? texture->descriptorIndex : TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE);
}

static void SetSurface(uint32_t index, GuestSurface* surface)
{
    AddBarrier(surface, RenderTextureLayout::SHADER_READ);

    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture2DIndices[index], surface->descriptorIndex);
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.texture3DIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_3D));
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.textureCubeIndices[index], uint32_t(TEXTURE_DESCRIPTOR_NULL_TEXTURE_CUBE));
}

static void ProcSetTexture(const RenderCommand& cmd)
{
    const auto& args = cmd.setTexture;

    // If a pending copy operation is detected, set the source surface. The indices will be fixed later if flushing is necessary.
    bool shouldSetTexture = true;
    if (args.texture != nullptr && args.texture->sourceSurface != nullptr)
    {
        // MSAA surfaces need to be resolved and cannot be used directly.
        if (args.texture->sourceSurface->sampleCount != RenderSampleCount::COUNT_1)
        {
            g_pendingMsaaResolves.emplace(args.texture->sourceSurface);
        }
        else
        {
            SetSurface(args.index, args.texture->sourceSurface);
            shouldSetTexture = false;
        }
    }
    
    if (shouldSetTexture)
        SetTextureInRenderThread(args.index, args.texture);
    
    g_textures[args.index] = args.texture;
}

static void SetScissorRect(GuestDevice* device, GuestRect* rect)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetScissorRect;
    cmd.setScissorRect.top = rect->top;
    cmd.setScissorRect.left = rect->left;
    cmd.setScissorRect.bottom = rect->bottom;
    cmd.setScissorRect.right = rect->right;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetScissorRect(const RenderCommand& cmd)
{
    const auto& args = cmd.setScissorRect;

    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.top, args.top);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.left, args.left);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.bottom, args.bottom);
    SetDirtyValue<int32_t>(g_dirtyStates.scissorRect, g_scissorRect.right, args.right);
}

static RenderShader* GetOrLinkShader(GuestShader* guestShader, uint32_t specConstants)
{
    if (g_vulkan ||
        guestShader->shaderCacheEntry == nullptr || 
        guestShader->shaderCacheEntry->specConstantsMask == 0)
    {
        std::lock_guard lock(guestShader->mutex);

        if (guestShader->shader == nullptr)
        {
            assert(guestShader->shaderCacheEntry != nullptr);

            if (g_vulkan)
            {
                auto compressedSpirvData = g_shaderCache.get() + guestShader->shaderCacheEntry->spirvOffset;

                std::vector<uint8_t> decoded(smolv::GetDecodedBufferSize(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize));
                bool result = smolv::Decode(compressedSpirvData, guestShader->shaderCacheEntry->spirvSize, decoded.data(), decoded.size());
                assert(result);

                guestShader->shader = g_device->createShader(decoded.data(), decoded.size(), "main", RenderShaderFormat::SPIRV);
            }
            else
            {
                guestShader->shader = g_device->createShader(g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset, 
                    guestShader->shaderCacheEntry->dxilSize, "main", RenderShaderFormat::DXIL);
            }
        }

        return guestShader->shader.get();
    }

    specConstants &= guestShader->shaderCacheEntry->specConstantsMask;

    RenderShader* shader;
    {
        std::lock_guard lock(guestShader->mutex);
        shader = guestShader->linkedShaders[specConstants].get();
    }

#ifdef UNLEASHED_RECOMP_D3D12
    if (shader == nullptr)
    {
        static Mutex g_compiledSpecConstantLibraryBlobMutex;
        static ankerl::unordered_dense::map<uint32_t, ComPtr<IDxcBlob>> g_compiledSpecConstantLibraryBlobs;

        thread_local ComPtr<IDxcCompiler3> s_dxcCompiler;
        thread_local ComPtr<IDxcLinker> s_dxcLinker;
        thread_local ComPtr<IDxcUtils> s_dxcUtils;

        wchar_t specConstantsLibName[0x100];
        swprintf_s(specConstantsLibName, L"SpecConstants_%d", specConstants);

        ComPtr<IDxcBlob> specConstantLibraryBlob;
        {
            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            specConstantLibraryBlob = g_compiledSpecConstantLibraryBlobs[specConstants];
        }

        if (specConstantLibraryBlob == nullptr)
        {
            if (s_dxcCompiler == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(s_dxcCompiler.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcCompiler != nullptr);
            }

            char libraryHlsl[0x100];
            sprintf_s(libraryHlsl, "export uint g_SpecConstants() { return %d; }", specConstants);

            DxcBuffer buffer{};
            buffer.Ptr = libraryHlsl;
            buffer.Size = strlen(libraryHlsl);

            const wchar_t* args[1];
            args[0] = L"-T lib_6_3";

            ComPtr<IDxcResult> result;
            HRESULT hr = s_dxcCompiler->Compile(&buffer, args, std::size(args), nullptr, IID_PPV_ARGS(result.GetAddressOf()));
            assert(SUCCEEDED(hr) && result != nullptr);

            hr = result->GetResult(specConstantLibraryBlob.GetAddressOf());
            assert(SUCCEEDED(hr) && specConstantLibraryBlob != nullptr);

            std::lock_guard lock(g_compiledSpecConstantLibraryBlobMutex);
            g_compiledSpecConstantLibraryBlobs.emplace(specConstants, specConstantLibraryBlob);
        }

        if (s_dxcLinker == nullptr)
        {
            HRESULT hr = DxcCreateInstance(CLSID_DxcLinker, IID_PPV_ARGS(s_dxcLinker.GetAddressOf()));
            assert(SUCCEEDED(hr) && s_dxcLinker != nullptr);
        }

        s_dxcLinker->RegisterLibrary(specConstantsLibName, specConstantLibraryBlob.Get());

        wchar_t shaderLibName[0x100];
        swprintf_s(shaderLibName, L"Shader_%d", guestShader->shaderCacheEntry->dxilOffset);

        ComPtr<IDxcBlobEncoding> shaderLibraryBlob;
        {
            std::lock_guard lock(guestShader->mutex);
            shaderLibraryBlob = guestShader->libraryBlob;
        }

        if (shaderLibraryBlob == nullptr)
        {
            if (s_dxcUtils == nullptr)
            {
                HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(s_dxcUtils.GetAddressOf()));
                assert(SUCCEEDED(hr) && s_dxcUtils != nullptr);
            }

            HRESULT hr = s_dxcUtils->CreateBlobFromPinned(
                g_shaderCache.get() + guestShader->shaderCacheEntry->dxilOffset,
                guestShader->shaderCacheEntry->dxilSize,
                DXC_CP_ACP,
                shaderLibraryBlob.GetAddressOf());

            assert(SUCCEEDED(hr) && shaderLibraryBlob != nullptr);

            std::lock_guard lock(guestShader->mutex);
            guestShader->libraryBlob = shaderLibraryBlob;
        }

        s_dxcLinker->RegisterLibrary(shaderLibName, shaderLibraryBlob.Get());

        const wchar_t* libraryNames[] = { specConstantsLibName, shaderLibName };

        ComPtr<IDxcOperationResult> result;
        HRESULT hr = s_dxcLinker->Link(L"main", guestShader->type == ResourceType::VertexShader ? L"vs_6_0" : L"ps_6_0",
            libraryNames, std::size(libraryNames), nullptr, 0, result.GetAddressOf());

        assert(SUCCEEDED(hr) && result != nullptr);

        ComPtr<IDxcBlob> blob;
        hr = result->GetResult(blob.GetAddressOf());
        assert(SUCCEEDED(hr) && blob != nullptr);

        {
            std::lock_guard lock(guestShader->mutex);

            auto& linkedShader = guestShader->linkedShaders[specConstants];
            if (linkedShader == nullptr)
            {
                linkedShader = g_device->createShader(blob->GetBufferPointer(), blob->GetBufferSize(), "main", RenderShaderFormat::DXIL);
                guestShader->shaderBlobs.push_back(std::move(blob));
            }

            shader = linkedShader.get();
        }        
    }
#endif

    return shader;
}

static void SanitizePipelineState(PipelineState& pipelineState)
{
    if (!pipelineState.zEnable)
    {
        pipelineState.zWriteEnable = false;
        pipelineState.zFunc = RenderComparisonFunction::LESS;
        pipelineState.slopeScaledDepthBias = 0.0f;
        pipelineState.depthBias = 0;
        pipelineState.depthStencilFormat = RenderFormat::UNKNOWN;
    }

    if (pipelineState.slopeScaledDepthBias == 0.0f)
        pipelineState.slopeScaledDepthBias = 0.0f; // Remove sign.

    if (!pipelineState.colorWriteEnable)
    {
        pipelineState.alphaBlendEnable = false;
        pipelineState.renderTargetFormat = RenderFormat::UNKNOWN;
    }

    if (!pipelineState.alphaBlendEnable)
    {
        pipelineState.srcBlend = RenderBlend::ONE;
        pipelineState.destBlend = RenderBlend::ZERO;
        pipelineState.blendOp = RenderBlendOperation::ADD;
        pipelineState.srcBlendAlpha = RenderBlend::ONE;
        pipelineState.destBlendAlpha = RenderBlend::ZERO;
        pipelineState.blendOpAlpha = RenderBlendOperation::ADD;
    }

    for (size_t i = 0; i < 16; i++)
    {
        if (!pipelineState.vertexDeclaration->vertexStreams[i])
            pipelineState.vertexStrides[i] = 0;
    }

    uint32_t specConstantsMask = 0;
    if (pipelineState.vertexShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.vertexShader->shaderCacheEntry->specConstantsMask;

    if (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry != nullptr)
        specConstantsMask |= pipelineState.pixelShader->shaderCacheEntry->specConstantsMask;

    pipelineState.specConstants &= specConstantsMask;
}

static std::unique_ptr<RenderPipeline> CreateGraphicsPipeline(const PipelineState& pipelineState)
{
#ifdef ASYNC_PSO_DEBUG
    ++g_pipelinesCurrentlyCompiling;
#endif

    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = g_pipelineLayout.get();
    desc.vertexShader = GetOrLinkShader(pipelineState.vertexShader, pipelineState.specConstants);
    desc.pixelShader = pipelineState.pixelShader != nullptr ? GetOrLinkShader(pipelineState.pixelShader, pipelineState.specConstants) : nullptr;
    desc.depthFunction = pipelineState.zFunc;
    desc.depthEnabled = pipelineState.zEnable;
    desc.depthWriteEnabled = pipelineState.zWriteEnable;
    desc.depthBias = pipelineState.depthBias;
    desc.slopeScaledDepthBias = pipelineState.slopeScaledDepthBias;
    desc.dynamicDepthBiasEnabled = g_capabilities.dynamicDepthBias;
    desc.depthClipEnabled = true;
    desc.primitiveTopology = pipelineState.primitiveTopology;
    desc.cullMode = pipelineState.cullMode;
    desc.renderTargetFormat[0] = pipelineState.renderTargetFormat;
    desc.renderTargetBlend[0].blendEnabled = pipelineState.alphaBlendEnable;
    desc.renderTargetBlend[0].srcBlend = pipelineState.srcBlend;
    desc.renderTargetBlend[0].dstBlend = pipelineState.destBlend;
    desc.renderTargetBlend[0].blendOp = pipelineState.blendOp;
    desc.renderTargetBlend[0].srcBlendAlpha = pipelineState.srcBlendAlpha;
    desc.renderTargetBlend[0].dstBlendAlpha = pipelineState.destBlendAlpha;
    desc.renderTargetBlend[0].blendOpAlpha = pipelineState.blendOpAlpha;
    desc.renderTargetBlend[0].renderTargetWriteMask = pipelineState.colorWriteEnable;
    desc.renderTargetCount = pipelineState.renderTargetFormat != RenderFormat::UNKNOWN ? 1 : 0;
    desc.depthTargetFormat = pipelineState.depthStencilFormat;
    desc.multisampling.sampleCount = pipelineState.sampleCount;
    desc.alphaToCoverageEnabled = pipelineState.enableAlphaToCoverage;
    desc.inputElements = pipelineState.vertexDeclaration->inputElements.get();
    desc.inputElementsCount = pipelineState.vertexDeclaration->inputElementCount;
    
    RenderSpecConstant specConstant{};
    specConstant.value = pipelineState.specConstants;
    
    if (pipelineState.specConstants != 0)
    {
        desc.specConstants = &specConstant;
        desc.specConstantsCount = 1;
    }
    
    RenderInputSlot inputSlots[16]{};
    uint32_t inputSlotIndices[16]{};
    uint32_t inputSlotCount = 0;
    
    for (size_t i = 0; i < pipelineState.vertexDeclaration->inputElementCount; i++)
    {
        auto& inputElement = pipelineState.vertexDeclaration->inputElements[i];
        auto& inputSlotIndex = inputSlotIndices[inputElement.slotIndex];
    
        if (inputSlotIndex == NULL)
            inputSlotIndex = ++inputSlotCount;
    
        auto& inputSlot = inputSlots[inputSlotIndex - 1];
        inputSlot.index = inputElement.slotIndex;
        inputSlot.stride = pipelineState.vertexStrides[inputElement.slotIndex];
    
        if (pipelineState.instancing && inputElement.slotIndex != 0 && inputElement.slotIndex != 15)
            inputSlot.classification = RenderInputSlotClassification::PER_INSTANCE_DATA;
        else
            inputSlot.classification = RenderInputSlotClassification::PER_VERTEX_DATA;
    }
    
    desc.inputSlots = inputSlots;
    desc.inputSlotsCount = inputSlotCount;
    
    auto pipeline = g_device->createGraphicsPipeline(desc);

#ifdef ASYNC_PSO_DEBUG
    --g_pipelinesCurrentlyCompiling;
#endif

    return pipeline;
}

static RenderPipeline* CreateGraphicsPipelineInRenderThread(PipelineState pipelineState)
{
    SanitizePipelineState(pipelineState);

    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
    auto& pipeline = g_pipelines[hash];
    if (pipeline == nullptr)
    {
        pipeline = CreateGraphicsPipeline(pipelineState);

#ifdef ASYNC_PSO_DEBUG
        bool loading = *SWA::SGlobals::ms_IsLoading;

        if (loading)
            ++g_pipelinesCreatedAsynchronously;
        else
            ++g_pipelinesCreatedInRenderThread;

        pipeline->setName(fmt::format("{} {} {} {:X}", loading ? "ASYNC" : "",
            pipelineState.vertexShader->name, pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>", hash));
        
        if (!loading)
        {
            std::lock_guard lock(g_debugMutex);
            g_pipelineDebugText = fmt::format(
                "PipelineState {:X}:\n"
                "  vertexShader: {}\n"
                "  pixelShader: {}\n"
                "  vertexDeclaration: {:X}\n"
                "  instancing: {}\n"
                "  zEnable: {}\n"
                "  zWriteEnable: {}\n"
                "  srcBlend: {}\n"
                "  destBlend: {}\n"
                "  cullMode: {}\n"
                "  zFunc: {}\n"
                "  alphaBlendEnable: {}\n"
                "  blendOp: {}\n"
                "  slopeScaledDepthBias: {}\n"
                "  depthBias: {}\n"
                "  srcBlendAlpha: {}\n"
                "  destBlendAlpha: {}\n"
                "  blendOpAlpha: {}\n"
                "  colorWriteEnable: {:X}\n"
                "  primitiveTopology: {}\n"
                "  vertexStrides[0]: {}\n"
                "  vertexStrides[1]: {}\n"
                "  vertexStrides[2]: {}\n"
                "  vertexStrides[3]: {}\n"
                "  renderTargetFormat: {}\n"
                "  depthStencilFormat: {}\n"
                "  sampleCount: {}\n"
                "  enableAlphaToCoverage: {}\n"
                "  specConstants: {:X}\n",
                hash,
                pipelineState.vertexShader->name,
                pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->name : "<none>",
                reinterpret_cast<size_t>(pipelineState.vertexDeclaration),
                pipelineState.instancing,
                pipelineState.zEnable,
                pipelineState.zWriteEnable,
                magic_enum::enum_name(pipelineState.srcBlend),
                magic_enum::enum_name(pipelineState.destBlend),
                magic_enum::enum_name(pipelineState.cullMode),
                magic_enum::enum_name(pipelineState.zFunc),
                pipelineState.alphaBlendEnable,
                magic_enum::enum_name(pipelineState.blendOp),
                pipelineState.slopeScaledDepthBias,
                pipelineState.depthBias,
                magic_enum::enum_name(pipelineState.srcBlendAlpha),
                magic_enum::enum_name(pipelineState.destBlendAlpha),
                magic_enum::enum_name(pipelineState.blendOpAlpha),
                pipelineState.colorWriteEnable,
                magic_enum::enum_name(pipelineState.primitiveTopology),
                pipelineState.vertexStrides[0],
                pipelineState.vertexStrides[1],
                pipelineState.vertexStrides[2],
                pipelineState.vertexStrides[3],
                magic_enum::enum_name(pipelineState.renderTargetFormat),
                magic_enum::enum_name(pipelineState.depthStencilFormat),
                pipelineState.sampleCount,
                pipelineState.enableAlphaToCoverage,
                pipelineState.specConstants)
                + g_pipelineDebugText;
        }
#endif

#ifdef PSO_CACHING
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.emplace(hash, pipelineState);
#endif
    }
    
    return pipeline.get();
}

static RenderTextureAddressMode ConvertTextureAddressMode(size_t value)
{
    switch (value)
    {
    case D3DTADDRESS_WRAP:
        return RenderTextureAddressMode::WRAP;
    case D3DTADDRESS_MIRROR:
        return RenderTextureAddressMode::MIRROR;
    case D3DTADDRESS_CLAMP:
        return RenderTextureAddressMode::CLAMP;
    case D3DTADDRESS_MIRRORONCE:
        return RenderTextureAddressMode::MIRROR_ONCE;
    case D3DTADDRESS_BORDER:
        return RenderTextureAddressMode::BORDER;
    default:
        assert(false && "Unknown texture address mode");
        return RenderTextureAddressMode::UNKNOWN;
    }
}

static RenderFilter ConvertTextureFilter(uint32_t value)
{
    switch (value)
    {
    case D3DTEXF_POINT:
    case D3DTEXF_NONE:
        return RenderFilter::NEAREST;
    case D3DTEXF_LINEAR:
        return RenderFilter::LINEAR;
    default:
        assert(false && "Unknown texture filter");
        return RenderFilter::UNKNOWN;
    }
}

static RenderBorderColor ConvertBorderColor(uint32_t value)
{
    switch (value)
    {
    case 0:
        return RenderBorderColor::TRANSPARENT_BLACK;
    case 1:
        return RenderBorderColor::OPAQUE_WHITE;
    default:
        assert(false && "Unknown border color");
        return RenderBorderColor::UNKNOWN;
    }
}

struct LocalRenderCommandQueue
{
    RenderCommand commands[20];
    uint32_t count = 0;

    RenderCommand& enqueue()
    {
        assert(count < std::size(commands));
        return commands[count++];
    }

    void submit()
    {
        g_renderQueue.enqueue_bulk(commands, count);
    }
};

static void FlushRenderStateForMainThread(GuestDevice* device, LocalRenderCommandQueue& queue)
{
    constexpr size_t BOOL_MASK = 0x100000000000000ull;
    if ((device->dirtyFlags[4].get() & BOOL_MASK) != 0)
    {
        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetBooleans;
        cmd.setBooleans.booleans = (device->vertexShaderBoolConstants[0].get() & 0xFF) | ((device->pixelShaderBoolConstants[0].get() & 0xFF) << 16);

        device->dirtyFlags[4] = device->dirtyFlags[4].get() & ~BOOL_MASK;
    }

    for (uint32_t i = 0; i < 16; i++)
    {
        const size_t mask = 0x8000000000000000ull >> (i + 32);
        if (device->dirtyFlags[3].get() & mask)
        {
            auto& cmd = queue.enqueue();
            cmd.type = RenderCommandType::SetSamplerState;
            cmd.setSamplerState.index = i;
            cmd.setSamplerState.data0 = device->samplerStates[i].data[0];
            cmd.setSamplerState.data3 = device->samplerStates[i].data[3];
            cmd.setSamplerState.data5 = device->samplerStates[i].data[5];

            device->dirtyFlags[3] = device->dirtyFlags[3].get() & ~mask;
        }
    }

    uint64_t dirtyFlags = device->dirtyFlags[0].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = 64 - std::countr_zero(dirtyFlags);

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetVertexShaderConstants;
        cmd.setVertexShaderConstants.memory = g_intermediaryUploadAllocator.allocate(&device->vertexShaderFloatConstants[index], size);
        cmd.setVertexShaderConstants.index = index;
        cmd.setVertexShaderConstants.size = size;

        device->dirtyFlags[0] = 0;
    }

    dirtyFlags = device->dirtyFlags[1].get();
    if (dirtyFlags != 0)
    {
        int startRegister = std::countl_zero(dirtyFlags);
        int endRegister = std::min(56, 64 - std::countr_zero(dirtyFlags));

        uint32_t index = startRegister * 16;
        uint32_t size = (endRegister - startRegister) * 64;

        auto& cmd = queue.enqueue();
        cmd.type = RenderCommandType::SetPixelShaderConstants;
        cmd.setPixelShaderConstants.memory = g_intermediaryUploadAllocator.allocate(&device->pixelShaderFloatConstants[index], size);
        cmd.setPixelShaderConstants.index = index;
        cmd.setPixelShaderConstants.size = size;

        device->dirtyFlags[1] = 0;
    }
}

static void ProcSetBooleans(const RenderCommand& cmd)
{
    SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.booleans, cmd.setBooleans.booleans);
}

static void ProcSetSamplerState(const RenderCommand& cmd)
{
    const auto& args = cmd.setSamplerState;

    const auto addressU = ConvertTextureAddressMode((args.data0 >> 10) & 0x7);
    const auto addressV = ConvertTextureAddressMode((args.data0 >> 13) & 0x7);
    const auto addressW = ConvertTextureAddressMode((args.data0 >> 16) & 0x7);
    auto magFilter = ConvertTextureFilter((args.data3 >> 19) & 0x3);
    auto minFilter = ConvertTextureFilter((args.data3 >> 21) & 0x3);
    auto mipFilter = ConvertTextureFilter((args.data3 >> 23) & 0x3);
    const auto borderColor = ConvertBorderColor(args.data5 & 0x3);

    bool anisotropyEnabled = Config::AnisotropicFiltering > 0 && mipFilter == RenderFilter::LINEAR;
    if (anisotropyEnabled)
    {
        magFilter = RenderFilter::LINEAR;
        minFilter = RenderFilter::LINEAR;
    }

    auto& samplerDesc = g_samplerDescs[args.index];

    bool dirty = false;

    SetDirtyValue(dirty, samplerDesc.addressU, addressU);
    SetDirtyValue(dirty, samplerDesc.addressV, addressV);
    SetDirtyValue(dirty, samplerDesc.addressW, addressW);
    SetDirtyValue(dirty, samplerDesc.minFilter, minFilter);
    SetDirtyValue(dirty, samplerDesc.magFilter, magFilter);
    SetDirtyValue(dirty, samplerDesc.mipmapMode, RenderMipmapMode(mipFilter));
    SetDirtyValue(dirty, samplerDesc.maxAnisotropy, anisotropyEnabled ? Config::AnisotropicFiltering : 16u);
    SetDirtyValue(dirty, samplerDesc.anisotropyEnabled, anisotropyEnabled);
    SetDirtyValue(dirty, samplerDesc.borderColor, borderColor);

    if (dirty)
    {
        auto& [descriptorIndex, sampler] = g_samplerStates[XXH3_64bits(&samplerDesc, sizeof(RenderSamplerDesc))];
        if (descriptorIndex == NULL)
        {
            descriptorIndex = g_samplerStates.size();
            sampler = g_device->createSampler(samplerDesc);

            g_samplerDescriptorSet->setSampler(descriptorIndex - 1, sampler.get());
        }

        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.samplerIndices[args.index], descriptorIndex - 1);
    }
}

static void ProcSetVertexShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_vertexShaderConstants));

    memcpy(&g_vertexShaderConstants[args.index], args.memory, args.size);
    g_dirtyStates.vertexShaderConstants = true;
}

static void ProcSetPixelShaderConstants(const RenderCommand& cmd)
{
    auto& args = cmd.setPixelShaderConstants;
    assert((args.index * sizeof(uint32_t) + args.size) <= sizeof(g_pixelShaderConstants));

    memcpy(&g_pixelShaderConstants[args.index], args.memory, args.size);
    g_dirtyStates.pixelShaderConstants = true;
}

static void ProcAddPipeline(const RenderCommand& cmd)
{
    auto& args = cmd.addPipeline;
    auto& pipeline = g_pipelines[args.hash];

    if (pipeline == nullptr)
    {
        pipeline = std::unique_ptr<RenderPipeline>(args.pipeline);
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesCreatedAsynchronously;
#endif
    }
    else
    {
#ifdef ASYNC_PSO_DEBUG
        ++g_pipelinesDropped;
#endif
        delete args.pipeline;
    }
}

static constexpr int32_t COMMON_DEPTH_BIAS_VALUE = int32_t((1 << 24) * 0.002f);
static constexpr float COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE = 1.0f;

static void FlushRenderStateForRenderThread()
{
    auto renderTarget = g_pipelineState.colorWriteEnable ? g_renderTarget : nullptr;
    auto depthStencil = g_pipelineState.zEnable ? g_depthStencil : nullptr;

    bool foundAny = PopulateBarriersForStretchRect(renderTarget, depthStencil);

    for (const auto surface : g_pendingMsaaResolves)
    {
        bool isDepthStencil = (surface->format == RenderFormat::D32_FLOAT);
        foundAny |= PopulateBarriersForStretchRect(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
    }

    if (foundAny)
    {
        FlushBarriers();
        ExecutePendingStretchRectCommands(renderTarget, depthStencil);

        for (const auto surface : g_pendingMsaaResolves)
        {
            bool isDepthStencil = (surface->format == RenderFormat::D32_FLOAT);
            ExecutePendingStretchRectCommands(isDepthStencil ? nullptr : surface, isDepthStencil ? surface : nullptr);
        }
    }

    if (!g_pendingMsaaResolves.empty())
        g_pendingMsaaResolves.clear();

    AddBarrier(renderTarget, RenderTextureLayout::COLOR_WRITE);
    AddBarrier(depthStencil, RenderTextureLayout::DEPTH_WRITE);

    FlushBarriers();

    SetFramebuffer(renderTarget, depthStencil, false);
    FlushViewport();

    auto& commandList = g_commandLists[g_frame];

    // D3D12 resets depth bias values to the pipeline values, even if they are dynamic.
    // We can reduce unnecessary calls by making common depth bias values part of the pipeline.
    if (g_capabilities.dynamicDepthBias && !g_vulkan)
    {
        bool useDepthBias = (g_depthBias != 0) || (g_slopeScaledDepthBias != 0.0f);

        int32_t depthBias = useDepthBias ? COMMON_DEPTH_BIAS_VALUE : 0;
        float slopeScaledDepthBias = useDepthBias ? COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE : 0.0f;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.depthBias, depthBias);
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.slopeScaledDepthBias, slopeScaledDepthBias);
    }

    if (g_dirtyStates.pipelineState)
    {
        commandList->setPipeline(CreateGraphicsPipelineInRenderThread(g_pipelineState));

        // D3D12 resets the depth bias values. Check if they need to be set again.
        if (g_capabilities.dynamicDepthBias && !g_vulkan)
            g_dirtyStates.depthBias = (g_depthBias != g_pipelineState.depthBias) || (g_slopeScaledDepthBias != g_pipelineState.slopeScaledDepthBias);
    }

    if (g_dirtyStates.depthBias && g_capabilities.dynamicDepthBias)
        commandList->setDepthBias(g_depthBias, 0.0f, g_slopeScaledDepthBias);

    if (g_dirtyStates.vertexShaderConstants)
    {
        auto vertexShaderConstants = g_uploadAllocators[g_frame].allocate<true>(g_vertexShaderConstants, sizeof(g_vertexShaderConstants), 0x100);
        SetRootDescriptor(vertexShaderConstants, 0);
    }

    if (g_dirtyStates.pixelShaderConstants)
    {
        auto pixelShaderConstants = g_uploadAllocators[g_frame].allocate<true>(g_pixelShaderConstants, sizeof(g_pixelShaderConstants), 0x100);
        SetRootDescriptor(pixelShaderConstants, 1);
    }

    if (g_dirtyStates.sharedConstants)
    {
        auto sharedConstants = g_uploadAllocators[g_frame].allocate<false>(&g_sharedConstants, sizeof(g_sharedConstants), 0x100);
        SetRootDescriptor(sharedConstants, 2);
    }

    if (g_dirtyStates.vertexStreamFirst <= g_dirtyStates.vertexStreamLast)
    {
        commandList->setVertexBuffers(
            g_dirtyStates.vertexStreamFirst,
            g_vertexBufferViews + g_dirtyStates.vertexStreamFirst,
            g_dirtyStates.vertexStreamLast - g_dirtyStates.vertexStreamFirst + 1,
            g_inputSlots + g_dirtyStates.vertexStreamFirst);
    }

    if (g_dirtyStates.indices && (!g_vulkan || g_indexBufferView.buffer.ref != nullptr))
        commandList->setIndexBuffer(&g_indexBufferView);

    g_dirtyStates = DirtyStates(false);
}

static RenderPrimitiveTopology ConvertPrimitiveType(uint32_t primitiveType)
{
    switch (primitiveType)
    {
    case D3DPT_POINTLIST:
        return RenderPrimitiveTopology::POINT_LIST;
    case D3DPT_LINELIST:
        return RenderPrimitiveTopology::LINE_LIST;
    case D3DPT_LINESTRIP:
        return RenderPrimitiveTopology::LINE_STRIP;
    case D3DPT_TRIANGLELIST:
    case D3DPT_QUADLIST:
        return RenderPrimitiveTopology::TRIANGLE_LIST;
    case D3DPT_TRIANGLESTRIP:
        return RenderPrimitiveTopology::TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN:
        return g_capabilities.triangleFan ? RenderPrimitiveTopology::TRIANGLE_FAN : RenderPrimitiveTopology::TRIANGLE_LIST;
    default:
        assert(false && "Unknown primitive type");
        return RenderPrimitiveTopology::UNKNOWN;
    }
}

static void SetPrimitiveType(uint32_t primitiveType)
{
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.primitiveTopology, ConvertPrimitiveType(primitiveType));
}

static uint32_t CheckInstancing()
{
    uint32_t indexCount = 0;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.instancing, g_pipelineState.vertexDeclaration->indexVertexStream != 0);
    if (g_pipelineState.instancing)
    {
        // Index buffer is passed as a vertex stream
        indexCount = g_vertexBufferViews[g_pipelineState.vertexDeclaration->indexVertexStream].size / 4;
    }

    return indexCount;
}

static void UnsetInstancingStream()
{
    bool dirty = false;
    uint32_t index = g_pipelineState.vertexDeclaration->indexVertexStream;

    SetDirtyValue(dirty, g_vertexBufferViews[index].buffer, RenderBufferReference{});
    SetDirtyValue(dirty, g_vertexBufferViews[index].size, 0u);
    SetDirtyValue(dirty, g_inputSlots[index].stride, 0u);

    if (dirty)
    {
        g_dirtyStates.vertexStreamFirst = std::min<uint8_t>(g_dirtyStates.vertexStreamFirst, index);
        g_dirtyStates.vertexStreamLast = std::max<uint8_t>(g_dirtyStates.vertexStreamLast, index);
    }
}

static void DrawPrimitive(GuestDevice* device, uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount) 
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitive;
    cmd.drawPrimitive.primitiveType = primitiveType;
    cmd.drawPrimitive.startVertex = startVertex;
    cmd.drawPrimitive.primitiveCount = primitiveCount;

    queue.submit();
}

static void ProcDrawPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitive;

    SetPrimitiveType(args.primitiveType);

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
    {
        auto& vertexBufferView = g_vertexBufferViews[g_pipelineState.vertexDeclaration->indexVertexStream];

        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, vertexBufferView.buffer);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, vertexBufferView.size);
        SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, RenderFormat::R32_UINT);

        UnsetInstancingStream();
    }

    FlushRenderStateForRenderThread();

    auto& commandList = g_commandLists[g_frame];

    if (indexCount > 0)
        commandList->drawIndexedInstanced(indexCount, args.primitiveCount / indexCount, 0, 0, 0);
    else
        commandList->drawInstanced(args.primitiveCount, 1, args.startVertex, 0);
}

static void DrawIndexedPrimitive(GuestDevice* device, uint32_t primitiveType, int32_t baseVertexIndex, uint32_t startIndex, uint32_t primCount)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawIndexedPrimitive;
    cmd.drawIndexedPrimitive.primitiveType = primitiveType;
    cmd.drawIndexedPrimitive.baseVertexIndex = baseVertexIndex;
    cmd.drawIndexedPrimitive.startIndex = startIndex;
    cmd.drawIndexedPrimitive.primCount = primCount;

    queue.submit();
}

static void ProcDrawIndexedPrimitive(const RenderCommand& cmd)
{
    const auto& args = cmd.drawIndexedPrimitive;

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
        UnsetInstancingStream();

    SetPrimitiveType(args.primitiveType);
    FlushRenderStateForRenderThread();

    g_commandLists[g_frame]->drawIndexedInstanced(args.primCount, 1, args.startIndex, args.baseVertexIndex, 0);
}

static void DrawPrimitiveUP(GuestDevice* device, uint32_t primitiveType, uint32_t primitiveCount, void* vertexStreamZeroData, uint32_t vertexStreamZeroStride)
{
    LocalRenderCommandQueue queue;
    FlushRenderStateForMainThread(device, queue);

    auto& cmd = queue.enqueue();
    cmd.type = RenderCommandType::DrawPrimitiveUP;
    cmd.drawPrimitiveUP.primitiveType = primitiveType;
    cmd.drawPrimitiveUP.primitiveCount = primitiveCount;
    cmd.drawPrimitiveUP.vertexStreamZeroData = g_intermediaryUploadAllocator.allocate(vertexStreamZeroData, primitiveCount * vertexStreamZeroStride);
    cmd.drawPrimitiveUP.vertexStreamZeroSize = primitiveCount * vertexStreamZeroStride;
    cmd.drawPrimitiveUP.vertexStreamZeroStride = vertexStreamZeroStride;
    cmd.drawPrimitiveUP.csdFilterState = g_csdFilterState;
    
    queue.submit();
}

static void ProcDrawPrimitiveUP(const RenderCommand& cmd)
{
    const auto& args = cmd.drawPrimitiveUP;

    uint32_t indexCount = CheckInstancing();
    if (indexCount > 0)
        UnsetInstancingStream();

    SetPrimitiveType(args.primitiveType);
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[0], uint8_t(args.vertexStreamZeroStride));

    auto allocation = g_uploadAllocators[g_frame].allocate<true>(reinterpret_cast<const uint32_t*>(args.vertexStreamZeroData), args.vertexStreamZeroSize, 0x4);

    auto& vertexBufferView = g_vertexBufferViews[0];
    vertexBufferView.size = args.primitiveCount * args.vertexStreamZeroStride;
    vertexBufferView.buffer = allocation.buffer->at(allocation.offset);
    g_inputSlots[0].stride = args.vertexStreamZeroStride;
    g_dirtyStates.vertexStreamFirst = 0;

    indexCount = 0;

    if (args.primitiveType == D3DPT_QUADLIST)
        indexCount = g_quadIndexData.prepare(args.primitiveCount);
    else if (!g_capabilities.triangleFan && args.primitiveType == D3DPT_TRIANGLEFAN)
        indexCount = g_triangleFanIndexData.prepare(args.primitiveCount);

    if (args.csdFilterState != CsdFilterState::Unknown &&
        (g_pipelineState.pixelShader == g_csdShader || g_pipelineState.pixelShader == g_csdFilterShader.get()))
    {
        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader,
            args.csdFilterState == CsdFilterState::On ? g_csdFilterShader.get() : g_csdShader);
    }

    FlushRenderStateForRenderThread();

    if (indexCount != 0)
        g_commandLists[g_frame]->drawIndexedInstanced(indexCount, 1, 0, 0, 0);
    else
        g_commandLists[g_frame]->drawInstanced(args.primitiveCount, 1, 0, 0);
}

static const char* ConvertDeclUsage(uint32_t usage)
{
    switch (usage)
    {
    case D3DDECLUSAGE_POSITION:
        return "POSITION";
    case D3DDECLUSAGE_BLENDWEIGHT:
        return "BLENDWEIGHT";
    case D3DDECLUSAGE_BLENDINDICES:
        return "BLENDINDICES";
    case D3DDECLUSAGE_NORMAL:
        return "NORMAL";
    case D3DDECLUSAGE_PSIZE:
        return "PSIZE";
    case D3DDECLUSAGE_TEXCOORD:
        return "TEXCOORD";
    case D3DDECLUSAGE_TANGENT:
        return "TANGENT";
    case D3DDECLUSAGE_BINORMAL:
        return "BINORMAL";
    case D3DDECLUSAGE_TESSFACTOR:
        return "TESSFACTOR";
    case D3DDECLUSAGE_POSITIONT:
        return "POSITIONT";
    case D3DDECLUSAGE_COLOR:
        return "COLOR";
    case D3DDECLUSAGE_FOG:
        return "FOG";
    case D3DDECLUSAGE_DEPTH:
        return "DEPTH";
    case D3DDECLUSAGE_SAMPLE:
        return "SAMPLE";
    default:
        assert(false && "Unknown usage");
        return "UNKNOWN";
    }
}

static RenderFormat ConvertDeclType(uint32_t type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1:
        return RenderFormat::R32_FLOAT;
    case D3DDECLTYPE_FLOAT2:
        return RenderFormat::R32G32_FLOAT;
    case D3DDECLTYPE_FLOAT3:
        return RenderFormat::R32G32B32_FLOAT;
    case D3DDECLTYPE_FLOAT4:
        return RenderFormat::R32G32B32A32_FLOAT;
    case D3DDECLTYPE_D3DCOLOR:
        return RenderFormat::B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4_2:
        return RenderFormat::R8G8B8A8_UINT;
    case D3DDECLTYPE_SHORT2:
        return RenderFormat::R16G16_SINT;
    case D3DDECLTYPE_SHORT4:
        return RenderFormat::R16G16B16A16_SINT;
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_UBYTE4N_2:
        return RenderFormat::R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2N:
        return RenderFormat::R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:
        return RenderFormat::R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:
        return RenderFormat::R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:
        return RenderFormat::R16G16B16A16_UNORM;
    case D3DDECLTYPE_UINT1:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_DEC3N_2:
    case D3DDECLTYPE_DEC3N_3:
        return RenderFormat::R32_UINT;
    case D3DDECLTYPE_FLOAT16_2:
        return RenderFormat::R16G16_FLOAT;
    case D3DDECLTYPE_FLOAT16_4:
        return RenderFormat::R16G16B16A16_FLOAT;
    default:
        assert(false && "Unknown type");
        return RenderFormat::UNKNOWN;
    }
}

static GuestVertexDeclaration* CreateVertexDeclarationWithoutAddRef(GuestVertexElement* vertexElements) 
{
    size_t vertexElementCount = 0;
    auto vertexElement = vertexElements;

    while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
    {
        vertexElement->padding = 0;
        ++vertexElement;
        ++vertexElementCount;
    }

    vertexElement->padding = 0; // Clear the padding in D3DDECL_END() 

    std::lock_guard lock(g_vertexDeclarationMutex);

    XXH64_hash_t hash = XXH3_64bits(vertexElements, vertexElementCount * sizeof(GuestVertexElement));
    auto& vertexDeclaration = g_vertexDeclarations[hash];

    if (vertexDeclaration == nullptr)
    {
        vertexDeclaration = g_userHeap.AllocPhysical<GuestVertexDeclaration>(ResourceType::VertexDeclaration);
        vertexDeclaration->hash = hash;

        static std::vector<RenderInputElement> inputElements;
        inputElements.clear();

        struct Location
        {
            uint32_t usage;
            uint32_t usageIndex;
            uint32_t location;
        };

        constexpr Location locations[] =
        {
            { D3DDECLUSAGE_POSITION, 0, 0 },
            { D3DDECLUSAGE_NORMAL, 0, 1 },
            { D3DDECLUSAGE_TANGENT, 0, 2 },
            { D3DDECLUSAGE_BINORMAL, 0, 3 },
            { D3DDECLUSAGE_TEXCOORD, 0, 4 },
            { D3DDECLUSAGE_TEXCOORD, 1, 5 },
            { D3DDECLUSAGE_TEXCOORD, 2, 6 },
            { D3DDECLUSAGE_TEXCOORD, 3, 7 },
            { D3DDECLUSAGE_COLOR, 0, 8 },
            { D3DDECLUSAGE_BLENDINDICES, 0, 9 },
            { D3DDECLUSAGE_BLENDWEIGHT, 0, 10 },
            { D3DDECLUSAGE_COLOR, 1, 11 },
            { D3DDECLUSAGE_TEXCOORD, 4, 12 },
            { D3DDECLUSAGE_TEXCOORD, 5, 13 },
            { D3DDECLUSAGE_TEXCOORD, 6, 14 },
            { D3DDECLUSAGE_TEXCOORD, 7, 15 },
            { D3DDECLUSAGE_POSITION, 1, 15 }
        };

        vertexElement = vertexElements;
        while (vertexElement->stream != 0xFF && vertexElement->type != D3DDECLTYPE_UNUSED)
        {
            if (vertexElement->usage == D3DDECLUSAGE_POSITION && vertexElement->usageIndex == 2)
            {
                ++vertexElement;
                continue;
            }

            auto& inputElement = inputElements.emplace_back();
            
            inputElement.semanticName = ConvertDeclUsage(vertexElement->usage);
            inputElement.semanticIndex = vertexElement->usageIndex;
            inputElement.location = ~0;

            for (auto& location : locations)
            {
                if (location.usage == vertexElement->usage && location.usageIndex == vertexElement->usageIndex)
                {
                    inputElement.location = location.location;
                    break;
                }
            }

            assert(inputElement.location != ~0);

            inputElement.format = ConvertDeclType(vertexElement->type);
            inputElement.slotIndex = vertexElement->stream;
            inputElement.alignedByteOffset = vertexElement->offset;

            switch (vertexElement->usage)
            {
            case D3DDECLUSAGE_POSITION:
                if (vertexElement->usageIndex == 1)
                    vertexDeclaration->indexVertexStream = vertexElement->stream;
                break;

            case D3DDECLUSAGE_NORMAL:
            case D3DDECLUSAGE_TANGENT:
            case D3DDECLUSAGE_BINORMAL:
                if (vertexElement->type == D3DDECLTYPE_FLOAT3)
                    inputElement.format = RenderFormat::R32G32B32_UINT;
                else
                    vertexDeclaration->hasR11G11B10Normal = true;
                break;

            case D3DDECLUSAGE_TEXCOORD:
                switch (vertexElement->type)
                {
                case D3DDECLTYPE_SHORT2:
                case D3DDECLTYPE_SHORT4:
                case D3DDECLTYPE_SHORT2N:
                case D3DDECLTYPE_SHORT4N:
                case D3DDECLTYPE_USHORT2N:
                case D3DDECLTYPE_USHORT4N:
                case D3DDECLTYPE_FLOAT16_2:
                case D3DDECLTYPE_FLOAT16_4:
                    vertexDeclaration->swappedTexcoords |= 1 << vertexElement->usageIndex;
                    break;
                }

                break;
            }

            vertexDeclaration->vertexStreams[vertexElement->stream] = true;

            ++vertexElement;
        }

        auto addInputElement = [&](uint32_t usage, uint32_t usageIndex)
            {
                uint32_t location = ~0;

                for (auto& alsoLocation : locations)
                {
                    if (alsoLocation.usage == usage && alsoLocation.usageIndex == usageIndex)
                    {
                        location = alsoLocation.location;
                        break;
                    }
                }

                assert(location != ~0);

                for (auto& inputElement : inputElements)
                {
                    if (inputElement.location == location)
                        return;
                }

                auto format = RenderFormat::R32_FLOAT;
                switch (usage)
                {
                case D3DDECLUSAGE_NORMAL:
                case D3DDECLUSAGE_TANGENT:
                case D3DDECLUSAGE_BINORMAL:
                case D3DDECLUSAGE_BLENDINDICES:
                    format = RenderFormat::R32_UINT;
                    break;
                }

                inputElements.emplace_back(ConvertDeclUsage(usage), usageIndex, location, format, 15, 0);
            };

        addInputElement(D3DDECLUSAGE_POSITION, 0);
        addInputElement(D3DDECLUSAGE_NORMAL, 0);
        addInputElement(D3DDECLUSAGE_TANGENT, 0);
        addInputElement(D3DDECLUSAGE_BINORMAL, 0);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 0);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 1);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 2);
        addInputElement(D3DDECLUSAGE_TEXCOORD, 3);
        addInputElement(D3DDECLUSAGE_COLOR, 0);
        addInputElement(D3DDECLUSAGE_BLENDWEIGHT, 0);
        addInputElement(D3DDECLUSAGE_BLENDINDICES, 0);

        vertexDeclaration->inputElements = std::make_unique<RenderInputElement[]>(inputElements.size());
        std::copy(inputElements.begin(), inputElements.end(), vertexDeclaration->inputElements.get());

        vertexDeclaration->vertexElements = std::make_unique<GuestVertexElement[]>(vertexElementCount + 1);
        std::copy(vertexElements, vertexElements + vertexElementCount + 1, vertexDeclaration->vertexElements.get());

        vertexDeclaration->inputElementCount = uint32_t(inputElements.size());
        vertexDeclaration->vertexElementCount = vertexElementCount + 1;
    }

    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static GuestVertexDeclaration* CreateVertexDeclaration(GuestVertexElement* vertexElements)
{
    auto vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
    vertexDeclaration->AddRef();
    return vertexDeclaration;
}

static void SetVertexDeclaration(GuestDevice* device, GuestVertexDeclaration* vertexDeclaration) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexDeclaration;
    cmd.setVertexDeclaration.vertexDeclaration = vertexDeclaration;
    g_renderQueue.enqueue(cmd);

    device->vertexDeclaration = g_memory.MapVirtual(vertexDeclaration);
}

static void ProcSetVertexDeclaration(const RenderCommand& cmd)
{
    auto& args = cmd.setVertexDeclaration;

    if (args.vertexDeclaration != nullptr)
    {
        SetDirtyValue(g_dirtyStates.sharedConstants, g_sharedConstants.swappedTexcoords, args.vertexDeclaration->swappedTexcoords);

        uint32_t specConstants = g_pipelineState.specConstants;
        if (args.vertexDeclaration->hasR11G11B10Normal)
            specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;
        else
            specConstants &= ~SPEC_CONSTANT_R11G11B10_NORMAL;

        SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.specConstants, specConstants);
    }
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexDeclaration, args.vertexDeclaration);
}

static ShaderCacheEntry* FindShaderCacheEntry(XXH64_hash_t hash)
{
    auto end = g_shaderCacheEntries + g_shaderCacheEntryCount;
    auto findResult = std::lower_bound(g_shaderCacheEntries, end, hash, [](ShaderCacheEntry& lhs, XXH64_hash_t rhs)
        {
            return lhs.hash < rhs;
        });

    return findResult != end && findResult->hash == hash ? findResult : nullptr;
}

static GuestShader* CreateShader(const be<uint32_t>* function, ResourceType resourceType)
{
    XXH64_hash_t hash = XXH3_64bits(function, function[1] + function[2]);

    auto findResult = FindShaderCacheEntry(hash);
    GuestShader* shader = nullptr;

    if (findResult != nullptr)
    {
        if (findResult->guestShader == nullptr)
        {
            shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);

            if (hash == 0x85ED723035ECF535)
                shader->shader = CREATE_SHADER(blend_color_alpha_ps);
            else if (hash == 0xB1086A4947A797DE)
                shader->shader = CREATE_SHADER(csd_no_tex_vs);
            else if (hash == 0xB4CAFC034A37C8A8)
                shader->shader = CREATE_SHADER(csd_vs);
            else
                shader->shaderCacheEntry = findResult;

            findResult->guestShader = shader;
        }
        else
        {
            shader = findResult->guestShader;
        }
    }

    if (shader == nullptr)
        shader = g_userHeap.AllocPhysical<GuestShader>(resourceType);
    else
        shader->AddRef();

    if (hash == 0x31173204A896098A)
        g_csdShader = shader;

    return shader;
}

static GuestShader* CreateVertexShader(const be<uint32_t>* function) 
{
    return CreateShader(function, ResourceType::VertexShader);
}

static void SetVertexShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetVertexShader;
    cmd.setVertexShader.shader = shader;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetVertexShader(const RenderCommand& cmd)
{
    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexShader, cmd.setVertexShader.shader);
}

static void SetStreamSource(GuestDevice* device, uint32_t index, GuestBuffer* buffer, uint32_t offset, uint32_t stride) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetStreamSource;
    cmd.setStreamSource.index = index;
    cmd.setStreamSource.buffer = buffer;
    cmd.setStreamSource.offset = offset;
    cmd.setStreamSource.stride = stride;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetStreamSource(const RenderCommand& cmd)
{
    const auto& args = cmd.setStreamSource;

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.vertexStrides[args.index], uint8_t(args.buffer != nullptr ? args.stride : 0));

    bool dirty = false;

    SetDirtyValue(dirty, g_vertexBufferViews[args.index].buffer, args.buffer != nullptr ? args.buffer->buffer->at(args.offset) : RenderBufferReference{});
    SetDirtyValue(dirty, g_vertexBufferViews[args.index].size, args.buffer != nullptr ? (args.buffer->dataSize - args.offset) : 0u);
    SetDirtyValue(dirty, g_inputSlots[args.index].stride, args.buffer != nullptr ? args.stride : 0u);

    if (dirty)
    {
        g_dirtyStates.vertexStreamFirst = std::min<uint8_t>(g_dirtyStates.vertexStreamFirst, args.index);
        g_dirtyStates.vertexStreamLast = std::max<uint8_t>(g_dirtyStates.vertexStreamLast, args.index);
    }
}

static void SetIndices(GuestDevice* device, GuestBuffer* buffer) 
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetIndices;
    cmd.setIndices.buffer = buffer;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetIndices(const RenderCommand& cmd)
{
    const auto& args = cmd.setIndices;

    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.buffer, args.buffer != nullptr ? args.buffer->buffer->at(0) : RenderBufferReference{});
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.format, args.buffer != nullptr ? args.buffer->format : RenderFormat::R16_UINT);
    SetDirtyValue(g_dirtyStates.indices, g_indexBufferView.size, args.buffer != nullptr ? args.buffer->dataSize : 0u);
}

static GuestShader* CreatePixelShader(const be<uint32_t>* function)
{
    return CreateShader(function, ResourceType::PixelShader);
}

static void SetPixelShader(GuestDevice* device, GuestShader* shader)
{
    RenderCommand cmd;
    cmd.type = RenderCommandType::SetPixelShader;
    cmd.setPixelShader.shader = shader;
    g_renderQueue.enqueue(cmd);
}

static void ProcSetPixelShader(const RenderCommand& cmd)
{
    GuestShader* shader = cmd.setPixelShader.shader;
    if (shader != nullptr && 
        shader->shaderCacheEntry != nullptr)
    {
        if (shader->shaderCacheEntry->hash == 0x4294510C775F4EE8)
        {
            size_t shaderIndex = GAUSSIAN_BLUR_3X3;

            switch (Config::DepthOfFieldQuality)
            {
            case EDepthOfFieldQuality::Low:
                shaderIndex = GAUSSIAN_BLUR_3X3;
                break;

            case EDepthOfFieldQuality::Medium:
                shaderIndex = GAUSSIAN_BLUR_5X5;
                break;

            case EDepthOfFieldQuality::High:
                shaderIndex = GAUSSIAN_BLUR_7X7;
                break;

            case EDepthOfFieldQuality::Ultra:
                shaderIndex = GAUSSIAN_BLUR_9X9;
                break;

            default:
            {
                if (g_aspectRatio >= WIDE_ASPECT_RATIO)
                {
                    size_t height = round(Video::s_viewportHeight * Config::ResolutionScale);

                    if (height > 1440)
                        shaderIndex = GAUSSIAN_BLUR_9X9;
                    else if (height > 1080)
                        shaderIndex = GAUSSIAN_BLUR_7X7;
                    else if (height > 720)
                        shaderIndex = GAUSSIAN_BLUR_5X5;
                    else
                        shaderIndex = GAUSSIAN_BLUR_3X3;
                }
                else
                {
                    // Narrow aspect ratios should check for width to account for VERT+.
                    size_t width = round(Video::s_viewportWidth * Config::ResolutionScale);

                    if (width > 2560)
                        shaderIndex = GAUSSIAN_BLUR_9X9;
                    else if (width > 1920)
                        shaderIndex = GAUSSIAN_BLUR_7X7;
                    else if (width > 1280)
                        shaderIndex = GAUSSIAN_BLUR_5X5;
                    else
                        shaderIndex = GAUSSIAN_BLUR_3X3;
                }

                break;
            }
            }

            shader = g_gaussianBlurShaders[shaderIndex].get();
        }
        else if (shader->shaderCacheEntry->hash == 0x6B9732B4CD7E7740 && Config::MotionBlur == EMotionBlur::Enhanced)
        {
            shader = g_enhancedMotionBlurShader.get();
        }
    }

    SetDirtyValue(g_dirtyStates.pipelineState, g_pipelineState.pixelShader, shader);
}

static std::thread g_renderThread([]
    {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        GuestThread::SetThreadName(GetCurrentThreadId(), "Render Thread");
#endif

        RenderCommand commands[32];

        while (true)
        {
            size_t count = g_renderQueue.wait_dequeue_bulk(commands, std::size(commands));

            for (size_t i = 0; i < count; i++)
            {
                auto& cmd = commands[i];
                switch (cmd.type)
                {
                case RenderCommandType::SetRenderState:                    ProcSetRenderState(cmd); break;
                case RenderCommandType::DestructResource:                  ProcDestructResource(cmd); break;
                case RenderCommandType::UnlockTextureRect:                 ProcUnlockTextureRect(cmd); break;
                case RenderCommandType::UnlockBuffer16:                    ProcUnlockBuffer16(cmd); break;
                case RenderCommandType::UnlockBuffer32:                    ProcUnlockBuffer32(cmd); break;
                case RenderCommandType::DrawImGui:                         ProcDrawImGui(cmd); break;
                case RenderCommandType::ExecuteCommandList:                ProcExecuteCommandList(cmd); break;
                case RenderCommandType::BeginCommandList:                  ProcBeginCommandList(cmd); break;
                case RenderCommandType::StretchRect:                       ProcStretchRect(cmd); break;
                case RenderCommandType::SetRenderTarget:                   ProcSetRenderTarget(cmd); break;
                case RenderCommandType::SetDepthStencilSurface:            ProcSetDepthStencilSurface(cmd); break;
                case RenderCommandType::ExecutePendingStretchRectCommands: ProcExecutePendingStretchRectCommands(cmd); break;
                case RenderCommandType::Clear:                             ProcClear(cmd); break;
                case RenderCommandType::SetViewport:                       ProcSetViewport(cmd); break;
                case RenderCommandType::SetTexture:                        ProcSetTexture(cmd); break;
                case RenderCommandType::SetScissorRect:                    ProcSetScissorRect(cmd); break;
                case RenderCommandType::SetSamplerState:                   ProcSetSamplerState(cmd); break;
                case RenderCommandType::SetBooleans:                       ProcSetBooleans(cmd); break;
                case RenderCommandType::SetVertexShaderConstants:          ProcSetVertexShaderConstants(cmd); break;
                case RenderCommandType::SetPixelShaderConstants:           ProcSetPixelShaderConstants(cmd); break;
                case RenderCommandType::AddPipeline:                       ProcAddPipeline(cmd); break;
                case RenderCommandType::DrawPrimitive:                     ProcDrawPrimitive(cmd); break;
                case RenderCommandType::DrawIndexedPrimitive:              ProcDrawIndexedPrimitive(cmd); break;
                case RenderCommandType::DrawPrimitiveUP:                   ProcDrawPrimitiveUP(cmd); break;
                case RenderCommandType::SetVertexDeclaration:              ProcSetVertexDeclaration(cmd); break;
                case RenderCommandType::SetVertexShader:                   ProcSetVertexShader(cmd); break;
                case RenderCommandType::SetStreamSource:                   ProcSetStreamSource(cmd); break;
                case RenderCommandType::SetIndices:                        ProcSetIndices(cmd); break;
                case RenderCommandType::SetPixelShader:                    ProcSetPixelShader(cmd); break;
                default:                                                   assert(false && "Unrecognized render command type."); break;
                }
            }
        }
    });

static void D3DXFillTexture(GuestTexture* texture, uint32_t function, void* data)
{
    if (texture->width == 1 && texture->height == 1 && texture->format == RenderFormat::R8_UNORM && function == 0x82BA2150)
    {
        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(PLACEMENT_ALIGNMENT));

        uint8_t* mappedData = reinterpret_cast<uint8_t*>(uploadBuffer->map());
        *mappedData = 0xFF;
        uploadBuffer->unmap();

        ExecuteCopyCommandList([&]
            {
                g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture->texture, RenderTextureLayout::COPY_DEST));

                g_copyCommandList->copyTextureRegion(
                    RenderTextureCopyLocation::Subresource(texture->texture, 0),
                    RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, 1, 1, 1, PLACEMENT_ALIGNMENT, 0));
            });

        texture->layout = RenderTextureLayout::COPY_DEST;
    }
}

static void D3DXFillVolumeTexture(GuestTexture* texture, uint32_t function, void* data)
{
    uint32_t rowPitch0 = (texture->width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch0 = (rowPitch0 * texture->height * texture->depth + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);

    uint32_t rowPitch1 = ((texture->width / 2) * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
    uint32_t slicePitch1 = (rowPitch1 * (texture->height / 2) * (texture->depth / 2) + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);

    auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch0 + slicePitch1));
    uint8_t* mappedData = reinterpret_cast<uint8_t*>(uploadBuffer->map());

    thread_local std::vector<float> mipData;
    mipData.resize((texture->width / 2) * (texture->height / 2) * (texture->depth / 2) * 4);
    memset(mipData.data(), 0, mipData.size() * sizeof(float));

    for (size_t z = 0; z < texture->depth; z++)
    {
        for (size_t y = 0; y < texture->height; y++)
        {
            for (size_t x = 0; x < texture->width; x++)
            {
                auto dest = mappedData + z * rowPitch0 * texture->height + y * rowPitch0 + x * sizeof(uint32_t);
                size_t index = z * texture->width * texture->height + y * texture->width + x;
                size_t mipIndex = ((z / 2) * (texture->width / 2) * (texture->height / 2) + (y / 2) * (texture->width / 2) + x / 2) * 4;

                if (function == 0x82BC7820)
                {
                    auto src = reinterpret_cast<be<float>*>(data) + index * 4;

                    float r = static_cast<uint8_t>(src[0] * 255.0f);
                    float g = static_cast<uint8_t>(src[1] * 255.0f);
                    float b = static_cast<uint8_t>(src[2] * 255.0f);
                    float a = static_cast<uint8_t>(src[3] * 255.0f);

                    dest[0] = r;
                    dest[1] = g;
                    dest[2] = b;
                    dest[3] = a;

                    mipData[mipIndex + 0] += r;
                    mipData[mipIndex + 1] += g;
                    mipData[mipIndex + 2] += b;
                    mipData[mipIndex + 3] += a;
                }
                else if (function == 0x82BC78A8)
                {
                    auto src = reinterpret_cast<uint8_t*>(data) + index * 4;

                    dest[0] = src[3];
                    dest[1] = src[2];
                    dest[2] = src[1];
                    dest[3] = src[0];

                    mipData[mipIndex + 0] += src[3];
                    mipData[mipIndex + 1] += src[2];
                    mipData[mipIndex + 2] += src[1];
                    mipData[mipIndex + 3] += src[0];
                }
            }
        }
    }

    for (size_t z = 0; z < texture->depth / 2; z++)
    {
        for (size_t y = 0; y < texture->height / 2; y++)
        {
            for (size_t x = 0; x < texture->width / 2; x++)
            {
                auto dest = mappedData + slicePitch0 + z * rowPitch1 * (texture->height / 2) + y * rowPitch1 + x * sizeof(uint32_t);
                size_t index = (z * (texture->width / 2) * (texture->height / 2) + y * (texture->width / 2) + x) * 4;

                dest[0] = static_cast<uint8_t>(mipData[index + 0] / 8.0f);
                dest[1] = static_cast<uint8_t>(mipData[index + 1] / 8.0f);
                dest[2] = static_cast<uint8_t>(mipData[index + 2] / 8.0f);
                dest[3] = static_cast<uint8_t>(mipData[index + 3] / 8.0f);
            }
        }
    }

    uploadBuffer->unmap();

    ExecuteCopyCommandList([&]
        {
            g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture->texture, RenderTextureLayout::COPY_DEST));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(texture->texture, 0),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, texture->width, texture->height, texture->depth, rowPitch0 / RenderFormatSize(texture->format), 0));

            g_copyCommandList->copyTextureRegion(
                RenderTextureCopyLocation::Subresource(texture->texture, 1),
                RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), texture->format, texture->width / 2, texture->height / 2, texture->depth / 2, rowPitch1 / RenderFormatSize(texture->format), slicePitch0));
        });

    texture->layout = RenderTextureLayout::COPY_DEST;
}

struct GuestPictureData
{
    be<uint32_t> vtable;
    uint8_t flags;
    be<uint32_t> name;
    be<uint32_t> texture;
    be<uint32_t> type;
};

static RenderTextureDimension ConvertTextureDimension(ddspp::TextureType type)
{
    switch (type) 
    {
    case ddspp::Texture1D:
        return RenderTextureDimension::TEXTURE_1D;
    case ddspp::Texture2D:
    case ddspp::Cubemap:
        return RenderTextureDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureDimension::TEXTURE_3D;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureDimension::UNKNOWN;
    }
}

static RenderTextureViewDimension ConvertTextureViewDimension(ddspp::TextureType type)
{
    switch (type)
    {
    case ddspp::Texture1D:
        return RenderTextureViewDimension::TEXTURE_1D;
    case ddspp::Texture2D:
        return RenderTextureViewDimension::TEXTURE_2D;
    case ddspp::Texture3D:
        return RenderTextureViewDimension::TEXTURE_3D;
    case ddspp::Cubemap:
        return RenderTextureViewDimension::TEXTURE_CUBE;
    default:
        assert(false && "Unknown texture type from DDS.");
        return RenderTextureViewDimension::UNKNOWN;
    }
}

static RenderFormat ConvertDXGIFormat(ddspp::DXGIFormat format) 
{
    switch (format)
    {
    case ddspp::R32G32B32A32_TYPELESS:
        return RenderFormat::R32G32B32A32_TYPELESS;
    case ddspp::R32G32B32A32_FLOAT:
        return RenderFormat::R32G32B32A32_FLOAT;
    case ddspp::R32G32B32A32_UINT:
        return RenderFormat::R32G32B32A32_UINT;
    case ddspp::R32G32B32A32_SINT:
        return RenderFormat::R32G32B32A32_SINT;
    case ddspp::R32G32B32_TYPELESS:
        return RenderFormat::R32G32B32_TYPELESS;
    case ddspp::R32G32B32_FLOAT:
        return RenderFormat::R32G32B32_FLOAT;
    case ddspp::R32G32B32_UINT:
        return RenderFormat::R32G32B32_UINT;
    case ddspp::R32G32B32_SINT:
        return RenderFormat::R32G32B32_SINT;
    case ddspp::R16G16B16A16_TYPELESS:
        return RenderFormat::R16G16B16A16_TYPELESS;
    case ddspp::R16G16B16A16_FLOAT:
        return RenderFormat::R16G16B16A16_FLOAT;
    case ddspp::R16G16B16A16_UNORM:
        return RenderFormat::R16G16B16A16_UNORM;
    case ddspp::R16G16B16A16_UINT:
        return RenderFormat::R16G16B16A16_UINT;
    case ddspp::R16G16B16A16_SNORM:
        return RenderFormat::R16G16B16A16_SNORM;
    case ddspp::R16G16B16A16_SINT:
        return RenderFormat::R16G16B16A16_SINT;
    case ddspp::R32G32_TYPELESS:
        return RenderFormat::R32G32_TYPELESS;
    case ddspp::R32G32_FLOAT:
        return RenderFormat::R32G32_FLOAT;
    case ddspp::R32G32_UINT:
        return RenderFormat::R32G32_UINT;
    case ddspp::R32G32_SINT:
        return RenderFormat::R32G32_SINT;
    case ddspp::R8G8B8A8_TYPELESS:
        return RenderFormat::R8G8B8A8_TYPELESS;
    case ddspp::R8G8B8A8_UNORM:
        return RenderFormat::R8G8B8A8_UNORM;
    case ddspp::R8G8B8A8_UINT:
        return RenderFormat::R8G8B8A8_UINT;
    case ddspp::R8G8B8A8_SNORM:
        return RenderFormat::R8G8B8A8_SNORM;
    case ddspp::R8G8B8A8_SINT:
        return RenderFormat::R8G8B8A8_SINT;
    case ddspp::B8G8R8A8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;
    case ddspp::B8G8R8X8_UNORM:
        return RenderFormat::B8G8R8A8_UNORM;   
    case ddspp::R16G16_TYPELESS:
        return RenderFormat::R16G16_TYPELESS;
    case ddspp::R16G16_FLOAT:
        return RenderFormat::R16G16_FLOAT;
    case ddspp::R16G16_UNORM:
        return RenderFormat::R16G16_UNORM;
    case ddspp::R16G16_UINT:
        return RenderFormat::R16G16_UINT;
    case ddspp::R16G16_SNORM:
        return RenderFormat::R16G16_SNORM;
    case ddspp::R16G16_SINT:
        return RenderFormat::R16G16_SINT;
    case ddspp::R32_TYPELESS:
        return RenderFormat::R32_TYPELESS;
    case ddspp::D32_FLOAT:
        return RenderFormat::D32_FLOAT;
    case ddspp::R32_FLOAT:
        return RenderFormat::R32_FLOAT;
    case ddspp::R32_UINT:
        return RenderFormat::R32_UINT;
    case ddspp::R32_SINT:
        return RenderFormat::R32_SINT;
    case ddspp::R8G8_TYPELESS:
        return RenderFormat::R8G8_TYPELESS;
    case ddspp::R8G8_UNORM:
        return RenderFormat::R8G8_UNORM;
    case ddspp::R8G8_UINT:
        return RenderFormat::R8G8_UINT;
    case ddspp::R8G8_SNORM:
        return RenderFormat::R8G8_SNORM;
    case ddspp::R8G8_SINT:
        return RenderFormat::R8G8_SINT;
    case ddspp::R16_TYPELESS:
        return RenderFormat::R16_TYPELESS;
    case ddspp::R16_FLOAT:
        return RenderFormat::R16_FLOAT;
    case ddspp::D16_UNORM:
        return RenderFormat::D16_UNORM;
    case ddspp::R16_UNORM:
        return RenderFormat::R16_UNORM;
    case ddspp::R16_UINT:
        return RenderFormat::R16_UINT;
    case ddspp::R16_SNORM:
        return RenderFormat::R16_SNORM;
    case ddspp::R16_SINT:
        return RenderFormat::R16_SINT;
    case ddspp::R8_TYPELESS:
        return RenderFormat::R8_TYPELESS;
    case ddspp::R8_UNORM:
        return RenderFormat::R8_UNORM;
    case ddspp::R8_UINT:
        return RenderFormat::R8_UINT;
    case ddspp::R8_SNORM:
        return RenderFormat::R8_SNORM;
    case ddspp::R8_SINT:
        return RenderFormat::R8_SINT;
    case ddspp::BC1_TYPELESS:
        return RenderFormat::BC1_TYPELESS;
    case ddspp::BC1_UNORM:
        return RenderFormat::BC1_UNORM;
    case ddspp::BC1_UNORM_SRGB:
        return RenderFormat::BC1_UNORM_SRGB;
    case ddspp::BC2_TYPELESS:
        return RenderFormat::BC2_TYPELESS;
    case ddspp::BC2_UNORM:
        return RenderFormat::BC2_UNORM;
    case ddspp::BC2_UNORM_SRGB:
        return RenderFormat::BC2_UNORM_SRGB;
    case ddspp::BC3_TYPELESS:
        return RenderFormat::BC3_TYPELESS;
    case ddspp::BC3_UNORM:
        return RenderFormat::BC3_UNORM;
    case ddspp::BC3_UNORM_SRGB:
        return RenderFormat::BC3_UNORM_SRGB;
    case ddspp::BC4_TYPELESS:
        return RenderFormat::BC4_TYPELESS;
    case ddspp::BC4_UNORM:
        return RenderFormat::BC4_UNORM;
    case ddspp::BC4_SNORM:
        return RenderFormat::BC4_SNORM;
    case ddspp::BC5_TYPELESS:
        return RenderFormat::BC5_TYPELESS;
    case ddspp::BC5_UNORM:
        return RenderFormat::BC5_UNORM;
    case ddspp::BC5_SNORM:
        return RenderFormat::BC5_SNORM;
    case ddspp::BC6H_TYPELESS:
        return RenderFormat::BC6H_TYPELESS;
    case ddspp::BC6H_UF16:
        return RenderFormat::BC6H_UF16;
    case ddspp::BC6H_SF16:
        return RenderFormat::BC6H_SF16;
    case ddspp::BC7_TYPELESS:
        return RenderFormat::BC7_TYPELESS;
    case ddspp::BC7_UNORM:
        return RenderFormat::BC7_UNORM;
    case ddspp::BC7_UNORM_SRGB:
        return RenderFormat::BC7_UNORM_SRGB;
    default:
        assert(false && "Unsupported format from DDS.");
        return RenderFormat::UNKNOWN;
    }
}

static bool LoadTexture(GuestTexture& texture, const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping, bool forceCubeMap = false)
{
    ddspp::Descriptor ddsDesc;
    if (ddspp::decode_header((unsigned char *)(data), ddsDesc) != ddspp::Error)
    {
        forceCubeMap &= (ddsDesc.type == ddspp::Texture2D) && (ddsDesc.arraySize == 1);
        uint32_t arraySize = ddsDesc.type == ddspp::TextureType::Cubemap ? (ddsDesc.arraySize * 6) : ddsDesc.arraySize;
            
        RenderTextureDesc desc;
        desc.dimension = ConvertTextureDimension(ddsDesc.type);
        desc.width = ddsDesc.width;
        desc.height = ddsDesc.height;
        desc.depth = ddsDesc.depth;
        desc.mipLevels = ddsDesc.numMips;
        desc.arraySize = arraySize;
        desc.format = ConvertDXGIFormat(ddsDesc.format);
        desc.flags = ddsDesc.type == ddspp::TextureType::Cubemap ? RenderTextureFlag::CUBE : RenderTextureFlag::NONE;

        if (forceCubeMap)
        {
            desc.arraySize = 6;
            desc.flags = RenderTextureFlag::CUBE;
        }

        texture.textureHolder = g_device->createTexture(desc);
        texture.texture = texture.textureHolder.get();
        texture.layout = RenderTextureLayout::COPY_DEST;

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.dimension = ConvertTextureViewDimension(ddsDesc.type);
        viewDesc.mipLevels = ddsDesc.numMips;
        viewDesc.componentMapping = componentMapping;

        if (forceCubeMap)
            viewDesc.dimension = RenderTextureViewDimension::TEXTURE_CUBE;

        texture.textureView = texture.texture->createTextureView(viewDesc);
        texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
        g_textureDescriptorSet->setTexture(texture.descriptorIndex, texture.texture, RenderTextureLayout::SHADER_READ, texture.textureView.get());

        texture.width = ddsDesc.width;
        texture.height = ddsDesc.height;
        texture.viewDimension = viewDesc.dimension;

        struct Slice
        {
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            uint32_t srcOffset;
            uint32_t dstOffset;
            uint32_t srcRowPitch;
            uint32_t dstRowPitch;
            uint32_t rowCount;
        };

        std::vector<Slice> slices;
        uint32_t curSrcOffset = 0;
        uint32_t curDstOffset = 0;

        for (uint32_t arraySlice = 0; arraySlice < arraySize; arraySlice++)
        {
            for (uint32_t mipSlice = 0; mipSlice < ddsDesc.numMips; mipSlice++)
            {
                auto& slice = slices.emplace_back();

                slice.width = std::max(1u, ddsDesc.width >> mipSlice);
                slice.height = std::max(1u, ddsDesc.height >> mipSlice);
                slice.depth = std::max(1u, ddsDesc.depth >> mipSlice);
                slice.srcOffset = curSrcOffset;
                slice.dstOffset = curDstOffset;
                uint32_t rowPitch = ((slice.width + ddsDesc.blockWidth - 1) / ddsDesc.blockWidth) * ddsDesc.bitsPerPixelOrBlock;
                slice.srcRowPitch = (rowPitch + 7) / 8;
                slice.dstRowPitch = (slice.srcRowPitch + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
                slice.rowCount = (slice.height + ddsDesc.blockHeight - 1) / ddsDesc.blockHeight;

                curSrcOffset += slice.srcRowPitch * slice.rowCount * slice.depth;
                curDstOffset += (slice.dstRowPitch * slice.rowCount * slice.depth + PLACEMENT_ALIGNMENT - 1) & ~(PLACEMENT_ALIGNMENT - 1);
            }
        }

        auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(curDstOffset));
        uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

        for (auto& slice : slices)
        {
            const uint8_t* srcData = data + ddsDesc.headerSize + slice.srcOffset;
            uint8_t* dstData = mappedMemory + slice.dstOffset;

            if (slice.srcRowPitch == slice.dstRowPitch)
            {
                memcpy(dstData, srcData, slice.srcRowPitch * slice.rowCount * slice.depth);
            }
            else
            {
                for (size_t i = 0; i < slice.rowCount * slice.depth; i++)
                {
                    memcpy(dstData, srcData, slice.srcRowPitch);
                    srcData += slice.srcRowPitch;
                    dstData += slice.dstRowPitch;
                }
            }
        }

        uploadBuffer->unmap();

        ExecuteCopyCommandList([&]
            {
                g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                auto copyTextureRegion = [&](Slice& slice, uint32_t subresourceIndex)
                    {
                        g_copyCommandList->copyTextureRegion(
                            RenderTextureCopyLocation::Subresource(texture.texture, subresourceIndex % ddsDesc.numMips, subresourceIndex / ddsDesc.numMips),
                            RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), desc.format, slice.width, slice.height, slice.depth, (slice.dstRowPitch * 8) / ddsDesc.bitsPerPixelOrBlock * ddsDesc.blockWidth, slice.dstOffset));
                    };

                for (size_t i = 0; i < slices.size(); i++)
                    copyTextureRegion(slices[i], i);

                // Duplicate the first face across the remaining 6 faces.
                if (forceCubeMap)
                {
                    for (size_t i = 1; i < 6; i++)
                    {
                        for (size_t j = 0; j < slices.size(); j++)
                            copyTextureRegion(slices[j], (slices.size() * i) + j);
                    }
                }
            });

        return true;
    }
    else
    {
        int width, height;
        void* stbImage = stbi_load_from_memory(data, dataSize, &width, &height, nullptr, 4);

        if (stbImage != nullptr)
        {
            texture.textureHolder = g_device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, RenderFormat::R8G8B8A8_UNORM));
            texture.texture = texture.textureHolder.get();
            texture.viewDimension = RenderTextureViewDimension::TEXTURE_2D;
            texture.layout = RenderTextureLayout::COPY_DEST;

            texture.descriptorIndex = g_textureDescriptorAllocator.allocate();
            g_textureDescriptorSet->setTexture(texture.descriptorIndex, texture.texture, RenderTextureLayout::SHADER_READ);

            uint32_t rowPitch = (width * 4 + PITCH_ALIGNMENT - 1) & ~(PITCH_ALIGNMENT - 1);
            uint32_t slicePitch = rowPitch * height;

            auto uploadBuffer = g_device->createBuffer(RenderBufferDesc::UploadBuffer(slicePitch));
            uint8_t* mappedMemory = reinterpret_cast<uint8_t*>(uploadBuffer->map());

            if (rowPitch == (width * 4))
            {
                memcpy(mappedMemory, stbImage, slicePitch);
            }
            else
            {
                auto data = reinterpret_cast<const uint8_t*>(stbImage);

                for (size_t i = 0; i < height; i++)
                {
                    memcpy(mappedMemory, data, width * 4);
                    data += width * 4;
                    mappedMemory += rowPitch;
                }
            }

            uploadBuffer->unmap();

            stbi_image_free(stbImage);

            ExecuteCopyCommandList([&]
                {
                    g_copyCommandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(texture.texture, RenderTextureLayout::COPY_DEST));

                    g_copyCommandList->copyTextureRegion(
                        RenderTextureCopyLocation::Subresource(texture.texture, 0),
                        RenderTextureCopyLocation::PlacedFootprint(uploadBuffer.get(), RenderFormat::R8G8B8A8_UNORM, width, height, 1, rowPitch / 4, 0));
                });

            return true;
        }
    }

    return false;
}

std::unique_ptr<GuestTexture> LoadTexture(const uint8_t* data, size_t dataSize, RenderComponentMapping componentMapping)
{
    GuestTexture texture(ResourceType::Texture);

    if (LoadTexture(texture, data, dataSize, componentMapping))
        return std::make_unique<GuestTexture>(std::move(texture));

    return nullptr;
}

static void DiffPatchTexture(GuestTexture& texture, uint8_t* data, uint32_t dataSize, XXH64_hash_t hash)
{
    auto header = reinterpret_cast<BlockCompressionDiffPatchHeader*>(g_buttonBcDiff.get());
    auto entries = reinterpret_cast<BlockCompressionDiffPatchEntry*>(g_buttonBcDiff.get() + header->entriesOffset);
    auto end = entries + header->entryCount;
    
    auto findResult = std::lower_bound(entries, end, hash, [](BlockCompressionDiffPatchEntry& lhs, XXH64_hash_t rhs)
        {
            return lhs.hash < rhs;
        });

    if (findResult != end && findResult->hash == hash)
    {
        auto patch = reinterpret_cast<BlockCompressionDiffPatch*>(g_buttonBcDiff.get() + findResult->patchesOffset);
        for (size_t i = 0; i < findResult->patchCount; i++)
        {
            assert(patch->destinationOffset + patch->patchBytesSize <= dataSize);
            memcpy(data + patch->destinationOffset, g_buttonBcDiff.get() + patch->patchBytesOffset, patch->patchBytesSize);
            ++patch;
        }

        GuestTexture patchedTexture(ResourceType::Texture);
        if (LoadTexture(patchedTexture, data, dataSize, {}))
            texture.patchedTexture = std::make_unique<GuestTexture>(std::move(patchedTexture));
    }
}

static void MakePictureData(GuestPictureData* pictureData, uint8_t* data, uint32_t dataSize)
{
    if ((pictureData->flags & 0x1) == 0 && data != nullptr)
    {
        GuestTexture texture(ResourceType::Texture);

        if (LoadTexture(texture, data, dataSize, {}))
        {
#ifdef _DEBUG
            texture.texture->setName(reinterpret_cast<char*>(g_memory.Translate(pictureData->name + 2)));
#endif
            XXH64_hash_t hash = XXH3_64bits(data, dataSize);

            // The whale in Cool Edge has a 2D texture assigned as a cubemap which makes it not display in recomp.
            // The hardware duplicates the first face to the remaining 6 faces, so to simulate that we'll recreate the asset.
            bool forceCubeMap = (dataSize == 0xAB38) && (hash == 0x160E9E250FDE88A9);
            if (forceCubeMap)
            {
                GuestTexture recreatedCubeMapTexture(ResourceType::Texture);
                if (LoadTexture(recreatedCubeMapTexture, data, dataSize, {}, true))
                    texture.recreatedCubeMapTexture = std::make_unique<GuestTexture>(std::move(recreatedCubeMapTexture));
            }

            DiffPatchTexture(texture, data, dataSize, hash);

            pictureData->texture = g_memory.MapVirtual(g_userHeap.AllocPhysical<GuestTexture>(std::move(texture)));
            pictureData->type = 0;
        }
    }
}

void IndexBufferLengthMidAsmHook(PPCRegister& r3)
{
    r3.u64 *= 2;
}

void SetShadowResolutionMidAsmHook(PPCRegister& r11)
{
    auto res = (int32_t)Config::ShadowResolution.Value;

    if (res > 0)
        r11.u64 = res;
}

static void SetResolution(be<uint32_t>* device)
{
    Video::ComputeViewportDimensions();

    uint32_t width = uint32_t(round(Video::s_viewportWidth * Config::ResolutionScale));
    uint32_t height = uint32_t(round(Video::s_viewportHeight * Config::ResolutionScale));
    device[46] = width == 0 ? 880 : width;
    device[47] = height == 0 ? 720 : height;
}

// The game does some weird stuff to render targets if they are above 
// 1024x1024 resolution, setting this bool at address 20 seems to avoid all that.
PPC_FUNC(sub_82E9F048)
{
    PPC_STORE_U8(ctx.r4.u32 + 20, 1);
    PPC_STORE_U32(ctx.r4.u32 + 44, PPC_LOAD_U32(ctx.r4.u32 + 8)); // Width
    PPC_STORE_U32(ctx.r4.u32 + 48, PPC_LOAD_U32(ctx.r4.u32 + 12)); // Height
}

static GuestShader* g_movieVertexShader;
static GuestShader* g_moviePixelShader;
static GuestVertexDeclaration* g_movieVertexDeclaration;

static void ScreenShaderInit(be<uint32_t>* a1, uint32_t a2, uint32_t a3, GuestVertexElement* vertexElements)
{
    if (g_moviePixelShader == nullptr)
    {
        g_moviePixelShader = g_userHeap.AllocPhysical<GuestShader>(ResourceType::PixelShader);
        g_moviePixelShader->shader = CREATE_SHADER(movie_ps);
    }

    if (g_movieVertexShader == nullptr)
    {
        g_movieVertexShader = g_userHeap.AllocPhysical<GuestShader>(ResourceType::VertexShader);
        g_movieVertexShader->shader = CREATE_SHADER(movie_vs);
    }

    if (g_movieVertexDeclaration == nullptr)
        g_movieVertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);

    g_moviePixelShader->AddRef();
    g_movieVertexShader->AddRef();
    g_movieVertexDeclaration->AddRef();

    a1[2] = g_memory.MapVirtual(g_moviePixelShader);
    a1[3] = g_memory.MapVirtual(g_movieVertexShader);
    a1[4] = g_memory.MapVirtual(g_movieVertexDeclaration);
}

void MovieRendererMidAsmHook(PPCRegister& r3)
{
    auto device = reinterpret_cast<GuestDevice*>(g_memory.Translate(r3.u32));

    // Force linear filtering & clamp addressing
    for (size_t i = 0; i < 3; i++)
    {
        device->samplerStates[i].data[0] = (device->samplerStates[i].data[0].get() & ~0x7fc00) | 0x24800;
        device->samplerStates[i].data[3] = (device->samplerStates[i].data[3].get() & ~0x1f80000) | 0x1280000;
    }

    device->dirtyFlags[3] = device->dirtyFlags[3].get() | 0xe0000000ull;
}

static PPCRegister g_r4;
static PPCRegister g_r5;

// CRenderDirectorFxPipeline::Initialize
PPC_FUNC_IMPL(__imp__sub_8258C8A0);
PPC_FUNC(sub_8258C8A0)
{
    g_r4 = ctx.r4;
    g_r5 = ctx.r5;
    __imp__sub_8258C8A0(ctx, base);
}

// CRenderDirectorFxPipeline::Update
PPC_FUNC_IMPL(__imp__sub_8258CAE0);
PPC_FUNC(sub_8258CAE0)
{
    if (g_needsResize)
    {
        // Backup job values. These get modified by cutscenes, 
        // and resizing will cause the values to be forgotten.
        auto traverseFxJobs = [&]<typename TCallback>(const TCallback& callback)
        {
            uint32_t scheduler = PPC_LOAD_U32(ctx.r3.u32 + 0xE0);
            if (scheduler != NULL)
            {
                uint32_t member = PPC_LOAD_U32(scheduler + 0x8);
                if (member != NULL)
                {
                    for (uint32_t it = PPC_LOAD_U32(member + 0x24); it != PPC_LOAD_U32(member + 0x28); it += 8)
                    {
                        uint32_t job = PPC_LOAD_U32(it);
                        if (job != NULL)
                            callback(job);
                    }
                }
            }
        };

        union JobValues
        {
            struct
            {
                uint8_t field50[0x18];
                uint8_t field88;
            } fade;

            struct
            {
                uint8_t camera[0x120];
                uint8_t field44;
                uint8_t fieldA0;
            } shadowMap;
        };

        std::map<uint32_t, JobValues> jobValuesMap;
        traverseFxJobs([&](uint32_t job)
            {
                uint32_t vfTable = PPC_LOAD_U32(job);

                if (vfTable == 0x820CA6F8) // SWA::CFxFade
                {
                    // NOTE: Intentionally not storing shared pointers here. 
                    // Game sends messages that assign these every frame already.
                    JobValues jobValues{};

                    memcpy(jobValues.fade.field50, base + job + 0x50, sizeof(jobValues.fade.field50));
                    jobValues.fade.field88 = PPC_LOAD_U8(job + 0x88);

                    jobValuesMap.emplace(PPC_LOAD_U32(job + 0x48), jobValues);
                }
                else if (vfTable == 0x820CAC5C) // SWA::CFxShadowMap
                {
                    for (uint32_t it = PPC_LOAD_U32(job + 0x88); it != PPC_LOAD_U32(job + 0x8C); it += 8)
                    {
                        uint32_t camera = PPC_LOAD_U32(it);
                        if (camera != NULL && PPC_LOAD_U32(camera) == 0x820BF83C) // SWA::CShadowMapCameraLiSPSM
                        {
                            JobValues jobValues{};

                            memcpy(jobValues.shadowMap.camera, base + camera, sizeof(jobValues.shadowMap.camera));
                            jobValues.shadowMap.field44 = PPC_LOAD_U8(job + 0x44);
                            jobValues.shadowMap.fieldA0 = PPC_LOAD_U8(job + 0xA0);

                            jobValuesMap.emplace(vfTable, jobValues);
                            break;
                        }
                    }
                }
            });

        auto r3 = ctx.r3;
        ctx.r4 = g_r4;
        ctx.r5 = g_r5;
        __imp__sub_8258C8A0(ctx, base);
        ctx.r3 = r3;

        // Restore job values.
        traverseFxJobs([&](uint32_t job)
            {
                uint32_t vfTable = PPC_LOAD_U32(job);

                if (vfTable == 0x820CA6F8) // SWA::CFxFade
                {
                    auto findResult = jobValuesMap.find(PPC_LOAD_U32(job + 0x48));
                    if (findResult != jobValuesMap.end()) // May NOT actually be found.
                    {
                        memcpy(base + job + 0x50, findResult->second.fade.field50, sizeof(findResult->second.fade.field50));
                        PPC_STORE_U8(job + 0x88, findResult->second.fade.field88);
                    }
                }
                else if (vfTable == 0x820CAC5C) // SWA::CFxShadowMap
                {
                    auto findResult = jobValuesMap.find(vfTable);
                    if (findResult != jobValuesMap.end()) // Would be weird if this one wasn't found.
                    {
                        for (uint32_t it = PPC_LOAD_U32(job + 0x88); it != PPC_LOAD_U32(job + 0x8C); it += 8)
                        {
                            uint32_t camera = PPC_LOAD_U32(it);
                            if (camera != NULL && PPC_LOAD_U32(camera) == 0x820BF83C) // SWA::CShadowMapCameraLiSPSM
                            {
                                memcpy(base + camera, findResult->second.shadowMap.camera, sizeof(findResult->second.shadowMap.camera));
                                PPC_STORE_U32(job + 0x80, camera);
                                PPC_STORE_U8(job + 0x44, findResult->second.shadowMap.field44);
                                PPC_STORE_U8(job + 0xA0, findResult->second.shadowMap.fieldA0);
                                break;
                            }
                        }
                    }
                }
            });

        g_needsResize = false;
    }

    __imp__sub_8258CAE0(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_824EB5B0);
PPC_FUNC(sub_824EB5B0)
{
    g_updateDirectorProfiler.Begin();
    __imp__sub_824EB5B0(ctx, base);
    g_updateDirectorProfiler.End();
}

PPC_FUNC_IMPL(__imp__sub_824EB290);
PPC_FUNC(sub_824EB290)
{
    g_renderDirectorProfiler.Begin();
    __imp__sub_824EB290(ctx, base);
    g_renderDirectorProfiler.End();
}

// World map disables VERT+, so scaling by width does not work for it.
static uint32_t g_forceCheckHeightForPostProcessFix;

// SWA::CWorldMapCamera::CWorldMapCamera
PPC_FUNC_IMPL(__imp__sub_824860E0);
PPC_FUNC(sub_824860E0)
{
    ++g_forceCheckHeightForPostProcessFix;
    __imp__sub_824860E0(ctx, base);
}

// SWA::CCameraController::~CCameraController
PPC_FUNC_IMPL(__imp__sub_824831D0);
PPC_FUNC(sub_824831D0)
{
    if (PPC_LOAD_U32(ctx.r3.u32) == 0x8202BF1C) // SWA::CWorldMapCamera
        --g_forceCheckHeightForPostProcessFix;

    __imp__sub_824831D0(ctx, base);
}

void PostProcessResolutionFix(PPCRegister& r4, PPCRegister& f1, PPCRegister& f2)
{
    auto device = reinterpret_cast<be<uint32_t>*>(g_memory.Translate(r4.u32));

    uint32_t width = device[46].get();
    uint32_t height = device[47].get();
    double aspectRatio = double(width) / double(height);

    double factor;
    if ((aspectRatio >= WIDE_ASPECT_RATIO) || (g_forceCheckHeightForPostProcessFix != 0))
        factor = 720.0 / double(height);
    else
        factor = 1280.0 / double(width);

    f1.f64 *= factor;
    f2.f64 *= factor;
}

void LightShaftAspectRatioFix(PPCRegister& f28, PPCRegister& f0)
{
    f28.f64 = f0.f64;
}

static const be<uint16_t> g_particleTestIndexBuffer[] =
{
    0, 1, 2,
    0, 2, 3,
    0, 3, 4,
    0, 4, 5
};

bool ParticleTestIndexBufferMidAsmHook(PPCRegister& r30)
{
    if (!g_capabilities.triangleFan)
    {
        auto buffer = CreateIndexBuffer(sizeof(g_particleTestIndexBuffer), 0, D3DFMT_INDEX16);
        void* memory = LockIndexBuffer(buffer, 0, 0, 0);
        memcpy(memory, g_particleTestIndexBuffer, sizeof(g_particleTestIndexBuffer));
        UnlockIndexBuffer(buffer);

        r30.u32 = g_memory.MapVirtual(buffer);
        return true;
    }
    return false;
}

void ParticleTestDrawIndexedPrimitiveMidAsmHook(PPCRegister& r7)
{
    if (!g_capabilities.triangleFan)
        r7.u64 = std::size(g_particleTestIndexBuffer);
}

void MotionBlurPrevInvViewProjectionMidAsmHook(PPCRegister& r10)
{
    auto mtxProjection = reinterpret_cast<be<float>*>(g_memory.Translate(r10.u32));

    // Reverse Z. Have to be done on CPU side because the matrix multiplications
    // add up and it loses precision by the time it's sent to GPU.
    mtxProjection[10] = -(mtxProjection[10] + 1.0f);
    mtxProjection[14] = -mtxProjection[14];
}

// Normally, we could delay setting IsMadeOne, but the game relies on that flag
// being present to handle load priority. To work around that, we can prevent
// IsMadeAll from being set until the compilation is finished. Time for a custom flag!
enum
{
    eDatabaseDataFlags_CompilingPipelines = 0x80
};

// This is passed to pipeline compilation threads to keep the loading screen busy until 
// all of them are finished. A shared pointer makes sure the destructor is called only once.
struct PipelineTaskToken
{
    PipelineTaskType type{};
    boost::shared_ptr<Hedgehog::Database::CDatabaseData> databaseData;

    PipelineTaskToken() : databaseData()
    {
    }

    PipelineTaskToken(const PipelineTaskToken&) = delete;

    PipelineTaskToken(PipelineTaskToken&& other)
        : type(std::exchange(other.type, PipelineTaskType::Null))
        , databaseData(std::exchange(other.databaseData, nullptr))
    {
    }

    ~PipelineTaskToken()
    {
        if (type != PipelineTaskType::Null)
        {
            if (databaseData.get() != nullptr)
                databaseData->m_Flags &= ~eDatabaseDataFlags_CompilingPipelines;

            if ((--g_compilingPipelineTaskCount) == 0)
                g_compilingPipelineTaskCount.notify_one();
        }
    }
};

struct PipelineStateQueueItem
{
    XXH64_hash_t pipelineHash;
    PipelineState pipelineState;
    std::shared_ptr<PipelineTaskToken> token;
#ifdef ASYNC_PSO_DEBUG
    std::string pipelineName;
#endif
};

static moodycamel::BlockingConcurrentQueue<PipelineStateQueueItem> g_pipelineStateQueue;

static void CompilePipeline(XXH64_hash_t pipelineHash, const PipelineState& pipelineState
#ifdef ASYNC_PSO_DEBUG
    , const std::string& pipelineName
#endif
)
{
    auto pipeline = CreateGraphicsPipeline(pipelineState);
#ifdef ASYNC_PSO_DEBUG
    pipeline->setName(pipelineName);
#endif

    // Will get dropped in render thread if a different thread already managed to compile this.
    RenderCommand cmd;
    cmd.type = RenderCommandType::AddPipeline;
    cmd.addPipeline.hash = pipelineHash;
    cmd.addPipeline.pipeline = pipeline.release();
    g_renderQueue.enqueue(cmd);
}

static void PipelineCompilerThread()
{
#ifdef _WIN32
    int threadPriority = THREAD_PRIORITY_LOWEST;
    SetThreadPriority(GetCurrentThread(), threadPriority);
    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Compiler Thread");
#endif

    std::unique_ptr<GuestThreadContext> ctx;

    while (true)
    {
        PipelineStateQueueItem queueItem;
        g_pipelineStateQueue.wait_dequeue(queueItem);

        if (ctx == nullptr)
            ctx = std::make_unique<GuestThreadContext>(0);

#ifdef _WIN32
        int newThreadPriority = threadPriority;

        bool loading = *SWA::SGlobals::ms_IsLoading;
        if (loading)
            newThreadPriority = THREAD_PRIORITY_HIGHEST;
        else
            newThreadPriority = THREAD_PRIORITY_LOWEST;

        if (newThreadPriority != threadPriority)
        {
            SetThreadPriority(GetCurrentThread(), newThreadPriority);
            threadPriority = newThreadPriority;
        }
#endif

        CompilePipeline(queueItem.pipelineHash, queueItem.pipelineState
#ifdef ASYNC_PSO_DEBUG
            , queueItem.pipelineName.c_str()
#endif
        );

        std::this_thread::yield();
    }
}

static std::vector<std::unique_ptr<std::thread>> g_pipelineCompilerThreads = []()
    {
        size_t threadCount = std::max(2u, (std::thread::hardware_concurrency() * 2) / 3);

        std::vector<std::unique_ptr<std::thread>> threads(threadCount);
        for (auto& thread : threads)
            thread = std::make_unique<std::thread>(PipelineCompilerThread);

        return threads;
    }();

static constexpr uint32_t MODEL_DATA_VFTABLE = 0x82073A44;
static constexpr uint32_t TERRAIN_MODEL_DATA_VFTABLE = 0x8211D25C;
static constexpr uint32_t PARTICLE_MATERIAL_VFTABLE = 0x8211F198;

// Allocate the shared pointer only when new compilations are happening.
// If nothing was compiled, the local "token" variable will get destructed with RAII instead.
struct PipelineTaskTokenPair
{
    PipelineTaskToken token;
    std::shared_ptr<PipelineTaskToken> sharedToken;
};

// Having this separate, because I don't want to lock a mutex in the render thread before
// every single draw. Might be worth profiling to see if it actually has an impact and merge them.
static xxHashMap<PipelineState> g_asyncPipelineStates;

static void EnqueueGraphicsPipelineCompilation(
    const PipelineState& pipelineState, 
    PipelineTaskTokenPair& tokenPair, 
    const char* name,
    bool isPrecompiledPipeline = false)
{
    XXH64_hash_t hash = XXH3_64bits(&pipelineState, sizeof(pipelineState));
    bool shouldCompile = g_asyncPipelineStates.emplace(hash, pipelineState).second;

    if (shouldCompile)
    {
        bool loading = *SWA::SGlobals::ms_IsLoading;
        if (!loading && isPrecompiledPipeline)
        {
            // We can just compile here during the logos.
            CompilePipeline(hash, pipelineState
#ifdef ASYNC_PSO_DEBUG
                , fmt::format("CACHE {} {:X}", name, hash)
#endif
            );
        }
        else
        {
            if (tokenPair.sharedToken == nullptr && tokenPair.token.type != PipelineTaskType::Null)
                tokenPair.sharedToken = std::make_shared<PipelineTaskToken>(std::move(tokenPair.token));

            PipelineStateQueueItem queueItem;
            queueItem.pipelineHash = hash;
            queueItem.pipelineState = pipelineState;
            queueItem.token = tokenPair.sharedToken;
#ifdef ASYNC_PSO_DEBUG
            queueItem.pipelineName = fmt::format("ASYNC {} {:X}", name, hash);
#endif
            g_pipelineStateQueue.enqueue(queueItem);
        }
    }

#ifdef PSO_CACHING_CLEANUP
    if (shouldCompile && isPrecompiledPipeline)
    {
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.emplace(hash, pipelineState);
    }
#endif

#ifdef PSO_CACHING
    if (!isPrecompiledPipeline)
    {
        std::lock_guard lock(g_pipelineCacheMutex);
        g_pipelineStatesToCache.erase(hash);
    }
#endif
}

struct CompilationArgs
{
    PipelineTaskTokenPair tokenPair;
    bool noGI{};
    bool hasMoreThanOneBone{};
    bool velocityMapQuickStep{};
    bool objectIcon{};
    bool instancing{};
};

enum class MeshLayer
{
    Opaque,
    Transparent,
    PunchThrough,
    Special
};

struct Mesh
{
    uint32_t vertexSize{};
    uint32_t morphTargetVertexSize{};
    GuestVertexDeclaration* vertexDeclaration{};
    Hedgehog::Mirage::CMaterialData* material{};
    MeshLayer layer{};
    bool morphModel{};
};

static void CompileMeshPipeline(const Mesh& mesh, CompilationArgs& args)
{
    if (mesh.material == nullptr || mesh.material->m_spShaderListData.get() == nullptr)
        return;

    auto& shaderList = mesh.material->m_spShaderListData;

    bool isFur = !mesh.morphModel && !args.instancing &&
        strstr(shaderList->m_TypeAndName.c_str(), "Fur") != nullptr;

    bool isSky = !mesh.morphModel && !args.instancing &&
        strstr(shaderList->m_TypeAndName.c_str(), "Sky") != nullptr;

    bool isSonicMouth = !mesh.morphModel && !args.instancing &&
        strcmp(mesh.material->m_TypeAndName.c_str() + 2, "sonic_gm_mouth_duble") == 0 &&
        strcmp(shaderList->m_TypeAndName.c_str() + 3, "SonicSkin_dspf[b]") == 0;

    bool compiledOutsideMainFramebuffer = !args.instancing && !isFur && !isSky;

    bool constTexCoord;
    if (args.instancing)
    {
        constTexCoord = false;
    }
    else
    {
        constTexCoord = true;
        if (mesh.material->m_spTexsetData.get() != nullptr)
        {
            for (size_t i = 1; i < mesh.material->m_spTexsetData->m_TextureList.size(); i++)
            {
                if (mesh.material->m_spTexsetData->m_TextureList[i]->m_TexcoordIndex !=
                    mesh.material->m_spTexsetData->m_TextureList[0]->m_TexcoordIndex)
                {
                    constTexCoord = false;
                    break;
                }
            }
        }
    }

    // Shadow pipeline.
    if (compiledOutsideMainFramebuffer && (mesh.layer == MeshLayer::Opaque || mesh.layer == MeshLayer::PunchThrough))
    {
        PipelineState pipelineState{};

        if (mesh.layer == MeshLayer::PunchThrough)
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0xDD4FA7BB53876300)->guestShader;
            pipelineState.pixelShader = FindShaderCacheEntry(0xE2ECA594590DDE8B)->guestShader;
        }
        else
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0x8E4BB23465BD909E)->guestShader;
        }

        pipelineState.vertexDeclaration = mesh.vertexDeclaration;
        pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
        pipelineState.zFunc = RenderComparisonFunction::LESS_EQUAL;
        
        if (g_capabilities.dynamicDepthBias)
        {
            // Put common depth bias values for reducing unnecessary calls.
            if (!g_vulkan)
            {
                pipelineState.depthBias = COMMON_DEPTH_BIAS_VALUE;
                pipelineState.slopeScaledDepthBias = COMMON_SLOPE_SCALED_DEPTH_BIAS_VALUE;
            }
        }
        else 
        {
            pipelineState.depthBias = (1 << 24) * (*reinterpret_cast<be<float>*>(g_memory.Translate(0x83302760)));
            pipelineState.slopeScaledDepthBias = *reinterpret_cast<be<float>*>(g_memory.Translate(0x83302764));
        }

        pipelineState.colorWriteEnable = 0;
        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
        pipelineState.vertexStrides[0] = mesh.vertexSize;
        pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;

        if (mesh.layer == MeshLayer::PunchThrough)
            pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;

        const char* name = (mesh.layer == MeshLayer::PunchThrough ? "MakeShadowMapTransparent" : "MakeShadowMap");
        SanitizePipelineState(pipelineState);
        EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);

        // Morph models have 4 targets where unused targets default to the first vertex stream.
        if (mesh.morphModel)
        {
            for (size_t i = 0; i < 5; i++)
            {
                for (size_t j = 0; j < 4; j++)
                    pipelineState.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;

                SanitizePipelineState(pipelineState);
                EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, name);
            }
        }
    }

    // Motion blur pipeline. We could normally do the player here only, but apparently Werehog enemies also have object blur.
    // TODO: Do punch through meshes get rendered?
    if (!mesh.morphModel && compiledOutsideMainFramebuffer && args.hasMoreThanOneBone && mesh.layer == MeshLayer::Opaque)
    {
        PipelineState pipelineState{};
        pipelineState.vertexShader = FindShaderCacheEntry(0x4620B236DC38100C)->guestShader;
        pipelineState.pixelShader = FindShaderCacheEntry(0xBBDB735BEACC8F41)->guestShader;
        pipelineState.vertexDeclaration = mesh.vertexDeclaration;
        pipelineState.cullMode = RenderCullMode::NONE;
        pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
        pipelineState.vertexStrides[0] = mesh.vertexSize;
        pipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
        pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
        pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;

        SanitizePipelineState(pipelineState);
        EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMap");

        if (args.velocityMapQuickStep)
        {
            pipelineState.vertexShader = FindShaderCacheEntry(0x99DC3F27E402700D)->guestShader;
            SanitizePipelineState(pipelineState);
            EnqueueGraphicsPipelineCompilation(pipelineState, args.tokenPair, "FxVelocityMapQuickStep");
        }
    }

    uint32_t defaultStr = args.instancing ? 0x820C8734 : 0x8202DDBC; // "instancing" for instancing, "default" for regular
    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(defaultStr)));
    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
        return;

    uint32_t pixelShaderSubPermutationsToCompile = 0;
    if (constTexCoord) pixelShaderSubPermutationsToCompile |= 0x1;
    if (args.noGI) pixelShaderSubPermutationsToCompile |= 0x2;

    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x1;
    if ((defaultFindResult->second.m_SubPermutations.get() & (1 << pixelShaderSubPermutationsToCompile)) == 0) pixelShaderSubPermutationsToCompile &= ~0x2;

    uint32_t noneStr = mesh.morphModel ? 0x820D72F0 : 0x8200D938; // "p" for morph, "none" for regular
    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(noneStr)));
    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
        return;

    uint32_t vertexShaderSubPermutationsToCompile = 0;
    if (constTexCoord) vertexShaderSubPermutationsToCompile |= 0x1;

    if ((noneFindResult->second->m_SubPermutations.get() & (1 << vertexShaderSubPermutationsToCompile)) == 0)
        vertexShaderSubPermutationsToCompile &= ~0x1;

    auto vertexDeclaration = mesh.vertexDeclaration;
    bool instancing = args.instancing || isFur;

    if (instancing)
    {
        GuestVertexElement vertexElements[64];
        memcpy(vertexElements, mesh.vertexDeclaration->vertexElements.get(), (mesh.vertexDeclaration->vertexElementCount - 1) * sizeof(GuestVertexElement));

        if (args.instancing)
        {
            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2A23B9, 0, 5, 4 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 1, 12, 0x2C2159, 0, 5, 5 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = { 1, 16, 0x2C2159, 0, 5, 6 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 2] = { 1, 20, 0x182886, 0, 10, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 3] = { 2, 0, 0x2C82A1, 0, 0, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 4] = D3DDECL_END();
        }
        else if (isFur)
        {
            vertexElements[mesh.vertexDeclaration->vertexElementCount - 1] = { 1, 0, 0x2C82A1, 0, 0, 1 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount] = { 2, 0, 0x2C83A4, 0, 0, 2 };
            vertexElements[mesh.vertexDeclaration->vertexElementCount + 1] = D3DDECL_END();
        }

        vertexDeclaration = CreateVertexDeclarationWithoutAddRef(vertexElements);
    }

    for (auto& [pixelShaderSubPermutations, pixelShader] : defaultFindResult->second.m_PixelShaders)
    {
        if (pixelShader.get() == nullptr || (pixelShaderSubPermutations & 0x3) != pixelShaderSubPermutationsToCompile)
            continue;

        for (auto& [vertexShaderSubPermutations, vertexShader] : noneFindResult->second->m_VertexShaders)
        {
            if (vertexShader.get() == nullptr || (vertexShaderSubPermutations & 0x1) != vertexShaderSubPermutationsToCompile)
                continue;

            PipelineState pipelineState{};
            pipelineState.vertexShader = reinterpret_cast<GuestShader*>(vertexShader->m_spCode->m_pD3DVertexShader.get());
            pipelineState.pixelShader = reinterpret_cast<GuestShader*>(pixelShader->m_spCode->m_pD3DPixelShader.get());
            pipelineState.vertexDeclaration = vertexDeclaration;
            pipelineState.instancing = instancing;
            pipelineState.zWriteEnable = !isSky && mesh.layer != MeshLayer::Transparent;
            pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
            pipelineState.destBlend = mesh.material->m_Additive ? RenderBlend::ONE : RenderBlend::INV_SRC_ALPHA;
            pipelineState.cullMode = mesh.material->m_DoubleSided ? RenderCullMode::NONE : RenderCullMode::BACK;
            pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL; // Reverse Z
            pipelineState.alphaBlendEnable = mesh.layer == MeshLayer::Transparent || mesh.layer == MeshLayer::Special;
            pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
            pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
            pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
            pipelineState.vertexStrides[0] = mesh.vertexSize;

            if (args.instancing)
            {
                pipelineState.vertexStrides[1] = 24;
                pipelineState.vertexStrides[2] = 4;
            }
            else if (isFur)
            {
                pipelineState.vertexStrides[1] = 4;
                pipelineState.vertexStrides[2] = 4;
            }

            pipelineState.renderTargetFormat = RenderFormat::R16G16B16A16_FLOAT;
            pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
            pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;

            if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
                pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;

            if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

            if (mesh.layer == MeshLayer::PunchThrough)
            {
                if (Config::AntiAliasing != EAntiAliasing::None && Config::TransparencyAntiAliasing)
                {
                    pipelineState.enableAlphaToCoverage = true;
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                }
                else
                {
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                }
            }

            if (!isSky)
                pipelineState.specConstants |= SPEC_CONSTANT_REVERSE_Z;

            auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
                {
                    SanitizePipelineState(pipelineStateToCreate);
                    EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);

                    // Morph models have 4 targets where unused targets default to the first vertex stream.
                    if (mesh.morphModel)
                    {
                        for (size_t i = 0; i < 5; i++)
                        {
                            for (size_t j = 0; j < 4; j++)
                                pipelineStateToCreate.vertexStrides[j + 1] = i > j ? mesh.morphTargetVertexSize : mesh.vertexSize;

                            SanitizePipelineState(pipelineStateToCreate);
                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, args.tokenPair, shaderList->m_TypeAndName.c_str() + 3);
                        }
                    }
                };

            createGraphicsPipeline(pipelineState);

            // We cannot rely on this being accurate during loading as SceneEffect.prm.xml gets loaded a bit later.
            bool planarReflectionEnabled = *reinterpret_cast<bool*>(g_memory.Translate(0x832FA0D8));
            bool loading = *SWA::SGlobals::ms_IsLoading;
            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1 && (loading || planarReflectionEnabled);

            auto noMsaaPipeline = pipelineState;
            noMsaaPipeline.sampleCount = 1;
            noMsaaPipeline.enableAlphaToCoverage = false;

            if ((noMsaaPipeline.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
            {
                noMsaaPipeline.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                noMsaaPipeline.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
            }

            if (compileNoMsaaPipeline)
            {
                // Planar reflections don't use MSAA.
                createGraphicsPipeline(noMsaaPipeline);
            }

            if (args.objectIcon) 
            {
                // Object icons get rendered to a SDR buffer without MSAA.
                auto iconPipelineState = noMsaaPipeline;
                iconPipelineState.renderTargetFormat = RenderFormat::R8G8B8A8_UNORM;
                createGraphicsPipeline(iconPipelineState);
            }

            if (isSonicMouth)
            {
                // Sonic's mouth switches between "SonicSkin_dspf[b]" or "SonicSkinNodeInvX_dspf[b]" depending on the view angle.
                auto mouthPipelineState = pipelineState;
                mouthPipelineState.vertexShader = FindShaderCacheEntry(0x689AA3140AB9EBAA)->guestShader;
                createGraphicsPipeline(mouthPipelineState);

                if (compileNoMsaaPipeline)
                {
                    auto noMsaaMouthPipelineState = noMsaaPipeline;
                    noMsaaMouthPipelineState.vertexShader = mouthPipelineState.vertexShader;
                    createGraphicsPipeline(noMsaaMouthPipelineState);
                }
            }
        }
    }
}

static void CompileMeshPipeline(Hedgehog::Mirage::CMeshData* mesh, MeshLayer layer, CompilationArgs& args)
{
    CompileMeshPipeline(Mesh
        {
            mesh->m_VertexSize,
            0,
            reinterpret_cast<GuestVertexDeclaration*>(mesh->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
            mesh->m_spMaterial.get(),
            layer,
            false
        }, args);
}

static void CompileMeshPipeline(Hedgehog::Mirage::CMorphModelData* morphModel, Hedgehog::Mirage::CMeshIndexData* mesh, MeshLayer layer, CompilationArgs& args)
{
    CompileMeshPipeline(Mesh
        {
            morphModel->m_VertexSize,
            morphModel->m_MorphTargetVertexSize,
            reinterpret_cast<GuestVertexDeclaration*>(morphModel->m_VertexDeclarationPtr.m_pD3DVertexDeclaration.get()),
            mesh->m_spMaterial.get(),
            layer,
            true
        }, args);
}

template<typename T>
static void CompileMeshPipelines(const T& modelData, CompilationArgs& args)
{
    for (auto& meshGroup : modelData.m_NodeGroupModels)
    {
        for (auto& mesh : meshGroup->m_OpaqueMeshes)
        {
            CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);

            if (args.noGI) // For models that can be shown transparent (eg. medals)
                CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
        }

        for (auto& mesh : meshGroup->m_TransparentMeshes)
            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);

        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
            CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);

        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
        {
            for (auto& mesh : specialMeshGroup)
                CompileMeshPipeline(mesh.get(), MeshLayer::Special, args); // TODO: Are there layer types other than water in this game??
        }
    }

    for (auto& mesh : modelData.m_OpaqueMeshes)
    {
        CompileMeshPipeline(mesh.get(), MeshLayer::Opaque, args);

        if (args.noGI)
            CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);
    }

    for (auto& mesh : modelData.m_TransparentMeshes)
        CompileMeshPipeline(mesh.get(), MeshLayer::Transparent, args);

    for (auto& mesh : modelData.m_PunchThroughMeshes)
        CompileMeshPipeline(mesh.get(), MeshLayer::PunchThrough, args);

    if constexpr (std::is_same_v<T, Hedgehog::Mirage::CModelData>)
    {
        for (auto& morphModel : modelData.m_MorphModels)
        {
            for (auto& mesh : morphModel->m_OpaqueMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Opaque, args);

            for (auto& mesh : morphModel->m_TransparentMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::Transparent, args);

            for (auto& mesh : morphModel->m_PunchThroughMeshList)
                CompileMeshPipeline(morphModel.get(), mesh.get(), MeshLayer::PunchThrough, args);
        }
    }
}

static void CompileParticleMaterialPipeline(const Hedgehog::Sparkle::CParticleMaterial& material, PipelineTaskTokenPair& tokenPair)
{
    auto& shaderList = material.m_spShaderListData;
    if (shaderList.get() == nullptr)
        return;

    guest_stack_var<Hedgehog::Base::CStringSymbol> defaultSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8202DDBC)));
    auto defaultFindResult = shaderList->m_PixelShaderPermutations.find(*defaultSymbol);
    if (defaultFindResult == shaderList->m_PixelShaderPermutations.end())
        return;

    guest_stack_var<Hedgehog::Base::CStringSymbol> noneSymbol(reinterpret_cast<const char*>(g_memory.Translate(0x8200D938)));
    auto noneFindResult = defaultFindResult->second.m_VertexShaderPermutations.find(*noneSymbol);
    if (noneFindResult == defaultFindResult->second.m_VertexShaderPermutations.end())
        return;

    // All the particle models in the game come with the unoptimized format, so we can assume it.
    uint8_t unoptimizedVertexElements[144] = 
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0C, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x18, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x24, 0x00, 0x2A, 0x23, 0xB9, 0x00, 0x07, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x30, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x38, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x00, 0x2C, 0x23, 0xA5, 0x00, 0x05, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x50, 0x00, 0x1A, 0x23, 0xA6, 0x00, 0x0A, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x60, 0x00, 0x1A, 0x23, 0x86, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x64, 0x00, 0x1A, 0x20, 0x86, 0x00, 0x01, 0x00, 0x00,
        0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
    };

    auto unoptimizedVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(unoptimizedVertexElements));
    auto sparkleVertexDeclaration = CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(g_memory.Translate(0x8211F540)));

    bool isMeshShader = strstr(shaderList->m_TypeAndName.c_str(), "Mesh") != nullptr;

    PipelineState pipelineState{};
    pipelineState.vertexShader = reinterpret_cast<GuestShader*>(noneFindResult->second->m_VertexShaders.begin()->second->m_spCode->m_pD3DVertexShader.get());
    pipelineState.pixelShader = reinterpret_cast<GuestShader*>(defaultFindResult->second.m_PixelShaders.begin()->second->m_spCode->m_pD3DPixelShader.get());
    pipelineState.vertexDeclaration = isMeshShader ? unoptimizedVertexDeclaration : sparkleVertexDeclaration;
    pipelineState.zWriteEnable = false;
    pipelineState.zFunc = RenderComparisonFunction::GREATER_EQUAL;
    pipelineState.alphaBlendEnable = true;
    pipelineState.srcBlendAlpha = RenderBlend::SRC_ALPHA;
    pipelineState.destBlendAlpha = RenderBlend::INV_SRC_ALPHA;
    pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_STRIP;
    pipelineState.vertexStrides[0] = isMeshShader ? 104 : 28;
    pipelineState.depthStencilFormat = RenderFormat::D32_FLOAT;
    pipelineState.specConstants = SPEC_CONSTANT_REVERSE_Z;

    if (pipelineState.vertexDeclaration->hasR11G11B10Normal)
        pipelineState.specConstants |= SPEC_CONSTANT_R11G11B10_NORMAL;

    switch (material.m_BlendMode.get())
    {
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Zero:
        pipelineState.srcBlend = RenderBlend::ZERO;
        pipelineState.destBlend = RenderBlend::ZERO;
        break;
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Typical:
        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
        pipelineState.destBlend = RenderBlend::INV_SRC_ALPHA;
        break;
    case Hedgehog::Sparkle::CParticleMaterial::eBlendMode_Add:
        pipelineState.srcBlend = RenderBlend::SRC_ALPHA;
        pipelineState.destBlend = RenderBlend::ONE;
        break;
    default:
        pipelineState.srcBlend = RenderBlend::ONE;
        pipelineState.destBlend = RenderBlend::ONE;
        break;
    }

    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate)
        {
            SanitizePipelineState(pipelineStateToCreate);
            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, shaderList->m_TypeAndName.c_str() + 3);
        };

    // Mesh particles can use both cull modes. Quad particles are only NONE.
    RenderCullMode cullModes[] = { RenderCullMode::NONE, RenderCullMode::BACK };
    uint32_t cullModeCount = isMeshShader ? std::size(cullModes) : 1;
    RenderFormat renderTargetFormats[] = { RenderFormat::R16G16B16A16_FLOAT, RenderFormat::R8G8B8A8_UNORM };

    for (size_t i = 0; i < cullModeCount; i++)
    {
        pipelineState.cullMode = cullModes[i];

        for (auto renderTargetFormat : renderTargetFormats)
        {
            pipelineState.renderTargetFormat = renderTargetFormat;

            if (renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT)
                pipelineState.sampleCount = Config::AntiAliasing != EAntiAliasing::None ? int32_t(Config::AntiAliasing.Value) : 1;
            else
                pipelineState.sampleCount = 1;

            createGraphicsPipeline(pipelineState);

            // Always compile no MSAA variant for particles, as the planar
            // reflection variable isn't reliable at this time of compilation.
            bool compileNoMsaaPipeline = pipelineState.sampleCount != 1;

            auto noMsaaPipelineState = pipelineState;
            noMsaaPipelineState.sampleCount = 1;

            if (compileNoMsaaPipeline)
                createGraphicsPipeline(noMsaaPipelineState);

            if (!isMeshShader)
            {
                // Previous compilation was for locus particles. This one will be for quads.
                auto quadPipelineState = pipelineState;
                quadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
                createGraphicsPipeline(quadPipelineState);

                if (compileNoMsaaPipeline)
                {
                    auto noMsaaQuadPipelineState = noMsaaPipelineState;
                    noMsaaQuadPipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
                    createGraphicsPipeline(noMsaaQuadPipelineState);
                }
            }
        }
    }
}

static std::thread::id g_mainThreadId = std::this_thread::get_id();

// SWA::CGameModeStage::ExitLoading
PPC_FUNC_IMPL(__imp__sub_825369A0);
PPC_FUNC(sub_825369A0)
{
    assert(std::this_thread::get_id() == g_mainThreadId);

    // Wait for pipeline compilations to finish.
    uint32_t value;
    while ((value = g_compilingPipelineTaskCount.load()) != 0)
    {
        // Pump SDL events to prevent the OS
        // from thinking the process is unresponsive.
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

        g_compilingPipelineTaskCount.wait(value);
    }

    __imp__sub_825369A0(ctx, base);
}

// CModelData::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E2EFB0);
PPC_FUNC(sub_82E2EFB0)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E2EFB0(ctx, base);
    }
}

// CTerrainModelData::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E243D8);
PPC_FUNC(sub_82E243D8)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E243D8(ctx, base);
    }
}

// CParticleMaterial::CheckMadeAll
PPC_FUNC_IMPL(__imp__sub_82E87598);
PPC_FUNC(sub_82E87598)
{   
    if (reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32)->m_Flags & eDatabaseDataFlags_CompilingPipelines)
    {
        ctx.r3.u64 = 0;
    }
    else
    {
        __imp__sub_82E87598(ctx, base);
    }
}

void GetDatabaseDataMidAsmHook(PPCRegister& r1, PPCRegister& r4)
{
    auto& databaseData = *reinterpret_cast<boost::shared_ptr<Hedgehog::Database::CDatabaseData>*>(
        g_memory.Translate(r1.u32 + 0x58));

    if (!databaseData->IsMadeOne() && r4.u32 != NULL)
    {
        if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
        {
            // Ignore particle models, the materials they point at don't actually
            // get used and give the threads unnecessary work.
            bool isParticleModel = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(r4.u32 + 4)) != 5 &&
                strncmp(databaseData->m_TypeAndName.c_str() + 2, "eff_", 4) == 0;

            if (isParticleModel)
                return;

            // Adabat water is broken in original game, which they tried to fix by partially including the files in the update,
            // which then finally fixed for real in the DLC. This confuses the async PSO compiler and causes a hang if the DLC is missing.
            // We'll just ignore it.
            bool isAdabatWater = strcmp(databaseData->m_TypeAndName.c_str() + 2, "evl_sea_obj_st_waterCircle") == 0;
            if (isAdabatWater)
                return;
        }

        databaseData->m_Flags |= eDatabaseDataFlags_CompilingPipelines;
        EnqueuePipelineTask(PipelineTaskType::DatabaseData, databaseData);
    }
}

static bool CheckMadeAll(Hedgehog::Mirage::CMeshData* meshData)
{
    if (!meshData->IsMadeOne())
        return false;

    if (meshData->m_spMaterial.get() != nullptr)
    {
        if (!meshData->m_spMaterial->IsMadeOne())
            return false;

        if (meshData->m_spMaterial->m_spTexsetData.get() != nullptr)
        {
            if (!meshData->m_spMaterial->m_spTexsetData->IsMadeOne())
                return false;

            for (auto& texture : meshData->m_spMaterial->m_spTexsetData->m_TextureList)
            {
                if (!texture->IsMadeOne())
                    return false;
            }
        }
    }

    return true;
}

template<typename T>
static bool CheckMadeAll(const T& modelData)
{
    if (!modelData.IsMadeOne())
        return false;

    for (auto& meshGroup : modelData.m_NodeGroupModels)
    {
        for (auto& mesh : meshGroup->m_OpaqueMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }     

        for (auto& mesh : meshGroup->m_TransparentMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }    

        for (auto& mesh : meshGroup->m_PunchThroughMeshes)
        {
            if (!CheckMadeAll(mesh.get()))
                return false;
        }

        for (auto& specialMeshGroup : meshGroup->m_SpecialMeshGroups)
        {
            for (auto& mesh : specialMeshGroup)
            {
                if (!CheckMadeAll(mesh.get()))
                    return false;
            }
        }
    }

    for (auto& mesh : modelData.m_OpaqueMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_TransparentMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    for (auto& mesh : modelData.m_PunchThroughMeshes)
    {
        if (!CheckMadeAll(mesh.get()))
            return false;
    }

    return true;
}

static void PipelineTaskConsumerThread()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    GuestThread::SetThreadName(GetCurrentThreadId(), "Pipeline Task Consumer Thread");
#endif

    std::vector<PipelineTask> localPipelineTaskQueue;
    std::unique_ptr<GuestThreadContext> ctx;

    while (true)
    {
        // Wait for tasks to arrive.
        uint32_t pendingPipelineTaskCount;
        while ((pendingPipelineTaskCount = g_pendingPipelineTaskCount.load()) == 0)
            g_pendingPipelineTaskCount.wait(pendingPipelineTaskCount);

        if (ctx == nullptr)
            ctx = std::make_unique<GuestThreadContext>(0);

        {
            std::lock_guard lock(g_pipelineTaskMutex);
            localPipelineTaskQueue.insert(localPipelineTaskQueue.end(), g_pipelineTaskQueue.begin(), g_pipelineTaskQueue.end());
            g_pipelineTaskQueue.clear();
        }

        bool allHandled = true;

        for (auto& [type, databaseData] : localPipelineTaskQueue)
        {
            switch (type)
            {
            case PipelineTaskType::DatabaseData:
            {
                bool ready = false;

                if (databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE)
                    ready = CheckMadeAll(*reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get()));
                else
                    ready = databaseData->IsMadeOne();

                if (ready || databaseData.unique())
                {
                    if (databaseData->m_pVftable.ptr == TERRAIN_MODEL_DATA_VFTABLE)
                    {
                        CompilationArgs args{};
                        args.tokenPair.token.type = type;
                        args.tokenPair.token.databaseData = databaseData;
                        args.instancing = strncmp(databaseData->m_TypeAndName.c_str() + 3, "ins", 3) == 0;
                        CompileMeshPipelines(*reinterpret_cast<Hedgehog::Mirage::CTerrainModelData*>(databaseData.get()), args);
                    }
                    else if (databaseData->m_pVftable.ptr == PARTICLE_MATERIAL_VFTABLE)
                    {
                        PipelineTaskTokenPair tokenPair;
                        tokenPair.token.type = type;
                        tokenPair.token.databaseData = databaseData;
                        CompileParticleMaterialPipeline(*reinterpret_cast<Hedgehog::Sparkle::CParticleMaterial*>(databaseData.get()), tokenPair);
                    }
                    else
                    {
                        assert(databaseData->m_pVftable.ptr == MODEL_DATA_VFTABLE);

                        auto modelData = reinterpret_cast<Hedgehog::Mirage::CModelData*>(databaseData.get());

                        CompilationArgs args{};
                        args.tokenPair.token.type = type;
                        args.tokenPair.token.databaseData = databaseData;
                        args.noGI = true;
                        args.hasMoreThanOneBone = modelData->m_NodeNum > 1;
                        args.velocityMapQuickStep = strcmp(databaseData->m_TypeAndName.c_str() + 2, "SonicRoot") == 0;

                        // Check for the on screen items, eg. rings going to HUD.
                        auto items = reinterpret_cast<xpointer<const char>*>(g_memory.Translate(0x832A8DD0));
                        for (size_t i = 0; i < 50; i++)
                        {
                            if (strcmp(databaseData->m_TypeAndName.c_str() + 2, (*items).get()) == 0)
                            {
                                args.objectIcon = true;
                                break;
                            }
                            items += 7;
                        }

                        CompileMeshPipelines(*modelData, args);
                    }

                    type = PipelineTaskType::Null;
                    databaseData = nullptr;

                    --g_pendingPipelineTaskCount;
                }
                else
                {
                    allHandled = false;
                }

                break;
            }

            case PipelineTaskType::PrecompilePipelines:
            {
                // Deliberately leaving the type null to account for the enqueue
                // call not incrementing the compiling pipeline task counter.
                PipelineTaskTokenPair tokenPair;

                for (auto vertexElements : g_vertexDeclarationCache)
                    CreateVertexDeclarationWithoutAddRef(reinterpret_cast<GuestVertexElement*>(vertexElements));

                for (auto pipelineState : g_pipelineStateCache)
                {
                    // The hashes were reinterpret casted to pointers in the cache.
                    pipelineState.vertexShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.vertexShader))->guestShader;

                    if (pipelineState.pixelShader != nullptr)
                        pipelineState.pixelShader = FindShaderCacheEntry(reinterpret_cast<XXH64_hash_t>(pipelineState.pixelShader))->guestShader;

                    {
                        std::lock_guard lock(g_vertexDeclarationMutex);
                        pipelineState.vertexDeclaration = g_vertexDeclarations[reinterpret_cast<XXH64_hash_t>(pipelineState.vertexDeclaration)];
                    }

                    if (!g_capabilities.triangleFan && pipelineState.primitiveTopology == RenderPrimitiveTopology::TRIANGLE_FAN)
                        pipelineState.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;

                    // Zero out depth bias for Vulkan, we only store common values for D3D12.
                    if (g_capabilities.dynamicDepthBias && g_vulkan)
                    {
                        pipelineState.depthBias = 0;
                        pipelineState.slopeScaledDepthBias = 0.0f;
                    }

                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                    auto createGraphicsPipeline = [&](PipelineState& pipelineStateToCreate, const char* name)
                        {
                            SanitizePipelineState(pipelineStateToCreate);
                            EnqueueGraphicsPipelineCompilation(pipelineStateToCreate, tokenPair, name, true);
                        };

                    // Compile both MSAA and non MSAA variants to work with reflection maps. The render formats are an assumption but it should hold true.
                    if (Config::AntiAliasing != EAntiAliasing::None &&
                        pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT && 
                        pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT)
                    {
                        auto msaaPipelineState = pipelineState;
                        msaaPipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);

                        if (Config::TransparencyAntiAliasing && (msaaPipelineState.specConstants & SPEC_CONSTANT_ALPHA_TEST) != 0)
                        {
                            msaaPipelineState.enableAlphaToCoverage = true;
                            msaaPipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TEST;
                            msaaPipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                        }

                        createGraphicsPipeline(msaaPipelineState, "Precompiled Pipeline MSAA");
                    }

                    if (pipelineState.pixelShader != nullptr &&
                        pipelineState.pixelShader->shaderCacheEntry != nullptr)
                    {
                        XXH64_hash_t hash = pipelineState.pixelShader->shaderCacheEntry->hash;

                        // Compile the custom gaussian blur shaders that we pass to the game.
                        if (hash == 0x4294510C775F4EE8)
                        {
                            for (auto& shader : g_gaussianBlurShaders)
                            {
                                auto newPipelineState = pipelineState;
                                newPipelineState.pixelShader = shader.get();
                                createGraphicsPipeline(newPipelineState, "Precompiled Gaussian Blur Pipeline");
                            }
                        }
                        // Compile enhanced motion blur shader.
                        else if (hash == 0x6B9732B4CD7E7740)
                        {
                            auto newPipelineState = pipelineState;
                            newPipelineState.pixelShader = g_enhancedMotionBlurShader.get();
                            createGraphicsPipeline(newPipelineState, "Precompiled Enhanced Motion Blur Pipeline");
                        }
                    }
                
                    createGraphicsPipeline(pipelineState, "Precompiled Pipeline");

                    // Compile the CSD filter shader that we pass to the game when point filtering is used.
                    if (pipelineState.pixelShader == g_csdShader)
                    {
                        pipelineState.pixelShader = g_csdFilterShader.get();
                        createGraphicsPipeline(pipelineState, "Precompiled CSD Filter Pipeline");
                    }
                }

                type = PipelineTaskType::Null;
                --g_pendingPipelineTaskCount;

                break;
            }

            case PipelineTaskType::RecompilePipelines:
            {
                PipelineTaskTokenPair tokenPair;
                tokenPair.token.type = type;

                auto asyncPipelines = g_asyncPipelineStates.values();

                for (auto& [hash, pipelineState] : asyncPipelines)
                {
                    bool alphaTest = (pipelineState.specConstants & (SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE)) != 0;
                    bool msaa = pipelineState.sampleCount != 1 || (pipelineState.renderTargetFormat == RenderFormat::R16G16B16A16_FLOAT && pipelineState.depthStencilFormat == RenderFormat::D32_FLOAT);

                    pipelineState.sampleCount = 1;
                    pipelineState.enableAlphaToCoverage = false;
                    pipelineState.specConstants &= ~(SPEC_CONSTANT_BICUBIC_GI_FILTER | SPEC_CONSTANT_ALPHA_TEST | SPEC_CONSTANT_ALPHA_TO_COVERAGE);

                    if (msaa && Config::AntiAliasing != EAntiAliasing::None)
                    {
                        pipelineState.sampleCount = int32_t(Config::AntiAliasing.Value);

                        if (alphaTest)
                        {
                            if (Config::TransparencyAntiAliasing)
                            {
                                pipelineState.enableAlphaToCoverage = true;
                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                            }
                            else
                            {
                                pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                            }
                        }
                    }
                    else if (alphaTest)
                    {
                        pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                    }

                    if (Config::GITextureFiltering == EGITextureFiltering::Bicubic)
                        pipelineState.specConstants |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                    SanitizePipelineState(pipelineState);
                    EnqueueGraphicsPipelineCompilation(pipelineState, tokenPair, "Recompiled Pipeline State");
                }

                type = PipelineTaskType::Null;
                --g_pendingPipelineTaskCount;

                break;
            }
            }
        }

        if (allHandled)
            localPipelineTaskQueue.clear();

        std::this_thread::yield();
    }
}

static std::thread g_pipelineTaskConsumerThread(PipelineTaskConsumerThread);

#ifdef ASYNC_PSO_DEBUG

PPC_FUNC_IMPL(__imp__sub_82E33330);
PPC_FUNC(sub_82E33330)
{
    auto vertexShaderCode = reinterpret_cast<Hedgehog::Mirage::CVertexShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
    __imp__sub_82E33330(ctx, base);
    reinterpret_cast<GuestShader*>(vertexShaderCode->m_pD3DVertexShader.get())->name = vertexShaderCode->m_TypeAndName.c_str() + 3;
}

PPC_FUNC_IMPL(__imp__sub_82E328D8);
PPC_FUNC(sub_82E328D8)
{
    auto pixelShaderCode = reinterpret_cast<Hedgehog::Mirage::CPixelShaderCodeData*>(g_memory.Translate(ctx.r4.u32));
    __imp__sub_82E328D8(ctx, base);
    reinterpret_cast<GuestShader*>(pixelShaderCode->m_pD3DPixelShader.get())->name = pixelShaderCode->m_TypeAndName.c_str() + 2;
}

#endif

#ifdef PSO_CACHING
class SDLEventListenerForPSOCaching : public SDLEventListener
{
public:
    bool OnSDLEvent(SDL_Event* event) override 
    {
        if (event->type != SDL_QUIT)
            return false;

        std::lock_guard lock(g_pipelineCacheMutex);
        if (g_pipelineStatesToCache.empty())
            return false;

        FILE* f = fopen("send_this_file_to_skyth.txt", "ab");
        if (f != nullptr)
        {
            ankerl::unordered_dense::set<GuestVertexDeclaration*> vertexDeclarations;
            xxHashMap<PipelineState> pipelineStatesToCache;

            for (auto& [hash, pipelineState] : g_pipelineStatesToCache)
            {
                if (pipelineState.vertexShader->shaderCacheEntry == nullptr ||
                    (pipelineState.pixelShader != nullptr && pipelineState.pixelShader->shaderCacheEntry == nullptr))
                {
                    continue;
                }

                vertexDeclarations.emplace(pipelineState.vertexDeclaration);

                // Mask out the config options.
                pipelineState.sampleCount = 1;
                pipelineState.enableAlphaToCoverage = false;

                pipelineState.specConstants &= ~SPEC_CONSTANT_BICUBIC_GI_FILTER;
                if ((pipelineState.specConstants & SPEC_CONSTANT_ALPHA_TO_COVERAGE) != 0)
                {
                    pipelineState.specConstants &= ~SPEC_CONSTANT_ALPHA_TO_COVERAGE;
                    pipelineState.specConstants |= SPEC_CONSTANT_ALPHA_TEST;
                }

                pipelineStatesToCache.emplace(XXH3_64bits(&pipelineState, sizeof(pipelineState)), pipelineState);
            }

            for (auto vertexDeclaration : vertexDeclarations)
            {
                fmt::print(f, "static uint8_t g_vertexElements_{:016X}[] = {{", vertexDeclaration->hash);

                auto bytes = reinterpret_cast<uint8_t*>(vertexDeclaration->vertexElements.get());
                for (size_t i = 0; i < vertexDeclaration->vertexElementCount * sizeof(GuestVertexElement); i++)
                    fmt::print(f, "0x{:X},", bytes[i]);

                fmt::println(f, "}};");
            }

            for (auto& [pipelineHash, pipelineState] : pipelineStatesToCache)
            {
                fmt::println(f, "{{ "
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestShader*>(0x{:X}),"
                    "reinterpret_cast<GuestVertexDeclaration*>(0x{:X}),"
                    "{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderCullMode::{},"
                    "RenderComparisonFunction::{},"
                    "{},"
                    "RenderBlendOperation::{},"
                    "{},"
                    "{},"
                    "RenderBlend::{},"
                    "RenderBlend::{},"
                    "RenderBlendOperation::{},"
                    "0x{:X},"
                    "RenderPrimitiveTopology::{},"
                    "{{ {},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{} }},"
                    "RenderFormat::{},"
                    "RenderFormat::{},"
                    "{},"
                    "{},"
                    "0x{:X} }},",
                    pipelineState.vertexShader->shaderCacheEntry->hash,
                    pipelineState.pixelShader != nullptr ? pipelineState.pixelShader->shaderCacheEntry->hash : 0,
                    pipelineState.vertexDeclaration->hash,
                    pipelineState.instancing,
                    pipelineState.zEnable,
                    pipelineState.zWriteEnable,
                    magic_enum::enum_name(pipelineState.srcBlend),
                    magic_enum::enum_name(pipelineState.destBlend),
                    magic_enum::enum_name(pipelineState.cullMode),
                    magic_enum::enum_name(pipelineState.zFunc),
                    pipelineState.alphaBlendEnable,
                    magic_enum::enum_name(pipelineState.blendOp),
                    pipelineState.slopeScaledDepthBias,
                    pipelineState.depthBias,
                    magic_enum::enum_name(pipelineState.srcBlendAlpha),
                    magic_enum::enum_name(pipelineState.destBlendAlpha),
                    magic_enum::enum_name(pipelineState.blendOpAlpha),
                    pipelineState.colorWriteEnable,
                    magic_enum::enum_name(pipelineState.primitiveTopology),
                    pipelineState.vertexStrides[0],
                    pipelineState.vertexStrides[1],
                    pipelineState.vertexStrides[2],
                    pipelineState.vertexStrides[3],
                    pipelineState.vertexStrides[4],
                    pipelineState.vertexStrides[5],
                    pipelineState.vertexStrides[6],
                    pipelineState.vertexStrides[7],
                    pipelineState.vertexStrides[8],
                    pipelineState.vertexStrides[9],
                    pipelineState.vertexStrides[10],
                    pipelineState.vertexStrides[11],
                    pipelineState.vertexStrides[12],
                    pipelineState.vertexStrides[13],
                    pipelineState.vertexStrides[14],
                    pipelineState.vertexStrides[15],
                    magic_enum::enum_name(pipelineState.renderTargetFormat),
                    magic_enum::enum_name(pipelineState.depthStencilFormat),
                    pipelineState.sampleCount,
                    pipelineState.enableAlphaToCoverage,
                    pipelineState.specConstants);
            }

            fclose(f);
        }

        return false;
    }
};
SDLEventListenerForPSOCaching g_sdlEventListenerForPSOCaching;
#endif

void VideoConfigValueChangedCallback(IConfigDef* config)
{
    // Config options that require internal resolution resize
    g_needsResize |=
        config == &Config::AspectRatio ||
        config == &Config::ResolutionScale ||
        config == &Config::AntiAliasing ||
        config == &Config::ShadowResolution;

    if (g_needsResize)
        Video::ComputeViewportDimensions();
        
    // Config options that require pipeline recompilation
    bool shouldRecompile =
        config == &Config::AntiAliasing ||
        config == &Config::TransparencyAntiAliasing ||
        config == &Config::GITextureFiltering;

    if (shouldRecompile)
        EnqueuePipelineTask(PipelineTaskType::RecompilePipelines, {});
}

// SWA::CCsdTexListMirage::SetFilter
PPC_FUNC_IMPL(__imp__sub_825E4300);
PPC_FUNC(sub_825E4300)
{
    g_csdFilterState = ctx.r5.u32 == 0 ? CsdFilterState::On : CsdFilterState::Off;
    ctx.r5.u32 = 1;
    __imp__sub_825E4300(ctx, base);
}

// SWA::CCsdPlatformMirage::EndScene
PPC_FUNC_IMPL(__imp__sub_825E2F78);
PPC_FUNC(sub_825E2F78)
{
    g_csdFilterState = CsdFilterState::Unknown;
    __imp__sub_825E2F78(ctx, base);
}

// Game shares surfaces with identical descriptions. We don't want to share shadow maps,
// so we can set its format to a depth format that still resolves to the same type in recomp,
// but manages to keep the surfaces actually separated in guest code.
void FxShadowMapInitMidAsmHook(PPCRegister& r11)
{
    uint8_t* base = g_memory.base;

    uint32_t surface = PPC_LOAD_U32(PPC_LOAD_U32(PPC_LOAD_U32(r11.u32 + 0x24) + 0x4));
    PPC_STORE_U32(surface + 0x20, D3DFMT_D24FS8);
}

// Re-render objects in the terrain shadow map instead of copying the texture.
static bool g_jumpOverStretchRect;

void FxShadowMapNoTerrainMidAsmHook(PPCRegister& r4, PPCRegister& r30)
{
    // Set the no terrain shadow map as the render target.
    uint8_t* base = g_memory.base;
    r4.u64 = PPC_LOAD_U32(r30.u32 + 0x58);
}

bool FxShadowMapMidAsmHook(PPCRegister& r4, PPCRegister& r5, PPCRegister& r6, PPCRegister& r30)
{
    if (g_jumpOverStretchRect)
    {
        // Reset for the next time shadow maps get rendered.
        g_jumpOverStretchRect = false;

        // Jump over the stretch rect call.
        return false;
    }
    else
    {
        // Mark to jump over the stretch call the next time.
        g_jumpOverStretchRect = true;

        // Jump to the beginning. Set registers accordingly to set the terrain shadow map as the render target.
        uint8_t* base = g_memory.base;
        r6.u64 = 0;
        r5.u64 = 0;
        r4.u64 = PPC_LOAD_U32(r30.u32 + 0x50);

        return true;
    }
}

// There is a bug on AMD where restart indices cause incorrect culling and prevent some triangles from being rendered.
// This seems to happen on both Windows AMD drivers and Mesa. Converting restart indices to degenerate triangles fixes it.
static void ConvertToDegenerateTriangles(uint16_t* indices, uint32_t indexCount, uint16_t*& newIndices, uint32_t& newIndexCount)
{
    newIndices = reinterpret_cast<uint16_t*>(g_userHeap.Alloc(indexCount * sizeof(uint16_t) * 3));
    newIndexCount = 0;

    bool stripStart = true;
    uint32_t stripSize = 0;
    uint16_t lastIndex = 0;

    for (uint32_t i = 0; i < indexCount; i++)
    {
        uint16_t index = indices[i];
        if (index == 0xFFFF)
        {
            if ((stripSize % 2) != 0)
                newIndices[newIndexCount++] = lastIndex;

            stripStart = true;
            stripSize = 0;
        }
        else 
        {
            if (stripStart && newIndexCount != 0)
            {
                newIndices[newIndexCount++] = lastIndex;
                newIndices[newIndexCount++] = index;
            }

            newIndices[newIndexCount++] = index;
            stripStart = false;
            ++stripSize;
            lastIndex = index;
        }
    }
}

struct MeshResource
{
    SWA_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

static std::vector<uint16_t*> g_newIndicesToFree;

// Hedgehog::Mirage::CMeshData::Make
PPC_FUNC_IMPL(__imp__sub_82E44AF8);
PPC_FUNC(sub_82E44AF8)
{
    uint16_t* newIndicesToFree = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto meshResource = reinterpret_cast<MeshResource*>(base + ctx.r4.u32);

        if (meshResource->indexCount != 0)
        {
            uint16_t* newIndices;
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + meshResource->indices),
                meshResource->indexCount,
                newIndices,
                newIndexCount);

            meshResource->indexCount = newIndexCount;
            meshResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);

            if (PPC_LOAD_U32(0x83396E98) != NULL)
            {
                // If index buffers are getting merged, new indices need to survive until the merge happens.
                g_newIndicesToFree.push_back(newIndices);
            }
            else 
            {
                // Otherwise, we can free it immediately.
                newIndicesToFree = newIndices;
            }
        }
    }

    __imp__sub_82E44AF8(ctx, base);

    if (newIndicesToFree != nullptr)
        g_userHeap.Free(newIndicesToFree);
}

// Hedgehog::Mirage::CShareVertexBuffer::Reset
PPC_FUNC_IMPL(__imp__sub_82E250D0);
PPC_FUNC(sub_82E250D0)
{
    __imp__sub_82E250D0(ctx, base);

    for (auto newIndicesToFree : g_newIndicesToFree)
        g_userHeap.Free(newIndicesToFree);

    g_newIndicesToFree.clear();
}

struct LightAndIndexBufferResourceV1
{
    SWA_INSERT_PADDING(0x4);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV1
PPC_FUNC_IMPL(__imp__sub_82E3AFC8);
PPC_FUNC(sub_82E3AFC8)
{
    uint16_t* newIndices = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV1*>(base + ctx.r4.u32);

        if (lightAndIndexBufferResource->indexCount != 0)
        {
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
                lightAndIndexBufferResource->indexCount,
                newIndices,
                newIndexCount);

            lightAndIndexBufferResource->indexCount = newIndexCount;
            lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
        }
    }

    __imp__sub_82E3AFC8(ctx, base);

    if (newIndices != nullptr)
        g_userHeap.Free(newIndices);
}

struct LightAndIndexBufferResourceV5
{
    SWA_INSERT_PADDING(0x8);
    be<uint32_t> indexCount;
    be<uint32_t> indices;
};

// Hedgehog::Mirage::CLightAndIndexBufferData::MakeV5
PPC_FUNC_IMPL(__imp__sub_82E3B1C0);
PPC_FUNC(sub_82E3B1C0)
{
    uint16_t* newIndices = nullptr;

    auto databaseData = reinterpret_cast<Hedgehog::Database::CDatabaseData*>(base + ctx.r3.u32);
    if (g_triangleStripWorkaround && !databaseData->IsMadeOne())
    {
        auto lightAndIndexBufferResource = reinterpret_cast<LightAndIndexBufferResourceV5*>(base + ctx.r4.u32);

        if (lightAndIndexBufferResource->indexCount != 0)
        {
            uint32_t newIndexCount;

            ConvertToDegenerateTriangles(
                reinterpret_cast<uint16_t*>(base + lightAndIndexBufferResource->indices),
                lightAndIndexBufferResource->indexCount,
                newIndices,
                newIndexCount);

            lightAndIndexBufferResource->indexCount = newIndexCount;
            lightAndIndexBufferResource->indices = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(newIndices) - base);
        }
    }

    __imp__sub_82E3B1C0(ctx, base);

    if (newIndices != nullptr)
        g_userHeap.Free(newIndices);
}

GUEST_FUNCTION_HOOK(sub_82BD99B0, CreateDevice);

GUEST_FUNCTION_HOOK(sub_82BE6230, DestructResource);

GUEST_FUNCTION_HOOK(sub_82BE9300, LockTextureRect);
GUEST_FUNCTION_HOOK(sub_82BE7780, UnlockTextureRect);

GUEST_FUNCTION_HOOK(sub_82BE6B98, LockVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6BE8, UnlockVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE61D0, GetVertexBufferDesc);

GUEST_FUNCTION_HOOK(sub_82BE6CA8, LockIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6CF0, UnlockIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6200, GetIndexBufferDesc);

GUEST_FUNCTION_HOOK(sub_82BE96F0, GetSurfaceDesc);

GUEST_FUNCTION_HOOK(sub_82BE04B0, GetVertexDeclaration);
GUEST_FUNCTION_HOOK(sub_82BE0530, HashVertexDeclaration);

GUEST_FUNCTION_HOOK(sub_82BDA8C0, Video::Present);
GUEST_FUNCTION_HOOK(sub_82BDD330, GetBackBuffer);

GUEST_FUNCTION_HOOK(sub_82BE9498, CreateTexture);
GUEST_FUNCTION_HOOK(sub_82BE6AD0, CreateVertexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE6BF8, CreateIndexBuffer);
GUEST_FUNCTION_HOOK(sub_82BE95B8, CreateSurface);

GUEST_FUNCTION_HOOK(sub_82BF6400, StretchRect);

GUEST_FUNCTION_HOOK(sub_82BDD9F0, SetRenderTarget);
GUEST_FUNCTION_HOOK(sub_82BDDD38, SetDepthStencilSurface);

GUEST_FUNCTION_HOOK(sub_82BFE4C8, Clear);

GUEST_FUNCTION_HOOK(sub_82BDD8C0, SetViewport);

GUEST_FUNCTION_HOOK(sub_82BE9818, SetTexture);
GUEST_FUNCTION_HOOK(sub_82BDCFB0, SetScissorRect);

GUEST_FUNCTION_HOOK(sub_82BE5900, DrawPrimitive);
GUEST_FUNCTION_HOOK(sub_82BE5CF0, DrawIndexedPrimitive);
GUEST_FUNCTION_HOOK(sub_82BE52F8, DrawPrimitiveUP);

GUEST_FUNCTION_HOOK(sub_82BE0428, CreateVertexDeclaration);
GUEST_FUNCTION_HOOK(sub_82BE02E0, SetVertexDeclaration);

GUEST_FUNCTION_HOOK(sub_82BE1A80, CreateVertexShader);
GUEST_FUNCTION_HOOK(sub_82BE0110, SetVertexShader);

GUEST_FUNCTION_HOOK(sub_82BDD0F8, SetStreamSource);
GUEST_FUNCTION_HOOK(sub_82BDD218, SetIndices);

GUEST_FUNCTION_HOOK(sub_82BE1990, CreatePixelShader);
GUEST_FUNCTION_HOOK(sub_82BDFE58, SetPixelShader);

GUEST_FUNCTION_HOOK(sub_82C003B8, D3DXFillTexture);
GUEST_FUNCTION_HOOK(sub_82C00910, D3DXFillVolumeTexture);

GUEST_FUNCTION_HOOK(sub_82E43FC8, MakePictureData);

GUEST_FUNCTION_HOOK(sub_82E9EE38, SetResolution);

GUEST_FUNCTION_HOOK(sub_82AE2BF8, ScreenShaderInit);

// This is a buggy function that recreates framebuffers
// if the inverse capture ratio is not 2.0, but the parameter
// is completely unused and not stored, so it ends up
// recreating framebuffers every single frame instead.
GUEST_FUNCTION_STUB(sub_82BAAD38);

GUEST_FUNCTION_STUB(sub_822C15D8);
GUEST_FUNCTION_STUB(sub_822C1810);
GUEST_FUNCTION_STUB(sub_82BD97A8);
GUEST_FUNCTION_STUB(sub_82BD97E8);
GUEST_FUNCTION_STUB(sub_82BDD370); // SetGammaRamp
GUEST_FUNCTION_STUB(sub_82BE05B8);
GUEST_FUNCTION_STUB(sub_82BE9C98);
GUEST_FUNCTION_STUB(sub_82BEA308);
GUEST_FUNCTION_STUB(sub_82CD5D68);
GUEST_FUNCTION_STUB(sub_82BE9B28);
GUEST_FUNCTION_STUB(sub_82BEA018);
GUEST_FUNCTION_STUB(sub_82BEA7C0);
GUEST_FUNCTION_STUB(sub_82BFFF88); // D3DXFilterTexture
GUEST_FUNCTION_STUB(sub_82BD96D0);







