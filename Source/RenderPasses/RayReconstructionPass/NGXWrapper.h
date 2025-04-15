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
#pragma once
#include "Falcor.h"

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers_dlssd.h>

#include <filesystem>

 // Forward declarations from NGX library.
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace Falcor
{
    /**
     * This is a wrapper around the NGX functionality for DLSS.
     * It is seperated to provide focus to the calls specific to NGX for code sample purposes.
     */
    class NGXWrapper
    {
    public:
        /// Constructor. Throws an exception if unable to initialize NGX.
        NGXWrapper(ref<Device> pDevice, const std::filesystem::path& applicationDataPath, const std::filesystem::path& featureSearchPath);
        ~NGXWrapper();

        /// Initialize DLSSD. Throws an exception if unable to initialize.
        /// renderSize: input
        /// displaySize: output
        void initializeDLSSD(
            RenderContext* pRenderContext,
            uint2 renderSize,
            uint2 displaySize,
            bool isContentHDR,
            bool depthInverted,
            bool useMVJitteredFlag
        );

        /// Release DLSSD.
        void releaseDLSSD();

        /// Checks if DLSSD is initialized.
        bool isDLSSDInitialized() const { return mpFeature != nullptr; }

        //// Evaluate DLSSD.
        bool evaluateDLSSD(
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
        ) const;

    private:
        void initializeNGX(const std::filesystem::path& applicationDataPath, const std::filesystem::path& featureSearchPath);
        void shutdownNGX();

        ref<Device> mpDevice;
        bool mInitialized = false;

        NVSDK_NGX_Parameter* mpParameters = nullptr;
        NVSDK_NGX_Handle* mpFeature = nullptr;
    };
} // namespace Falcor
