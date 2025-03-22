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
#include "DitherVBufferRaster.h"
#include "../DitherVBuffer/PermutationLookup.h"
#include "Scene/Lighting/LightSettings.h"
#include "Scene/Lighting/ShadowSettings.h"

namespace
{
    const std::string kVbuffer = "vbuffer";
    const std::string kDepth = "depth";

    const std::string kProgramFile = "RenderPasses/DitherVBufferRaster/DitherVBuffer.3D.slang";

    const std::string kUseWhitelist = "useWhitelist";
    const std::string kWhitelist = "whitelist";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DitherVBufferRaster>();
}

DitherVBufferRaster::DitherVBufferRaster(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    // default graphics state
    mpState = GraphicsState::create(mpDevice);
    mpFbo = Fbo::create(mpDevice);

    mpSamplePattern = HaltonSamplePattern::create(16);
    createNoisePattern();
    setFractalDitherPattern(mFractalDitherPattern);
    Sampler::Desc sd;
    sd.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    sd.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Clamp);
    mpFracSampler = Sampler::create(mpDevice, sd);
    sd.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
    sd.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap);
    mpNoiseSampler = Sampler::create(mpDevice, sd);

    //generatePermutations<3>();
    mpPermutations3x3Buffer = generatePermutations3x3(mpDevice);

    mpBlueNoise3DTex = Texture::createFromFile(mpDevice, "dither/bluenoise3d_16.dds", false, false);
    mpBlueNoise64Tex = Texture::createFromFile(mpDevice, "dither/bluenoise64.dds", false, false);
    mpBayer64Tex = Texture::createFromFile(mpDevice, "dither/bayer64.dds", false, false);

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

Properties DitherVBufferRaster::getProperties() const
{
    Properties props;
    props[kUseWhitelist] = mUseTransparencyWhitelist;
    // convert whitelist into a comma separated string
    std::stringstream ss;
    for (const auto& entry : mTransparencyWhitelist) ss << entry << ",";
    props[kWhitelist] = ss.str();
    return props;
}

RenderPassReflection DitherVBufferRaster::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    reflector.addOutput(kVbuffer, "V-buffer").format(HitInfo::kDefaultFormat);
    reflector.addOutput(kDepth, "Depth Bufer").format(ResourceFormat::D32Float);
    return reflector;
}

void DitherVBufferRaster::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pVbuffer = renderData.getTexture(kVbuffer);
    auto pDepth = renderData.getTexture(kDepth);

    if (!mpScene)
    {
        pRenderContext->clearUAV(pVbuffer->getUAV().get(), uint4(0));
        return;
    }

    assert(mpProgram);
    assert(mpVars);

    uint2 frameDim = uint2(pVbuffer->getWidth(), pVbuffer->getHeight());
    mpScene->getCamera()->setPatternGenerator(mpSamplePattern, 1.0f / float2(frameDim));

    auto var = mpVars->getRootVar();
    var["gDitherTex"] = mpFracDitherTex;
    var["gDitherRampTex"] = mpFracDitherRampTex;
    var["gDitherSampler"] = mpFracSampler;
    var["gNoiseTex"] = mpNoiseTex;
    var["gNoiseSampler"] = mpNoiseSampler;
    assert(mpTransparencyWhitelist);
    var["gTransparencyWhitelist"] = mpTransparencyWhitelist;
    var["gPermutations3x3"] = mpPermutations3x3Buffer;
    var["gBlueNoise3DTex"] = mpBlueNoise3DTex;
    var["gBlueNoise64x64Tex"] = mpBlueNoise64Tex;
    var["gBayerNoise64Tex"] = mpBayer64Tex;

    var["PerFrame"]["gFrameCount"] = mFrameCount++;
    var["PerFrame"]["gSampleCount"] = mpSamplePattern->getSampleCount();
    var["PerFrame"]["gSampleIndex"] = mFrameCount % std::max(1u, mpSamplePattern->getSampleCount());//mpSamplePattern->getCurSample();
    var["PerFrame"]["gDLSSCorrectionStrength"] = mDLSSCorrectionStrength;
    var["PerFrame"]["gAlignMotionVectors"] = mAlignMotionVectors ? 1 : 0;
    var["PerFrame"]["gFrameDim"] = frameDim;

    var["DitherConstants"]["gGridScale"] = mGridScale;
    var["DitherConstants"]["gNoiseScale"] = float2(1.0f / mpNoiseTex->getWidth(), 1.0f / mpNoiseTex->getHeight());
    var["DitherConstants"]["gRotatePattern"] = mRotatePattern ? 1 : 0;
    var["DitherConstants"]["gObjectHashType"] = uint(mObjectHashType);
    var["DitherConstants"]["gNoiseTop"] = uint(mNoiseTopPattern);
    var["DitherConstants"]["gDitherTAAPermutations"] = mDitherTAAPermutations ? 1 : 0;

    mpProgram->addDefine("COVERAGE_CORRECTION", std::to_string(uint32_t(mCoverageCorrection)));
    mpProgram->addDefine("TRANSPARENCY_WHITELIST", mUseTransparencyWhitelist ? "1" : "0");
    mpProgram->addDefine("DITHER_MODE", std::to_string(uint32_t(mDitherMode)));
    mpProgram->addDefine("ALPHA_TEXTURE_LOD", mUseAlphaTextureLOD ? "1" : "0");
    mpProgram->addDefine("CULL_BACK_FACES", mCullBackFaces ? "1" : "0");

    // clear buffers
    pRenderContext->clearDsv(pDepth->getDSV().get(), 1.0f, 0);
    pRenderContext->clearUAV(pVbuffer->getUAV().get(), uint4(0));

    // framebuffer
    mpFbo->attachDepthStencilTarget(pDepth);
    mpFbo->attachColorTarget(pVbuffer, 0);
    mpState->setFbo(mpFbo);

    auto cullMode = mCullBackFaces ? RasterizerState::CullMode::Back : RasterizerState::CullMode::None;
    mpScene->rasterize(pRenderContext, mpState.get(), mpVars.get(), cullMode, RasterizerState::MeshRenderMode::All);
}

void DitherVBufferRaster::renderUI(Gui::Widgets& widget)
{
    //widget.slider("Dither Threshold", mMinVisibility, 0.0f, 1.0f);

    widget.dropdown("Dither", mDitherMode);
    if (mDitherMode == DitherMode::FractalDithering)
    {
        if (widget.dropdown("Pattern", mFractalDitherPattern))
        {
            setFractalDitherPattern(mFractalDitherPattern);
        }
    }

    const bool is2DDither = mDitherMode == DitherMode::PerPixel2x2 ||
        mDitherMode == DitherMode::PerPixel3x3 ||
        mDitherMode == DitherMode::PerPixel4x4;
    const bool is3DDither = mDitherMode == DitherMode::PerPixel2x2x2;
    const bool isDitherTAA = mDitherMode == DitherMode::DitherTemporalAA;

    if (is2DDither || is3DDither || isDitherTAA)
    {
        widget.checkbox("Align Motion Vector", mAlignMotionVectors);
        widget.tooltip("Align motion vector to grid size to prevent issues when moving camera");
    }

    bool useTopNoiseGrid = false;
    if (is2DDither || is3DDither || isDitherTAA)
    {
        widget.dropdown("Noise on Top", mNoiseTopPattern);
        if (mNoiseTopPattern == NoiseTopPattern::SurfaceWhite)
            useTopNoiseGrid = true;
    }

    if (is2DDither || mDitherMode == DitherMode::HashGrid)
    {
        widget.checkbox("Rotate Pattern", mRotatePattern);
        widget.tooltip("Rotates the per-pixel dither pattern based on the frame index");
    }

    if (mDitherMode == DitherMode::FractalDithering || mDitherMode == DitherMode::HashGrid || useTopNoiseGrid)
    {
        widget.var("Grid Scale", mGridScale, 0.0001f, 16.0f, 0.01f);
    }
    if (mDitherMode == DitherMode::HashGrid || useTopNoiseGrid)
    {
        if (widget.dropdown("Noise Pattern", mNoisePattern))
        {
            createNoisePattern();
        }
    }

    if (mDitherMode == DitherMode::DitherTemporalAA)
    {
        widget.checkbox("Mask Permutations", mDitherTAAPermutations);
        widget.tooltip("Uses a permutations of the [0,1,2,3,4] sequence for creating the 5x5 mask. This will prevent objects from masking each other.");
    }

    widget.dropdown("Coverage Correction", mCoverageCorrection);
    if (mCoverageCorrection == CoverageCorrection::DLSS)
    {
        widget.slider("DLSS Correction Strength", mDLSSCorrectionStrength, 0.0f, 4.0f);
    }


    if (auto g = widget.group("Scene"))
    {
        widget.dropdown("Object Hash", mObjectHashType);

        widget.checkbox("Alpha Texture LOD", mUseAlphaTextureLOD);

        widget.checkbox("Cull Back Faces", mCullBackFaces);

        widget.checkbox("Transparency Whitelist", mUseTransparencyWhitelist);
        widget.tooltip("Uses only whitelisted materials for dithering, when enabled. If not whitelisted, the material will use an alpha test.");
        if (mUseTransparencyWhitelist && mpScene)
        {
            auto g2 = widget.group("Whitelist");
            std::string removeEntry;
            // list all material names of the current scene
            for (uint mat = 0; mat < mpScene->getMaterialCount(); ++mat)
            {
                std::string name = mpScene->getMaterial(MaterialID(mat))->getName();
                bool isTransparent = mTransparencyWhitelist.find(name) != mTransparencyWhitelist.end();
                if (g2.checkbox(name.c_str(), isTransparent))
                {
                    if (isTransparent) mTransparencyWhitelist.insert(name);
                    else mTransparencyWhitelist.erase(name);
                    updateWhitelistBuffer();
                }
            }
        }
    }
}

void DitherVBufferRaster::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    setupProgram();
    mUseTransparencyWhitelist = updateWhitelistBuffer();
}

void DitherVBufferRaster::setFractalDitherPattern(DitherPattern pattern)
{
    mFractalDitherPattern = pattern;
    std::string texname;
    switch (pattern)
    {
    case DitherPattern::Dither2x2:
        texname = "dither/Dither3D_2x2";
        break;
    case DitherPattern::Dither4x4:
        texname = "dither/Dither3D_4x4";
        break;
    case DitherPattern::Dither8x8:
        texname = "dither/Dither3D_8x8";
        break;
    }
    mpFracDitherTex = Texture::createFromFile(mpDevice, texname + ".dds", false, false);
    mpFracDitherRampTex = Texture::createFromFile(mpDevice, texname + "_ramp.dds", false, false);

}

void DitherVBufferRaster::setupProgram()
{
    if (!mpScene) return;

    Program::Desc desc;
    desc.addShaderModules(mpScene->getShaderModules());
    desc.addShaderLibrary(kProgramFile).psEntry("psMain").vsEntry("vsMain");
    desc.addTypeConformances(mpScene->getTypeConformances());
    desc.setShaderModel("6_5");

    mpProgram = GraphicsProgram::create(mpDevice, desc, mpScene->getSceneDefines());
    mpState->setProgram(mpProgram);
    mpVars = GraphicsVars::create(mpDevice, mpProgram->getReflector());
}

bool DitherVBufferRaster::updateWhitelistBuffer()
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

void DitherVBufferRaster::createNoisePattern()
{
    std::string texname;
    switch (mNoisePattern)
    {
    case NoisePattern::White:
        texname = "dither/whitenoise1024.dds";
        break;
    case NoisePattern::Blue:
        texname = "dither/bluenoise1024.dds";
        break;
    case NoisePattern::Bayer:
        texname = "dither/bayer_matrix.dds";
        break;
    case NoisePattern::BlueBayer:
        texname = "dither/blue_bayer.dds";
        break;
    case NoisePattern::Poisson:
        texname = "dither/poisson1024.dds";
        break;
    case NoisePattern::Perlin:
        texname = "dither/perlin1024.dds";
        break;
    case NoisePattern::Blue64:
        texname = "dither/bluenoise64.dds";
        break;
    default:
        assert(false);
    }

    mpNoiseTex = Texture::createFromFile(mpDevice, texname, false, false);
}
