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
#include "RenderGraph/RenderPass.h"
#include "../DitherVBuffer/DitherVBuffer.h"

using namespace Falcor;

class DitherVBufferRaster : public RenderPass
{
    using DitherMode = DitherVBuffer::DitherMode;
    using CoverageCorrection = DitherVBuffer::CoverageCorrection;
    using ObjectHashType = DitherVBuffer::ObjectHashType;
    using NoisePattern = DitherVBuffer::NoisePattern;
    using NoiseTopPattern = DitherVBuffer::NoiseTopPattern;
    using DitherPattern = DitherVBuffer::DitherPattern;
public:
    FALCOR_PLUGIN_CLASS(DitherVBufferRaster, "DitherVBufferRaster", "Insert pass description here.");

    static ref<DitherVBufferRaster> create(ref<Device> pDevice, const Properties& props) { return make_ref<DitherVBufferRaster>(pDevice, props); }

    DitherVBufferRaster(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override {}
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

private:
    void setFractalDitherPattern(DitherPattern pattern);
    void setupProgram();
    // returns true if at least one material was whitelisted (or scene was invalid)
    bool updateWhitelistBuffer();
    void createNoisePattern();

    ref<Scene> mpScene;
    ref<Buffer> mpTransparencyWhitelist;
    ref<Buffer> mpPermutations3x3Buffer;

    uint mFrameCount = 0;
    ref<CPUSampleGenerator> mpSamplePattern;

    DitherMode mDitherMode = DitherMode::PerPixel3x3;
    bool mUseAlphaTextureLOD = false; // use lod for alpha lookups
    bool mUseTransparencyWhitelist = false;
    std::set<std::string> mTransparencyWhitelist;
    CoverageCorrection mCoverageCorrection = CoverageCorrection::DLSS;
    float mDLSSCorrectionStrength = 1.0;
    DitherPattern mFractalDitherPattern = DitherPattern::Dither8x8;
    float mGridScale = 0.5f;
    ObjectHashType mObjectHashType = ObjectHashType::Geometry;

    ref<GraphicsState> mpState;
    ref<GraphicsProgram> mpProgram;
    ref<GraphicsVars> mpVars;
    ref<Fbo> mpFbo;

    ref<Texture> mpFracDitherTex;
    ref<Texture> mpFracDitherRampTex;
    ref<Sampler> mpFracSampler;
    ref<Texture> mpNoiseTex;
    ref<Sampler> mpNoiseSampler;
    ref<Texture> mpBlueNoise3DTex;
    ref<Texture> mpBlueNoise64Tex;
    ref<Texture> mpBayer64Tex;

    NoisePattern mNoisePattern = NoisePattern::Blue;
    NoiseTopPattern mNoiseTopPattern = NoiseTopPattern::StaticBlue;
    bool mCullBackFaces = false;
    bool mAlignMotionVectors = false; // align when using pixel grid techniques
    bool mRotatePattern = true; // rotate pattern when using pixel grid techniques
    bool mDitherTAAPermutations = true;
};
