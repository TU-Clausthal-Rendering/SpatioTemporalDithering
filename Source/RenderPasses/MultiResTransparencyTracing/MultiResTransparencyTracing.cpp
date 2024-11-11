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
#include "MultiResTransparencyTracing.h"

namespace
{
    const std::string kOutput = "color";
    const std::string kInternal = "colorInternal";
    const std::string kRayT = "rayT";

    const uint32_t kMaxPayloadSizeBytes = 20; // 16 byte hit info + 4 byte distance
    const std::string kProgramRaytraceFile = "RenderPasses/MultiResTransparencyTracing/MultiResTransparencyTracing.rt.slang";
    const std::string kProgramPullFile = "RenderPasses/MultiResTransparencyTracing/Pull.cs.slang";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, MultiResTransparencyTracing>();
}

MultiResTransparencyTracing::MultiResTransparencyTracing(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);
    mThresholds = { 0.5f, 0.01f };

    Sampler::Desc desc;
    desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    mpLinearSampler = Sampler::create(mpDevice, desc);

    DefineList defines;
    mpPullProgram = ComputePass::create(mpDevice, kProgramPullFile, "main", defines);
    auto var = mpPullProgram->getRootVar();
    var["gSampler"] = mpLinearSampler;
}

Properties MultiResTransparencyTracing::getProperties() const
{
    return {};
}

RenderPassReflection MultiResTransparencyTracing::reflect(const CompileData& compileData)
{
    auto numMips = mThresholds.size();
    // Define the required resources here
    RenderPassReflection reflector;
    auto colorFormat = ResourceFormat::RGBA16Float;
    reflector.addOutput(kOutput, "Output image").format(colorFormat);
    reflector.addOutput(kInternal, "Internal image").format(colorFormat).texture2D(0,0, 1, numMips);
    reflector.addInternal(kRayT, "Internal ray t").format(ResourceFormat::R32Float).texture2D(0, 0, 1, numMips);
    return reflector;
}

void MultiResTransparencyTracing::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mpScene) return;
    assert(mpProgram);
    assert(mpVars);

    auto pColorOut = renderData[kOutput]->asTexture();
    auto pColorTmp = renderData[kInternal]->asTexture();
    auto pRayT = renderData[kRayT]->asTexture();

    auto var = mpVars->getRootVar();
    var["gSampler"] = mpLinearSampler;
    var["PerFrame"]["gFrameCount"] = mFrameCount++;
    var["PerFrame"]["gAmbientIntensity"] = mAmbientIntensity;
    var["PerFrame"]["gLightIntensity"] = mLightIntensity;
    var["PerFrame"]["gEnvMapIntensity"] = mEnvMapIntensity;
    var["PerFrame"]["gShadowRay"] = mShadowRay;

    uint3 dispatch = uint3(1);
    dispatch.x = pColorTmp->getWidth();
    dispatch.y = pColorTmp->getHeight();
    var["PerFrame"]["gFullResolution"] = uint2(dispatch.x, dispatch.y);
    uint resMultiplier = 1;
    for(uint level = 0; level < mThresholds.size(); ++level)
    {
        FALCOR_PROFILE(pRenderContext, "Level" + std::to_string(level));

        // write
        var["gColor"].setUav(pColorTmp->getUAV(level));
        var["gRayT"].setUav(pRayT->getUAV(level));

        // read
        ref<Texture> emptyTex;
        var["gColorPrev"] = emptyTex;
        var["gRayTPrev"] = emptyTex;
        if (level > 0)
        {
            var["gColorPrev"].setSrv(pColorTmp->getSRV(level - 1));
            var["gRayTPrev"].setSrv(pRayT->getSRV(level - 1));
            dispatch.x = (dispatch.x + 1) / 2;
            dispatch.y = (dispatch.y + 1) / 2;
            resMultiplier *= 2;
        }

        var["PerFrame"]["gResMultiplier"] = resMultiplier;
        var["PerFrame"]["gVisibilityThreshold"] = mDoMultiRes ? mThresholds[level] : 0.01f;

        mpScene->raytrace(pRenderContext, mpProgram.get(), mpVars, dispatch);

        if (!mDoMultiRes) break;
    }
    
    if(mDoMultiRes)
    {
        auto cvar = mpPullProgram->getRootVar();
        for (uint level = mThresholds.size() - 1u; level > 0; --level)
        {
            FALCOR_PROFILE(pRenderContext, "Pull" + std::to_string(level));

            cvar["gColor"].setUav(pColorTmp->getUAV(level - 1));
            cvar["gRayT"].setUav(pRayT->getUAV(level - 1));
            cvar["gColorPrev"].setSrv(pColorTmp->getSRV(level));
            cvar["gRayTPrev"].setSrv(pRayT->getSRV(level));

            mpPullProgram->execute(pRenderContext, pColorTmp->getWidth(level - 1), pColorTmp->getHeight(level - 1) , 1);
        }
    }

    // copy final result to output
    pRenderContext->blit(pColorTmp->getSRV(0, 1), pColorOut->getRTV());
}

void MultiResTransparencyTracing::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enable Multi-Res", mDoMultiRes);

    widget.var("Ambient Intensity", mAmbientIntensity, 0.f, 100.f, 0.1f);
    widget.var("Env Map Intensity", mEnvMapIntensity, 0.f, 100.f, 0.1f);
    widget.var("Scene Light Intensity", mLightIntensity, 0.f, 100.f, 0.1f);

    
    widget.checkbox("Ray Shadows", reinterpret_cast<bool&>(mShadowRay));
    
}

void MultiResTransparencyTracing::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    setupProgram();
}

void MultiResTransparencyTracing::setupProgram()
{
    if (!mpScene) return;

    DefineList defines;
    defines.add(mpScene->getSceneDefines());
    defines.add(mpSampleGenerator->getDefines());

    RtProgram::Desc desc;
    desc.addShaderModules(mpScene->getShaderModules());
    desc.addShaderLibrary(kProgramRaytraceFile);
    desc.addTypeConformances(mpScene->getTypeConformances());
    desc.setMaxPayloadSize(kMaxPayloadSizeBytes);
    desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
    desc.setMaxTraceRecursionDepth(1);

    ref<RtBindingTable> sbt = RtBindingTable::create(1, 1, mpScene->getGeometryCount());
    sbt->setRayGen(desc.addRayGen("rayGen"));
    sbt->setMiss(0, desc.addMiss("miss"));
    sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("closestHit", "anyHit"));

    // Add hit group with intersection shader for triangle meshes with displacement maps.
    /*if (mpScene->hasGeometryType(Scene::GeometryType::DisplacedTriangleMesh))
    {
        sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::DisplacedTriangleMesh), desc.addHitGroup("displacedTriangleMeshClosestHit", "", "displacedTriangleMeshIntersection"));
    }

    // Add hit group with intersection shader for curves (represented as linear swept spheres).
    if (mpScene->hasGeometryType(Scene::GeometryType::Curve))
    {
        sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::Curve), desc.addHitGroup("curveClosestHit", "", "curveIntersection"));
    }

    // Add hit group with intersection shader for SDF grids.
    if (mpScene->hasGeometryType(Scene::GeometryType::SDFGrid))
    {
        sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::SDFGrid), desc.addHitGroup("sdfGridClosestHit", "", "sdfGridIntersection"));
    }*/

    mpProgram = RtProgram::create(mpDevice, desc, defines);
    mpVars = RtProgramVars::create(mpDevice, mpProgram, sbt);

    // Bind static resources.
    ShaderVar var = mpVars->getRootVar();
    mpSampleGenerator->setShaderData(var);
}
