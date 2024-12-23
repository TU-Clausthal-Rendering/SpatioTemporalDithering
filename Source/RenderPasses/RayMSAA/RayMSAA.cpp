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
#include "RayMSAA.h"

namespace
{
    const std::string kDepth = "linearZ";
    const std::string kNormal = "normals";
    const std::string kColorIn = "color";
    const std::string kColorOut = "colorOut";

    const std::string kEdgeMask = "edgeMask";
    const std::string kDepthSamples = "depthSamples";

    const std::string kEdgeShader = "RenderPasses/RayMSAA/Edges.cs.slang";
    const std::string kRayShader = "RenderPasses/RayMSAA/Ray.rt.slang";
    const std::string kColorShader = "RenderPasses/RayMSAA/ColorResolve.cs.slang";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, RayMSAA>();
}

RayMSAA::RayMSAA(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    // create compute shader here
    mpEdgeMaskPass = ComputePass::create(pDevice, kEdgeShader);
    mpColorPass = ComputePass::create(pDevice, kColorShader);

    mpPointSampler = Sampler::create(pDevice, Sampler::Desc()
        .setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point)
        .setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp));

    mpEdgeMaskPass->getRootVar()["gSampler"] = mpPointSampler;
    mpColorPass->getRootVar()["gSampler"] = mpPointSampler;
}

Properties RayMSAA::getProperties() const
{
    return {};
}

RenderPassReflection RayMSAA::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addInput(kDepth, "linear depths");
    reflector.addInput(kNormal, "surface normals");
    reflector.addInput(kColorIn, "aliased color");
    reflector.addOutput(kColorOut, "anti-aliased color").format(ResourceFormat::RGBA16Float).bindFlags(ResourceBindFlags::AllColorViews);

    // internal
    reflector.addOutput(kEdgeMask, "detected edges").format(ResourceFormat::R8Uint).bindFlags(ResourceBindFlags::AllColorViews).texture2D(0,0);
    reflector.addOutput(kDepthSamples, "traced depth samples").texture2D(0,0,1,1,mSamples).format(ResourceFormat::R32Float).bindFlags(ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);; // TODO smaller format (R8 as linear interpolation between min and max)
    return reflector;
}

void RayMSAA::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pDepth = renderData[kDepth]->asTexture();
    auto pNormal = renderData[kNormal]->asTexture();
    auto pColorIn = renderData[kColorIn]->asTexture();
    auto pColorOut = renderData[kColorOut]->asTexture();
    auto pEdgeMask = renderData[kEdgeMask]->asTexture();
    auto pDepthSamples = renderData[kDepthSamples]->asTexture();

    if(!mEnabled || !mpScene)
    {
        pRenderContext->blit(pColorIn->getSRV(), pColorOut->getRTV());
        return;
    }

    auto width = pColorIn->getWidth();
    auto height = pColorIn->getHeight();
    auto uvstep = float2(1.f / width, 1.f / height);
    auto resolution = float2(width, height);

    if(!mpRayProgram)
    {
        auto defines = mpScene->getSceneDefines();
        auto rayConeSpread = mpScene->getCamera()->computeScreenSpacePixelSpreadAngle(renderData.getDefaultTextureDims().y);
        defines.add("RAY_CONE_SPREAD", std::to_string(rayConeSpread));
        RtProgram::Desc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kRayShader);
        desc.setMaxPayloadSize(sizeof(float));
        desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
        desc.setMaxTraceRecursionDepth(1);
        desc.addTypeConformances(mpScene->getTypeConformances());
        desc.setShaderModel("6_5");

        auto sbt = RtBindingTable::create(1, 1, mpScene->getGeometryCount());
        sbt->setRayGen(desc.addRayGen("rayGen"));
        sbt->setMiss(0, desc.addMiss("miss"));
        sbt->setHitGroup(0, mpScene->getGeometryIDs(GeometryType::TriangleMesh), desc.addHitGroup("closestHit", "anyHit"));
        
        mpRayProgram = RtProgram::create(mpDevice, desc, defines);
        mpRayVars = RtProgramVars::create(mpDevice, mpRayProgram, sbt);
        mpRayVars->getRootVar()["gSampler"] = mpPointSampler;
    }

    {
        FALCOR_PROFILE(pRenderContext, "Edges");
        auto var = mpEdgeMaskPass->getRootVar();
        var["gDepthTex"] = pDepth;
        var["gEdgeTex"] = pEdgeMask;
        var["CB"]["gStep"] = uvstep;
        var["CB"]["gResolution"] = resolution;

        mpEdgeMaskPass->execute(pRenderContext, width, height, 1);
    }

    {
        FALCOR_PROFILE(pRenderContext, "RayTracing");
        auto var = mpRayVars->getRootVar();
        var["gDepthTex"] = pDepth;
        var["gEdgeTex"] = pEdgeMask;
        var["gDepthSamples"] = pDepthSamples;

        mpScene->raytrace(pRenderContext, mpRayProgram.get(), mpRayVars, uint3(width, height, mSamples));
    }

    {
        FALCOR_PROFILE(pRenderContext, "ColorResolve");
        mpColorPass->getProgram()->addDefine("MSAA_SAMPLES", std::to_string(mSamples));
        auto var = mpColorPass->getRootVar();
        var["gColorIn"] = pColorIn;
        var["gColorOut"] = pColorOut;

        var["gEdgeTex"] = pEdgeMask;
        var["gDepthSamples"] = pDepthSamples;
        var["gDepthTex"] = pDepth;
        var["CB"]["gStep"] = uvstep;
        var["CB"]["gResolution"] = resolution;

        mpColorPass->execute(pRenderContext, width, height, 1);
    }
}

void RayMSAA::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    if (!mEnabled) return;

    if (widget.var("Samples", mSamples, 1, 16))
        requestRecompile();

}

void RayMSAA::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    // recompile ray shaders
    mpRayProgram.reset();
}
