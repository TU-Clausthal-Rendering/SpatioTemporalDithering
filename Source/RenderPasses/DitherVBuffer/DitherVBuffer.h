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

using namespace Falcor;

class DitherVBuffer : public RenderPass
{
public:
    enum class DitherMode : uint32_t
    {
        Disabled,
        PerPixel4x,
        PerPixel16x,
        PerJitter,
        RussianRoulette,
        Periodic
    };

    FALCOR_ENUM_INFO(DitherMode, {
        { DitherMode::Disabled, "Disabled" },
        { DitherMode::PerPixel4x, "PerPixel4x" },
        { DitherMode::PerPixel16x, "PerPixel16x" },
        { DitherMode::PerJitter, "PerJitter" },
        { DitherMode::RussianRoulette, "RussianRoulette" },
        { DitherMode::Periodic, "Periodic" },
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
    void setupProgram();
    void createStratifiedBuffers();
    // returns true if at least one material was whitelisted (or scene was invalid)
    bool updateWhitelistBuffer();

    ref<Scene> mpScene;

    ref<RtProgram> mpProgram;
    ref<RtProgramVars> mpVars;
    ref<SampleGenerator> mpSampleGenerator;
    ref<Buffer> mpStratifiedIndices;
    ref<Buffer> mpStratifiedLookUpBuffer;
    ref<Buffer> mpTransparencyWhitelist;

    uint mFrameCount = 0;

    ref<HaltonSamplePattern> mpSamplePattern;
    DitherMode mDitherMode = DitherMode::PerJitter;
    bool mUseAlphaTextureLOD = false; // use lod for alpha lookups
    bool mUseTransparencyWhitelist = false;
    std::set<std::string> mTransparencyWhitelist;
    CoverageCorrection mCoverageCorrection = CoverageCorrection::Disabled;
};

FALCOR_ENUM_REGISTER(DitherVBuffer::DitherMode);
FALCOR_ENUM_REGISTER(DitherVBuffer::CoverageCorrection);
