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
#include "DecimaTAA.h"

namespace
{
    const std::string kColorIn = "colorIn"; // only for disable option
    const std::string kFxaaIn = "fxaaIn"; // fxaa/smaa color from previous frame"
    const std::string kMotionVec = "mvec";
    const std::string kTaaOut = "taaOut";

    const std::string kColorBoxSigma = "colorBoxSigma";

    const std::string kSharpenPass = "RenderPasses/DecimaTAA/Sharpen.ps.slang";
    const std::string kTAAPass = "RenderPasses/DecimaTAA/TAA.ps.slang";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DecimaTAA>();
}

DecimaTAA::DecimaTAA(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpFbo = Fbo::create(pDevice);
    mpLinearSampler = Sampler::create(pDevice, Sampler::Desc()
        .setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Point)
        .setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp));
    mpSharpenPass = FullScreenPass::create(pDevice, kSharpenPass);
    mpTAAPass = FullScreenPass::create(pDevice, kTAAPass);
}

Properties DecimaTAA::getProperties() const
{
    return {};
}

RenderPassReflection DecimaTAA::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addInput(kColorIn, "Aliased Color (only used when disabled)");
    reflector.addInput(kFxaaIn, "Anti-Aliased FXAA/SMAA Color");
    reflector.addInput(kMotionVec, "Motion Vectors 2D");

    reflector.addOutput(kTaaOut, "TAA Output");
    return reflector;
}

void DecimaTAA::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    const auto& pColorIn = renderData.getTexture(kColorIn);
    const auto& pFxaaIn = renderData.getTexture(kFxaaIn);
    const auto& pMotionVec = renderData.getTexture(kMotionVec);
    const auto& pTaaOut = renderData.getTexture(kTaaOut);

    allocatePrevColor(pTaaOut.get());

    // obtain jitter direction for current frame (expect (-0.5, 0.0) or (0.0, -0.5) values)
    float2 jitterDir = float2(0.0f, 0.0f);
    if(mpScene)
    {
        auto cam = mpScene->getCamera();
        if (abs(cam->getJitterX()) > abs(cam->getJitterY()))
            jitterDir.x = 1.0f;
        else
            jitterDir.y = 1.0f;
    }

    {
        FALCOR_PROFILE(pRenderContext, "Sharpen");
        mpFbo->attachColorTarget(mpCurSharp, 0);

        auto var = mpSharpenPass->getRootVar();
        var["gInput"] = pFxaaIn;
        var["gSampler"] = mpLinearSampler;
        var["PerFrameCB"]["dir"] = jitterDir;
        var["PerFrameCB"]["gSharpen"] = mControls.sharpen;

        mpSharpenPass->execute(pRenderContext, mpFbo);
    }

    {
        FALCOR_PROFILE(pRenderContext, "TAA");
        mpFbo->attachColorTarget(pTaaOut, 0);

        auto var = mpTAAPass->getRootVar();
        var["gCurColor"] = mpCurSharp;
        var["gPrevColor"] = mpPrevColor;
        var["gMotionVec"] = pMotionVec;
        var["gSampler"] = mpLinearSampler;
        var["PerFrameCB"]["gColorBoxSigma"] = mControls.colorBoxSigma;

        mpTAAPass->execute(pRenderContext, mpFbo);

        std::swap(mpPrevColor, mpCurSharp); // copy current sharp image to previous color
    }

    if(!mEnabled)
    {
        pRenderContext->blit(pColorIn->getSRV(), pTaaOut->getRTV());
        return;
    }
}

void DecimaTAA::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    if (!mEnabled) return;

    widget.var("Color-Box Sigma", mControls.colorBoxSigma, 0.f, 500.f, 0.001f);
    widget.slider("Sharpen", mControls.sharpen, 0.f, 1.f);
}

void DecimaTAA::allocatePrevColor(const Texture* pColorOut)
{
    bool allocate = mpPrevColor == nullptr;
    allocate = allocate || (mpPrevColor->getWidth() != pColorOut->getWidth());
    allocate = allocate || (mpPrevColor->getHeight() != pColorOut->getHeight());
    allocate = allocate || (mpPrevColor->getDepth() != pColorOut->getDepth());
    allocate = allocate || (mpPrevColor->getFormat() != pColorOut->getFormat());
    FALCOR_ASSERT(pColorOut->getSampleCount() == 1);

    if (!allocate) return;
    mpPrevColor = Texture::create2D(mpDevice, pColorOut->getWidth(), pColorOut->getHeight(), pColorOut->getFormat(), 1, 1, nullptr, Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource);
    mpCurSharp = Texture::create2D(mpDevice, pColorOut->getWidth(), pColorOut->getHeight(), pColorOut->getFormat(), 1, 1, nullptr, Resource::BindFlags::RenderTarget | Resource::BindFlags::ShaderResource);
}
