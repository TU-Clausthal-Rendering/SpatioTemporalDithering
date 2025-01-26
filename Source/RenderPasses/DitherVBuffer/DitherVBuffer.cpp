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
#include "DitherVBuffer.h"
#include "DitherLookup.h"

namespace
{
    const std::string kVbuffer = "vbuffer";
    const std::string kMotion = "mvec";
    const std::string kDepth = "depth";

    const uint32_t kMaxPayloadSizeBytes = 4; 
    const std::string kProgramRaytraceFile = "RenderPasses/DitherVBuffer/DitherVBuffer.rt.slang";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DitherVBuffer>();
}

DitherVBuffer::DitherVBuffer(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);
    mpSamplePattern = HaltonSamplePattern::create(8);
    createStratifiedBuffers();
}

Properties DitherVBuffer::getProperties() const
{
    return {};
}

RenderPassReflection DitherVBuffer::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addOutput(kVbuffer, "V-buffer").format(HitInfo::kDefaultFormat);
    reflector.addOutput(kMotion, "Motion vector").format(ResourceFormat::RG32Float);
    reflector.addOutput(kDepth, "Normalized Depth (1=far, 0=origin)").format(ResourceFormat::R32Float);
    return reflector;
}

void DitherVBuffer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mpScene) return;
    assert(mpProgram);
    assert(mpVars);

    auto pVbuffer = renderData.getTexture(kVbuffer);
    auto pMotion = renderData.getTexture(kMotion);
    auto pDepth = renderData.getTexture(kDepth);

    uint2 frameDim = uint2(pVbuffer->getWidth(), pVbuffer->getHeight());
    mpScene->getCamera()->setPatternGenerator(mpSamplePattern, 1.0f / float2(frameDim));

    auto var = mpVars->getRootVar();
    var["gVBuffer"] = pVbuffer;
    var["gMotion"] = pMotion;
    var["gDepth"] = pDepth;
    var["gStratifiedIndices"] = mpStratifiedIndices;
    var["gStratifiedLookUpTable"] = mpStratifiedLookUpBuffer;

    var["PerFrame"]["gFrameCount"] = mFrameCount++;
    var["PerFrame"]["gSampleCount"] = mpSamplePattern->getSampleCount();
    var["PerFrame"]["gSampleIndex"] = mpSamplePattern->getCurSample();

    mpProgram->addDefine("DITHER_MODE", std::to_string(uint32_t(mDitherMode)));

    uint3 dispatch = uint3(1);
    dispatch.x = pVbuffer->getWidth();
    dispatch.y = pVbuffer->getHeight();
    mpScene->raytrace(pRenderContext, mpProgram.get(), mpVars, dispatch);
}

void DitherVBuffer::renderUI(Gui::Widgets& widget)
{
    auto sampleCount = mpSamplePattern->getSampleCount();
    if(widget.var("Sample Count", sampleCount, 1u, 16u)) // sizes > 16 generate too much possible combinations for the dither pattern (per jitter)
    {
        mpSamplePattern->setSampleCount(sampleCount);
        createStratifiedBuffers();
    }
    widget.dropdown("Dither", mDitherMode);
}

void DitherVBuffer::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    setupProgram();
}

void DitherVBuffer::setupProgram()
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

    mpProgram = RtProgram::create(mpDevice, desc, defines);
    mpVars = RtProgramVars::create(mpDevice, mpProgram, sbt);

    // Bind static resources.
    ShaderVar var = mpVars->getRootVar();
    mpSampleGenerator->setShaderData(var);
}

void DitherVBuffer::createStratifiedBuffers()
{
    std::vector<int> indices;
    std::vector<uint32_t> lookUpTable;
    generateStratifiedLookupTable(mpSamplePattern->getSampleCount(), indices, lookUpTable);

    mpStratifiedIndices = Buffer::createStructured(mpDevice, sizeof(indices[0]), uint32_t(indices.size()), ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, indices.data(), false);
    mpStratifiedLookUpBuffer = Buffer::createStructured(mpDevice, sizeof(lookUpTable[0]), uint32_t(lookUpTable.size()), ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, lookUpTable.data(), false);
}
