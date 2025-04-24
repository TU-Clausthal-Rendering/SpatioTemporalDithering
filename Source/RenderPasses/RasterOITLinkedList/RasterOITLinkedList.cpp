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
#include "RasterOITLinkedList.h"
#include "Scene/Lighting/LightSettings.h"
#include "Scene/Lighting/ShadowSettings.h"

namespace
{
    const std::string kDepth = "depth"; // input depth buffer
    const std::string kColor = "color";

    const std::string kHead = "head";

    const std::string kWhitelist = "whitelist";

    const std::string kProgramFile = "RenderPasses/RasterOITLinkedList/BuildList.3D.slang";
    const std::string kSortFile = "RenderPasses/RasterOITLinkedList/SortList.slang";
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, RasterOITLinkedList>();
}

RasterOITLinkedList::RasterOITLinkedList(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    // default graphics state
    mpState = GraphicsState::create(mpDevice);
    DepthStencilState::Desc dsDesc;
    dsDesc.setDepthWriteMask(false); // disable depth writes
    auto dsState = DepthStencilState::create(dsDesc);
    mpState->setDepthStencilState(dsState);
    mpFbo = Fbo::create(mpDevice);

    mpCountBuffer = Buffer::createStructured(mpDevice, sizeof(uint), 1, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None, nullptr, false);

    // sort pass
    Program::Desc desc;
    desc.addShaderLibrary(kSortFile).csEntry("main").setShaderModel("6_5");

    mpSortPass = ComputePass::create(mpDevice, desc);
}

Properties RasterOITLinkedList::getProperties() const
{
    return {};
}

RenderPassReflection RasterOITLinkedList::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    reflector.addInput(kDepth, "Depth buffer");

    reflector.addInternal(kHead, "head pointer").format(ResourceFormat::R32Uint).bindFlags(ResourceBindFlags::UnorderedAccess);

    reflector.addOutput(kColor, "Color buffer").format(ResourceFormat::RGBA16Float).bindFlags(ResourceBindFlags::AllColorViews);
    return reflector;
}

void RasterOITLinkedList::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto pDepth = renderData.getTexture(kDepth);
    auto pColor = renderData.getTexture(kColor);
    auto pHead = renderData.getTexture(kHead);

    // clear resources
    uint32_t uintmax = uint32_t(-1);
    pRenderContext->clearTexture(pColor.get(), float4(0, 0, 0, 1));
    pRenderContext->clearUAV(pHead->getUAV().get(), uint4(uintmax));
    pRenderContext->clearUAV(mpCountBuffer->getUAV(0, 1).get(), uint4(0));

    pRenderContext->uavBarrier(mpCountBuffer.get());
    pRenderContext->uavBarrier(pHead.get());

    if (!mpScene)
    {
        return;
    }

    assert(mpProgram);
    assert(mpVars);

    if (!mpDataBuffer || mpDataBuffer->getElementCount() != mDataBufferSize)
    {
        mpDataBuffer = Buffer::createStructured(mpDevice, sizeof(uint4), mDataBufferSize, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource, Buffer::CpuAccess::None);
    }

    {
        FALCOR_PROFILE(pRenderContext, "Build List");

        auto vars = mpVars->getRootVar();
        vars["gHead"] = pHead;
        vars["gBuffer"] = mpDataBuffer;
        vars["gCount"] = mpCountBuffer;

        vars["PerFrame"]["gFrameDim"] = uint2(pDepth->getWidth(), pDepth->getHeight());
        vars["PerFrame"]["maxElements"] = mpDataBuffer->getElementCount();

        // lighting settings
        LightSettings::get().updateShaderVar(vars);
        ShadowSettings::get().updateShaderVar(mpDevice, vars);
        mpProgram->addDefines(ShadowSettings::get().getShaderDefines(*mpScene, renderData.getDefaultTextureDims()));

        // framebuffer
        mpFbo->attachDepthStencilTarget(pDepth);
        mpState->setFbo(mpFbo);

        std::set<std::string> whitelist;
        const bool useWhitelist = renderData.getDictionary().keyExists(kWhitelist);
        if (useWhitelist)
            whitelist = renderData.getDictionary().getValue<decltype(whitelist)>(kWhitelist);

        mpState->setProgram(mpProgram);
        auto camera = mpScene->getCamera();
        if(!mpCulling)
        {
            mpCulling = make_ref<FrustumCulling>(camera);
        }

        mpCulling->setUserCallback([&](const MeshDesc& mesh)
        {
            if (!useWhitelist) return true; // draw all transparent objects
            auto mat = mpScene->getMaterial(MaterialID::fromSlang(mesh.materialID));
            auto name = mat->getName();
            return whitelist.find(name) != whitelist.end();
        });

        auto cameraChanges = camera->getChanges();
        auto excluded = Camera::Changes::Jitter | Camera::Changes::History;
        if (((cameraChanges & ~excluded) != Camera::Changes::None))
        {
            mpCulling->updateFrustum(camera);
        }

        mpScene->rasterizeFrustumCulling(pRenderContext, mpState.get(), mpVars.get(), RasterizerState::CullMode::None, RasterizerState::MeshRenderMode::SkipOpaque, true, mpCulling);
    }
    //return;
    {
        FALCOR_PROFILE(pRenderContext, "Sort and Blend");
        auto vars = mpSortPass->getRootVar();

        vars["gHead"] = pHead;
        vars["gBuffer"] = mpDataBuffer;
        vars["gColor"] = pColor;

        vars["PerFrame"]["gFrameDim"] = uint2(pDepth->getWidth(), pDepth->getHeight());
        vars["PerFrame"]["maxElements"] = mpDataBuffer->getElementCount();

        mpSortPass->execute(pRenderContext, pDepth->getWidth(), pDepth->getHeight());
    }
}

void RasterOITLinkedList::renderUI(Gui::Widgets& widget)
{
    widget.var("Linked List Node Count", mDataBufferSize, 1024u, 1024u * 1024u * 1024u);
    auto sizeInBytes = size_t(mDataBufferSize) * size_t(16);
    widget.text("Size in MB: " + std::to_string(sizeInBytes / (1024u * 1024u)));
}

void RasterOITLinkedList::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    setupProgram();
    mpCulling = nullptr;
}

void RasterOITLinkedList::setupProgram()
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
