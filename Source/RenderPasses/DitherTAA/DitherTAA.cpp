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
#include "DitherTAA.h"

namespace
{
    const std::string kMotionVec = "motionVecs";
    const std::string kColorIn = "colorIn";
    const std::string kOpacity = "opacity";
    const std::string kColorOut = "colorOut";

    const std::string kAlpha = "alpha";
    const std::string kColorBoxSigma = "colorBoxSigma";

    const std::string kShaderFilename = "RenderPasses/DitherTAA/DitherTAA.ps.slang";
}


extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DitherTAA>();
}

DitherTAA::DitherTAA(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpPass = FullScreenPass::create(mpDevice, kShaderFilename);
    mpFbo = Fbo::create(mpDevice);
    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearSampler = Sampler::create(mpDevice, samplerDesc);
    //samplerDesc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    //mpPointSampler = Sampler::create(mpDevice, samplerDesc);

    for (const auto& [key, value] : props)
    {
        if (key == kAlpha) mControls.alpha = value;
        else if (key == kColorBoxSigma) mControls.colorBoxSigma = value;
        else logWarning("Unknown property '{}' in a DitherTAA properties.", key);
    }
}

Properties DitherTAA::getProperties() const
{
    Properties props;
    props[kAlpha] = mControls.alpha;
    props[kColorBoxSigma] = mControls.colorBoxSigma;
    return props;
}

RenderPassReflection DitherTAA::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    reflection.addInput(kColorIn, "Color-buffer of the current frame");
    reflection.addInput(kMotionVec, "Screen-space motion vectors");
    reflection.addInput(kOpacity, "Opacity mask. 0 = opaque, 1 = transparent").flags(RenderPassReflection::Field::Flags::Optional);

    reflection.addOutput(kColorOut, "Anti-aliased color buffer").format(ResourceFormat::RGBA32Float);
    return reflection;
}

void DitherTAA::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    const auto& pColorIn = renderData.getTexture(kColorIn);
    const auto& pColorOut = renderData.getTexture(kColorOut);
    const auto& pMotionVec = renderData.getTexture(kMotionVec);
    const auto& pOpacity = renderData.getTexture(kOpacity);

    if (!mEnabled)
    {
        pRenderContext->blit(pColorIn->getSRV(), pColorOut->getRTV());
        return;
    }

    allocatePrevColorAndHistory(pColorOut.get());
    mpFbo->attachColorTarget(pColorOut, 0);

    {
        FALCOR_PROFILE(pRenderContext, "TAA");

        auto var = mpPass->getRootVar();
        var["PerFrameCB"]["gAlpha"] = mControls.alpha;
        var["PerFrameCB"]["gColorBoxSigma"] = mControls.colorBoxSigma;
        var["PerFrameCB"]["gUseBoundingBoxForTransparency"] = mControls.useBoundingBoxForTransparency ? 1 : 0;
        var["PerFrameCB"]["gTransparencyRadius"] = mControls.transparencyRadius;
        var["gTexColor"] = pColorIn;
        var["gTexMotionVec"] = pMotionVec;
        var["gTexOpacity"] = pOpacity;
        var["gTexPrevColor"] = mpPrevColor;
        var["gSampler"] = mpLinearSampler;

        mpPass->execute(pRenderContext, mpFbo);
    }

    {
        FALCOR_PROFILE(pRenderContext, "Blit History");
        pRenderContext->blit(pColorOut->getSRV(), mpPrevColor->getRTV());
    }

    if (mClear)
    {
        pRenderContext->blit(pColorIn->getSRV(), mpPrevColor->getRTV());
        mClear = false;
    }
}

void DitherTAA::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    if (!mEnabled) return;

    widget.var("Color-Box Sigma", mControls.colorBoxSigma, 0.f, 15.f, 0.001f);
    widget.var("Alpha", mControls.alpha, 0.f, 1.0f, 0.001f);

    widget.checkbox("Use BBOX for Transparency", mControls.useBoundingBoxForTransparency);
    widget.slider("Transparency Radius", mControls.transparencyRadius, 1, 5);

    if (widget.button("Clear"))
        mClear = true;
}

void DitherTAA::allocatePrevColorAndHistory(const Texture* pColorOut)
{
    bool allocate = mpPrevColor == nullptr;
    allocate = allocate || (mpPrevColor->getWidth() != pColorOut->getWidth());
    allocate = allocate || (mpPrevColor->getHeight() != pColorOut->getHeight());
    allocate = allocate || (mpPrevColor->getDepth() != pColorOut->getDepth());
    allocate = allocate || (mpPrevColor->getFormat() != pColorOut->getFormat());
    FALCOR_ASSERT(pColorOut->getSampleCount() == 1);

    if (!allocate) return;
    mpPrevColor = Texture::create2D(mpDevice, pColorOut->getWidth(), pColorOut->getHeight(), pColorOut->getFormat(), 1, 1, nullptr, Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource);
}
