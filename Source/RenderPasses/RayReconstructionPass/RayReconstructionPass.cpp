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
#include "RayReconstructionPass.h"

namespace
{
    const std::string kDiffAlbedo = "diffAlbedo";
    const std::string kSpecAlbedo = "specAlbedo";
    const std::string kNormal = "normal";
    const std::string kRoughness = "roughness";
    const std::string kColorIn = "color";
    const std::string kMotion = "mvec";
    const std::string kSpecMotion = "smvec";
    const std::string kDepth = "linearZ";
    const std::string kSpecHitDist = "specHitDist"; // (if smvec is not supplied)
    const std::string kTranparent = "transparent"; // optional RGB premultiplied and alpha is blending factor
    const std::string kOutput = "output";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, RayReconstructionPass>();
}

RayReconstructionPass::RayReconstructionPass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
}

Properties RayReconstructionPass::getProperties() const
{
    return {};
}

RenderPassReflection RayReconstructionPass::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addInput(kDiffAlbedo, "diffuse component of Reflectance material (RGB)").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kSpecAlbedo, "specular component of Reflectance material (RGB)").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kNormal, "Shading Normals (Normalized). Can be View Space or World Space. RGB16_FLOAT or RGB32_FLOAT").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kRoughness, "Linear Roughness (R)").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kColorIn, "Noisy Ray Traced Input Color (RGB)").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kMotion, "camera motion or the motion of dynamic objects. RG16_FLOAT or RG32_FLOAT").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kSpecMotion, "(Optional) Specular Motion Vectors (RG). Use Specular Hit Distance as alternative").bindFlags(ResourceBindFlags::ShaderResource).flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kDepth, "View Space Linear Depth (R)").bindFlags(ResourceBindFlags::ShaderResource);
    reflector.addInput(kSpecHitDist, "(Optional) World Space distance between the Specular Ray Origin and Hit Point (R)").bindFlags(ResourceBindFlags::ShaderResource).flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kTranparent, "(Optional) Transparent effects, that are excluded from the input color (RGBA). RGB must be premultiplied with Alpha, Alpha channel is the blending factor.").bindFlags(ResourceBindFlags::ShaderResource).flags(RenderPassReflection::Field::Flags::Optional);

    reflector.addOutput(kOutput, "denoised anti-aliased color").format(ResourceFormat::RGBA32Float).bindFlags(ResourceBindFlags::AllColorViews);
    return reflector;
}

void RayReconstructionPass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    mRecreate = true; // dimensions changed?
}

void RayReconstructionPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pDiffAlbedo = renderData.getTexture(kDiffAlbedo);
    auto pSpecAlbedo = renderData.getTexture(kSpecAlbedo);
    auto pNormal = renderData.getTexture(kNormal);
    auto pRoughness = renderData.getTexture(kRoughness);
    auto pColorIn = renderData.getTexture(kColorIn);
    auto pMotion = renderData.getTexture(kMotion);
    auto pSpecMotion = renderData.getTexture(kSpecMotion);
    auto pDepth = renderData.getTexture(kDepth);
    auto pSpecHitDist = renderData.getTexture(kSpecHitDist);
    auto pTransparent = renderData.getTexture(kTranparent);

    auto pOut = renderData.getTexture(kOutput);

    if(!mpScene || !mEnabled)
    {
        pRenderContext->blit(pColorIn->getSRV(), pOut->getRTV());
        return;
    }

    if (!mpNGXWrapper)
        mpNGXWrapper.reset(new NGXWrapper(mpDevice, getRuntimeDirectory(), getRuntimeDirectory()));

    // initialize DLSSD
    if(!mpNGXWrapper->isDLSSDInitialized() || mRecreate)
    {
        mpNGXWrapper->releaseDLSSD();
        mpNGXWrapper->initializeDLSSD(
            pRenderContext,
            uint2(pColorIn->getWidth(), pColorIn->getHeight()),
            uint2(pOut->getWidth(), pOut->getHeight()),
            true, false, false
        );
        mRecreate = false;
    }

    const auto& camera = mpScene->getCamera();
    float2 jitterOffset = float2(camera->getJitterX(), -camera->getJitterY()) * float2(pColorIn->getWidth(), pColorIn->getHeight());
    //float2 motionVectorScale = float2(1.f, 1.f);
    float2 motionVectorScale = float2(pColorIn->getWidth(), pColorIn->getHeight());

    mpNGXWrapper->evaluateDLSSD(
        pRenderContext,
        pDiffAlbedo.get(), pSpecAlbedo.get(), pNormal.get(), pRoughness.get(),
        pColorIn.get(), pMotion.get(), pSpecMotion.get(), pDepth.get(), pSpecHitDist.get(),
        pTransparent.get(), pOut.get(), mReset, jitterOffset, motionVectorScale,
        camera->getViewMatrix(), camera->getProjMatrix()
    );
    mReset = false;
}

void RayReconstructionPass::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    if (!mEnabled) return;

    if(widget.button("Reset"))
    {
        mReset = true;
    }
}
