/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "NGXWrapper.h"
#include "Core/API/NativeHandleTraits.h"
#include "Core/API/NativeFormats.h"

#include <nvsdk_ngx_defs_dlssd.h>

#if FALCOR_HAS_D3D12
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

#if FALCOR_HAS_VULKAN
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>
#endif

#include <cstdio>
#include <cstdarg>

#define THROW_IF_FAILED(call)                                                           \
    {                                                                                   \
        NVSDK_NGX_Result result_ = call;                                                \
        if (NVSDK_NGX_FAILED(result_))                                                  \
            throw RuntimeError(#call " failed with error {}", resultToString(result_)); \
    }

namespace Falcor
{
    namespace
    {
        const uint64_t kAppID = 231313132;

        std::string resultToString(NVSDK_NGX_Result result)
        {
            char buf[1024];
            snprintf(buf, sizeof(buf), "(code: 0x%08x, info: %ls)", result, GetNGXResultAsString(result));
            buf[sizeof(buf) - 1] = '\0';
            return std::string(buf);
        }

#if FALCOR_HAS_VULKAN
        VkImageAspectFlags getAspectMaskFromFormat(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_D16_UNORM_S8_UINT:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case VK_FORMAT_S8_UINT:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }
#endif

    } // namespace

    NGXWrapper::NGXWrapper(
        ref<Device> pDevice,
        const std::filesystem::path& applicationDataPath,
        const std::filesystem::path& featureSearchPath
    )
        : mpDevice(pDevice)
    {
        initializeNGX(applicationDataPath, featureSearchPath);
    }

    NGXWrapper::~NGXWrapper()
    {
        shutdownNGX();
    }

    void NGXWrapper::initializeNGX(const std::filesystem::path& applicationDataPath, const std::filesystem::path& featureSearchPath)
    {
        NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;

        NVSDK_NGX_FeatureCommonInfo featureInfo = {};
        const wchar_t* pathList[] = { featureSearchPath.c_str() };
        featureInfo.PathListInfo.Length = 1;
        featureInfo.PathListInfo.Path = const_cast<wchar_t**>(&pathList[0]);

        switch (mpDevice->getType())
        {
        case Device::Type::D3D12:
#if FALCOR_HAS_D3D12
            result = NVSDK_NGX_D3D12_Init(kAppID, applicationDataPath.c_str(), mpDevice->getNativeHandle().as<ID3D12Device*>(), &featureInfo);
#endif
            break;
        case Device::Type::Vulkan:
#if FALCOR_HAS_VULKAN
            result = NVSDK_NGX_VULKAN_Init(
                kAppID, applicationDataPath.c_str(), mpDevice->getNativeHandle(0).as<VkInstance>(),
                mpDevice->getNativeHandle(1).as<VkPhysicalDevice>(), mpDevice->getNativeHandle(2).as<VkDevice>(), nullptr, nullptr, &featureInfo
            );
#endif
            break;
        }

        if (NVSDK_NGX_FAILED(result))
        {
            if (result == NVSDK_NGX_Result_FAIL_FeatureNotSupported || result == NVSDK_NGX_Result_FAIL_PlatformError)
            {
                throw RuntimeError("NVIDIA NGX is not available on this hardware/platform " + resultToString(result));
            }
            else
            {
                throw RuntimeError("Failed to initialize NGX " + resultToString(result));
            }
        }

        mInitialized = true;

        switch (mpDevice->getType())
        {
        case Device::Type::D3D12:
#if FALCOR_HAS_D3D12
            THROW_IF_FAILED(NVSDK_NGX_D3D12_GetCapabilityParameters(&mpParameters));
#endif
            break;
        case Device::Type::Vulkan:
#if FALCOR_HAS_VULKAN
            THROW_IF_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&mpParameters));
#endif
            break;
        }

        int dlssAvailable = 0;
        result = mpParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
        if (NVSDK_NGX_FAILED(result) || !dlssAvailable)
        {
            throw RuntimeError("NVIDIA DLSS not available on this hardward/platform " + resultToString(result));
        }
    }

    void NGXWrapper::shutdownNGX()
    {
        if (mInitialized)
        {
            mpDevice->flushAndSync();

            if (mpFeature != nullptr)
                releaseDLSSD();

            switch (mpDevice->getType())
            {
            case Device::Type::D3D12:
#if FALCOR_HAS_D3D12
                NVSDK_NGX_D3D12_DestroyParameters(mpParameters);
                NVSDK_NGX_D3D12_Shutdown1(mpDevice->getNativeHandle().as<ID3D12Device*>());
#endif
                break;
            case Device::Type::Vulkan:
#if FALCOR_HAS_VULKAN
                THROW_IF_FAILED(NVSDK_NGX_VULKAN_DestroyParameters(mpParameters));
                THROW_IF_FAILED(NVSDK_NGX_VULKAN_Shutdown1(mpDevice->getNativeHandle(2).as<VkDevice>()));
#endif
                break;
            }

            mInitialized = false;
        }
    }



    void NGXWrapper::initializeDLSSD(
        RenderContext* pRenderContext,
        uint2 renderSize,
        uint2 displaySize,
        bool isContentHDR,
        bool depthInverted,
        bool useMVJitteredFlag
    )
    {
        unsigned int creationNodeMask = 1;
        unsigned int visibilityNodeMask = 1;

        // Next create features
        int createFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
        createFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        createFlags |= useMVJitteredFlag ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0;
        createFlags |= isContentHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0;
        createFlags |= depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0;

        NVSDK_NGX_DLSSD_Create_Params params = {};
        params.InWidth = renderSize.x;
        params.InHeight = renderSize.y;
        params.InTargetWidth = displaySize.x;
        params.InTargetHeight = displaySize.y;
        params.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
        params.InFeatureCreateFlags = createFlags;
        // new flags
        params.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        params.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked; // roughness is extra texture
        params.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear; // linear depth

        switch (mpDevice->getType())
        {
        case Device::Type::D3D12:
        {
#if FALCOR_HAS_D3D12
            pRenderContext->flush();
            ID3D12GraphicsCommandList* pCommandList =
                pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
            THROW_IF_FAILED(NGX_D3D12_CREATE_DLSSD_EXT(pCommandList, creationNodeMask, visibilityNodeMask, &mpFeature, mpParameters, &params)
            );
            pRenderContext->flush();
#endif
            break;
        }
        case Device::Type::Vulkan:
        {
#if FALCOR_HAS_VULKAN
            return throw std::runtime_error("DLSSD not implemented in vulkan");
            pRenderContext->flush();
            VkCommandBuffer vkCommandBuffer = pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<VkCommandBuffer>();
            //THROW_IF_FAILED(
            //    NGX_VULKAN_CREATE_DLSSD_EXT1(vkCommandBuffer, creationNodeMask, visibilityNodeMask, &mpFeature, mpParameters, &dlssParams)
            //); // comment in if available
            pRenderContext->flush();
#endif
            break;
        }
        }
    }

    void NGXWrapper::releaseDLSSD()
    {
        if (mpFeature)
        {
            mpDevice->flushAndSync();

            switch (mpDevice->getType())
            {
            case Device::Type::D3D12:
#if FALCOR_HAS_D3D12
                NVSDK_NGX_D3D12_ReleaseFeature(mpFeature);
#endif
                break;
            case Device::Type::Vulkan:
#if FALCOR_HAS_VULKAN
                THROW_IF_FAILED(NVSDK_NGX_VULKAN_ReleaseFeature(mpFeature));
#endif
                break;
            }
            mpFeature = nullptr;
        }
    }

    bool NGXWrapper::evaluateDLSSD(
        RenderContext* pRenderContext,
        Texture* pDiffAlbedo,
        Texture* pSpecAlbedo,
        Texture* pNormal,
        Texture* pRoughness,
        Texture* pColorIn,
        Texture* pMotion,
        Texture* pSpecMotion,
        Texture* pDepth,
        Texture* pSpecHitDist,
        Texture* pTransparent,
        Texture* pOut,
        bool resetAccumulation,
        float2 jitterOffset,
        float2 motionVectorScale,
        float4x4 viewMatrix,
        float4x4 projectionMatrix
    ) const
    {
        assert(mpFeature);
        if (!mpFeature)
            return false;

        bool success = true;

        auto inputs = {
            pDiffAlbedo,
            pSpecAlbedo,
            pNormal,
            pRoughness,
            pColorIn,
            pMotion,
            pSpecMotion,
            pDepth,
            pSpecHitDist,
            pTransparent
        };

        switch (mpDevice->getType())
        {
        case Device::Type::D3D12:
        {
#if FALCOR_HAS_D3D12
            for(auto pTex : inputs)
            {
                if(pTex) pRenderContext->resourceBarrier(pTex, Resource::State::ShaderResource);
            }
            pRenderContext->resourceBarrier(pOut, Resource::State::UnorderedAccess);

            auto getHandle = [](Texture* pTex) -> ID3D12Resource*
            {
                return pTex ? pTex->getNativeHandle().as<ID3D12Resource*>() : nullptr;
            };

            NVSDK_NGX_D3D12_DLSSD_Eval_Params params = {};
            params.pInDiffuseAlbedo = getHandle(pDiffAlbedo);
            params.pInSpecularAlbedo = getHandle(pSpecAlbedo);
            params.pInNormals = getHandle(pNormal);
            params.pInRoughness = getHandle(pRoughness);
            params.pInColor = getHandle(pColorIn);
            params.pInOutput = getHandle(pOut);
            params.pInDepth = getHandle(pDepth);
            params.pInMotionVectors = getHandle(pMotion);
            params.InJitterOffsetX = jitterOffset.x;
            params.InJitterOffsetY = jitterOffset.y;

            params.InReset = resetAccumulation ? 1 : 0;
            params.InMVScaleX = motionVectorScale.x;
            params.InMVScaleY = motionVectorScale.y;
            params.pInExposureTexture = nullptr; // not supported

            params.pInMotionVectorsReflections = getHandle(pSpecMotion);
            // pInDlssDEvalParams->GBufferSurface.pInAttrib[NVSDK_NGX_GBUFFER_SPECULAR_MVEC] ?
            params.pInSpecularHitDistance = getHandle(pSpecHitDist);
            params.pInWorldToViewMatrix = reinterpret_cast<float*>(&viewMatrix);
            params.pInViewToClipMatrix = reinterpret_cast<float*>(&projectionMatrix);
            params.pInTransparencyLayer = getHandle(pTransparent);

            params.InRenderSubrectDimensions.Width = pColorIn->getWidth();
            params.InRenderSubrectDimensions.Height = pColorIn->getHeight();

            ID3D12GraphicsCommandList* pCommandList =
                pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
            NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSSD_EXT(pCommandList, mpFeature, mpParameters, &params);
            if (NVSDK_NGX_FAILED(result))
            {
                logWarning("Failed to NGX_D3D12_EVALUATE_DLSSD_EXT for DLSSD: {}", resultToString(result));
                success = false;
            }

            pRenderContext->setPendingCommands(true);
            pRenderContext->uavBarrier(pOut);
            // TODO: Get rid of the flush
            pRenderContext->flush();
#endif // FALCOR_HAS_D3D12
            break;
        }
        case Device::Type::Vulkan:
        {
#if FALCOR_HAS_VULKAN
            throw std::runtime_error("DLSSD not implemented in vulkan");
#endif
            break;
        }
        }

        return success;
    }

} // namespace Falcor
