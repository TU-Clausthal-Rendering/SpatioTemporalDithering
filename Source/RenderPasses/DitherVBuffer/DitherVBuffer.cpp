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
    const std::string kOpacity = "opacity";

    const uint32_t kMaxPayloadSizeBytes = 4; 
    const std::string kProgramRaytraceFile = "RenderPasses/DitherVBuffer/DitherVBuffer.rt.slang";

    const std::string kUseWhitelist = "useWhitelist";
    const std::string kWhitelist = "whitelist";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DitherVBuffer>();
}

DitherVBuffer::DitherVBuffer(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_UNIFORM);
    mpSamplePattern = HaltonSamplePattern::create(16);
    createStratifiedBuffers();
    // load properties
    for (const auto& [key, value] : props)
    {
        if (key == kUseWhitelist) mUseTransparencyWhitelist = value;
        else if (key == kWhitelist)
        {
            std::stringstream ss;
            std::string svalue = value;
            ss << svalue;
            std::string entry;
            while (std::getline(ss, entry, ','))
            {
                mTransparencyWhitelist.insert(entry);
            }
        }
    }
}

Properties DitherVBuffer::getProperties() const
{
    Properties props;
    props[kUseWhitelist] = mUseTransparencyWhitelist;
    // convert whitelist into a comma separated string
    std::stringstream ss;
    for(const auto& entry : mTransparencyWhitelist) ss << entry << ",";
    props[kWhitelist] = ss.str();
    return props;
}

RenderPassReflection DitherVBuffer::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addOutput(kVbuffer, "V-buffer").format(HitInfo::kDefaultFormat);
    reflector.addOutput(kMotion, "Motion vector").format(ResourceFormat::RG32Float).flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addOutput(kOpacity, "Opacity Mask (1 = any transparent)").format(ResourceFormat::R8Unorm).flags(RenderPassReflection::Field::Flags::Optional);
    return reflector;
}

void DitherVBuffer::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!mpScene) return;
    assert(mpProgram);
    assert(mpVars);

    auto pVbuffer = renderData.getTexture(kVbuffer);
    auto pMotion = renderData.getTexture(kMotion);
    auto pOpacity = renderData.getTexture(kOpacity);

    uint2 frameDim = uint2(pVbuffer->getWidth(), pVbuffer->getHeight());
    mpScene->getCamera()->setPatternGenerator(mpSamplePattern, 1.0f / float2(frameDim));

    auto var = mpVars->getRootVar();
    var["gVBuffer"] = pVbuffer;
    var["gMotion"] = pMotion;
    var["gOpacity"] = pOpacity;
    var["gStratifiedIndices"] = mpStratifiedIndices;
    var["gStratifiedLookUpTable"] = mpStratifiedLookUpBuffer;
    assert(mpTransparencyWhitelist);
    var["gTransparencyWhitelist"] = mpTransparencyWhitelist;

    var["PerFrame"]["gFrameCount"] = mFrameCount++;
    var["PerFrame"]["gSampleCount"] = mpSamplePattern->getSampleCount();
    var["PerFrame"]["gSampleIndex"] = mpSamplePattern->getCurSample();
    var["PerFrame"]["gDLSSCorrectionStrength"] = mDLSSCorrectionStrength;

    mpProgram->addDefine("COVERAGE_CORRECTION", std::to_string(uint32_t(mCoverageCorrection)));
    mpProgram->addDefine("TRANSPARENCY_WHITELIST", mUseTransparencyWhitelist ? "1" : "0");
    mpProgram->addDefine("DITHER_MODE", std::to_string(uint32_t(mDitherMode)));
    mpProgram->addDefine("ALPHA_TEXTURE_LOD", mUseAlphaTextureLOD ? "1" : "0");

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
    widget.checkbox("Alpha Texture LOD", mUseAlphaTextureLOD);

    widget.dropdown("Coverage Correction", mCoverageCorrection);
    if (mCoverageCorrection == CoverageCorrection::DLSS)
    {
        widget.slider("DLSS Correction Strength", mDLSSCorrectionStrength, 0.0f, 1.0f);
    }

    widget.checkbox("Transparency Whitelist", mUseTransparencyWhitelist);
    if(mUseTransparencyWhitelist && mpScene)
    {
        auto g = widget.group("Whitelist");
        std::string removeEntry;
        // list all material names of the current scene
        for (uint mat = 0; mat < mpScene->getMaterialCount(); ++mat)
        {
            std::string name = mpScene->getMaterial(MaterialID(mat))->getName();
            bool isTransparent = mTransparencyWhitelist.find(name) != mTransparencyWhitelist.end();
            if (g.checkbox(name.c_str(), isTransparent))
            {
                if (isTransparent) mTransparencyWhitelist.insert(name);
                else mTransparencyWhitelist.erase(name);
                updateWhitelistBuffer();
            }
        }
    }
}

void DitherVBuffer::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    setupProgram();
    if(!updateWhitelistBuffer())
    {
        mUseTransparencyWhitelist = false; // disable whitelist since nothing is whitelisted (might confuse people when loading in custom scenes)
    }
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

bool DitherVBuffer::updateWhitelistBuffer()
{
    if (!mpScene) return true;
    bool any = false;

    // Calculate the number of uint32_t elements needed to store all bits
    uint32_t materialCount = mpScene->getMaterialCount();
    uint32_t uintCount = (materialCount + 31) / 32; // Round up to the nearest uint32_t

    std::vector<uint32_t> whitelist(uintCount, 0); // Initialize all bits to 0

    // Pack the boolean values into bits
    for (uint32_t mat = 0; mat < materialCount; ++mat)
    {
        std::string name = mpScene->getMaterial(MaterialID(mat))->getName();
        bool isTransparent = mTransparencyWhitelist.find(name) != mTransparencyWhitelist.end();
        any |= isTransparent;

        if (isTransparent)
        {
            // Set the corresponding bit in the whitelist
            uint32_t uintIndex = mat / 32;
            uint32_t bitIndex = mat % 32;
            whitelist[uintIndex] |= (1 << bitIndex);
        }
    }

    mpTransparencyWhitelist = Buffer::createStructured(mpDevice, sizeof(uint32_t), uintCount, ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, whitelist.data(), false);

    return any;
}
