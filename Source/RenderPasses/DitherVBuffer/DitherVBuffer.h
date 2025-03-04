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
#include "Utils/SampleGenerators/HaltonSamplePattern.h"
#include "SobolGenerator.h"
#include "Utils/SampleGenerators/StratifiedSamplePattern.h"

using namespace Falcor;

class DitherVBuffer : public RenderPass
{
public:
    enum class DitherMode : uint32_t
    {
        PerPixel2x2,
        PerPixel3x3,
        PerPixel4x4,
        PerJitter,
        RussianRoulette,
        Periodic,
        HashGrid,
        FractalDithering,
        Disabled = 0xff,
    };

    FALCOR_ENUM_INFO(DitherMode, {
        { DitherMode::Disabled, "Disabled" },
        { DitherMode::PerPixel2x2, "PerPixel2x2" },
        { DitherMode::PerPixel3x3, "PerPixel3x3" },
        { DitherMode::PerPixel4x4, "PerPixel4x4" },
        { DitherMode::PerJitter, "PerJitter" },
        { DitherMode::RussianRoulette, "RussianRoulette" },
        { DitherMode::Periodic, "Periodic" },
        { DitherMode::HashGrid, "HashGrid" },
        { DitherMode::FractalDithering, "FractalDithering" },
    });

    enum class TemporalDitherMode
    {
        Disabled,
        Uniform,
        VanDerCorput, // equidistributed sequence (but we use a cyclic subset)
        CyclicAlpha // potentially cyclic, but equidistributed if alpha is irrational
    };

    FALCOR_ENUM_INFO(TemporalDitherMode, {
        { TemporalDitherMode::Disabled, "Disabled" },
        { TemporalDitherMode::Uniform, "Uniform" },
        { TemporalDitherMode::VanDerCorput, "VanDerCorput" },
        { TemporalDitherMode::CyclicAlpha, "CyclicAlpha" },
    });

    enum class DitherPattern : uint32_t
    {
        Dither2x2,
        Dither4x4,
        Dither8x8,
    };

    FALCOR_ENUM_INFO(DitherPattern, {
       { DitherPattern::Dither2x2, "Dither2x2" },
       { DitherPattern::Dither4x4, "Dither4x4" },
       { DitherPattern::Dither8x8, "Dither8x8" },
    });

    enum class CoverageCorrection : uint32_t
    {
        Disabled,
        DLSS
    };

    FALCOR_ENUM_INFO(CoverageCorrection, {
        { CoverageCorrection::Disabled, "Disabled" },
        { CoverageCorrection::DLSS, "DLSS" },
    });

    enum class SamplePattern : uint32_t
    {
        Halton,
        Stratified,
        Sobol,
        Midpoint
    };

    FALCOR_ENUM_INFO(SamplePattern, {
        { SamplePattern::Halton, "Halton"},
        { SamplePattern::Stratified, "Stratified" },
        { SamplePattern::Sobol, "Sobol"},
        { SamplePattern::Midpoint, "Midpoint"},
    });

    enum class NoisePattern : uint32_t
    {
        White,
        Blue,
        Bayer,
        BlueBayer,
        Poisson,
        Perlin
    };

    FALCOR_ENUM_INFO(NoisePattern, {
        {NoisePattern::White, "White"},
        {NoisePattern::Blue, "Blue"},
        {NoisePattern::Bayer, "Bayer"},
        {NoisePattern::BlueBayer, "BlueBayer"},
        {NoisePattern::Poisson, "Poisson"},
        {NoisePattern::Perlin, "Perlin"}
    });

    enum class ObjectHashType : uint32_t
    {
        Quads,
        Geometry,
    };

    FALCOR_ENUM_INFO(ObjectHashType, {
        {ObjectHashType::Quads, "Quads"},
        {ObjectHashType::Geometry, "Geometry"},
    });

    FALCOR_PLUGIN_CLASS(DitherVBuffer, "DitherVBuffer", "VBuffer with Dithering options for transparency");

    static ref<DitherVBuffer> create(ref<Device> pDevice, const Properties& props) { return make_ref<DitherVBuffer>(pDevice, props); }

    DitherVBuffer(ref<Device> pDevice, const Properties& props);

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
    void createStratifiedBuffers();
    // returns true if at least one material was whitelisted (or scene was invalid)
    bool updateWhitelistBuffer();
    void createSamplePattern(uint sampleCount);
    void createNoisePattern();

    ref<Scene> mpScene;
    
    ref<RtProgram> mpProgram;
    ref<RtProgramVars> mpVars;
    ref<SampleGenerator> mpSampleGenerator;
    ref<Buffer> mpStratifiedIndices;
    ref<Buffer> mpStratifiedLookUpBuffer;
    ref<Buffer> mpTransparencyWhitelist;
    ref<Buffer> mpPermutations3x3Buffer;

    uint mFrameCount = 0;

    ref<CPUSampleGenerator> mpSamplePattern;
    SamplePattern mSamplePattern = SamplePattern::Halton;

    DitherMode mDitherMode = DitherMode::PerPixel3x3;
    TemporalDitherMode mTemporalDitherMode = TemporalDitherMode::Disabled;
    uint mTemporalDitherLength = 8; // cycle of the sequence
    bool mUseAlphaTextureLOD = false; // use lod for alpha lookups
    bool mUseTransparencyWhitelist = false;
    std::set<std::string> mTransparencyWhitelist;
    CoverageCorrection mCoverageCorrection = CoverageCorrection::DLSS;
    float mDLSSCorrectionStrength = 1.0;
    DitherPattern mFractalDitherPattern = DitherPattern::Dither8x8;
    float mGridScale = 0.25f;
    ObjectHashType mObjectHashType = ObjectHashType::Geometry;

    ref<Texture> mpFracDitherTex;
    ref<Texture> mpFracDitherRampTex;
    ref<Sampler> mpFracSampler;
    ref<Texture> mpNoiseTex;
    ref<Sampler> mpNoiseSampler;
    NoisePattern mNoisePattern = NoisePattern::Blue;
    bool mCullBackFaces = false;
    float mMinVisibility = 1.0f;
    bool mAlignMotionVectors = true; // align when using pixel grid techniques
    bool mRotatePattern = true; // rotate pattern when using pixel grid techniques
    bool mAddNoiseOnPattern = true; // adds additional noise on the threshold to prevent banding
};

FALCOR_ENUM_REGISTER(DitherVBuffer::DitherMode);
FALCOR_ENUM_REGISTER(DitherVBuffer::CoverageCorrection);
FALCOR_ENUM_REGISTER(DitherVBuffer::DitherPattern);
FALCOR_ENUM_REGISTER(DitherVBuffer::SamplePattern);
FALCOR_ENUM_REGISTER(DitherVBuffer::NoisePattern);
FALCOR_ENUM_REGISTER(DitherVBuffer::ObjectHashType);
FALCOR_ENUM_REGISTER(DitherVBuffer::TemporalDitherMode);
