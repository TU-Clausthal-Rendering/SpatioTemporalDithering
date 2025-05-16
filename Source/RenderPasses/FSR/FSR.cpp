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
//#include "Core/API/Shared/D3D12Handles.h"
#include "FSR.h"
#include "Core/API/NativeHandleTraits.h"
#include <ffx_api/dx12/ffx_api_dx12.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/ffx_api_types.h>
#include "Utils/Math/FalcorMath.h"

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, FSR>();
}

namespace
{
    const std::string kColorIn = "color";
    const std::string kDepth = "depth";
    const std::string kMotion = "mvec";
    // const std::string kExposure = "exposure"; // Optional resource containing a 1x1 exposure value.
    const std::string kReactive = "reactive"; // Optional resource containing alpha value of reactive objects in the scene.
    const std::string kOutput = "output";
}

static void ffxApiMessageFunc(uint32_t type, const wchar_t* message)
{
    auto wstr = std::wstring(message);
    auto cstr = std::string(wstr.begin(), wstr.end());
    std::cerr << "FFX API message: " << cstr << std::endl;
}

FSR::FSR(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice), mContext(nullptr)
{

}

Properties FSR::getProperties() const
{
    return {};
}

RenderPassReflection FSR::reflect(const CompileData& compileData)
{
    // Define the required resources here
    RenderPassReflection reflector;
    reflector.addInput(kColorIn, "Color buffer for the current frame (at render resolution)");
    reflector.addInput(kDepth, "32bit depth values for the current frame (at render resolution)");
    reflector.addInput(kMotion, "2-dimensional motion vectors");
    reflector.addInput(kReactive, "Reactive Mask: For transparency, should be ~alpha. For best results, use values between 0.0 (no transparency) and 0.9 (transparent)").flags(RenderPassReflection::Field::Flags::Optional);

    reflector.addOutput(kOutput, "Output texture").format(ResourceFormat::RGBA32Float).bindFlags(Resource::BindFlags::AllColorViews);
    return reflector;
}

void FSR::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    auto pNative = mpDevice->getNativeHandle().as<ID3D12Device*>();

    ffx::CreateBackendDX12Desc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = pNative;

    ffx::CreateContextDescUpscale contextDesc{};
    contextDesc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE; // | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    contextDesc.maxRenderSize = { compileData.defaultTexDims.x, compileData.defaultTexDims.y };
    contextDesc.maxUpscaleSize = { compileData.defaultTexDims.x, compileData.defaultTexDims.y };
#if defined(_DEBUG)
    contextDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    contextDesc.fpMessage = ffxApiMessageFunc;
#endif  

    auto ret = ffx::CreateContext(mContext, nullptr, contextDesc, backendDesc);
    if(ret != ffx::ReturnCode::Ok)
        throw std::runtime_error("Failed to create FFX context");

    // Query version of fsr for display
    ffx::QueryDescGetVersions versionQuery{};
    versionQuery.createDescType = contextDesc.header.type;
    versionQuery.device = pNative; // only for DirectX 12 applications
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;
    // get number of versions for allocation
    ffxQuery(nullptr, &versionQuery.header);
    if (!versionCount) return; // failed?

    std::vector<const char*> versionNames;
    versionNames.resize(versionCount);
    versionQuery.versionNames = versionNames.data();
    ffxQuery(nullptr, &versionQuery.header);
    std::cerr << "FSR: Upscaler Context version " << versionNames[0] << std::endl;
}

static FfxApiResource ffxGetResourceApi(RenderContext* pRenderContext, Texture* pTexture)
{
    if(!pTexture)
    {
        return ffxApiGetResourceDX12(nullptr);
    }

    // transition state to shader resource
    pRenderContext->resourceBarrier(pTexture, Resource::State::ShaderResource);

    ID3D12Resource* pResource = pTexture->getNativeHandle().as<ID3D12Resource*>();
    FfxApiResource apiRes = ffxApiGetResourceDX12(pResource, FFX_API_RESOURCE_STATE_COMPUTE_READ, 0);
    return apiRes;
}

void FSR::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    mTimer.update();
    auto pColorIn = renderData.getTexture(kColorIn);
    auto pDepth = renderData.getTexture(kDepth);
    auto pMotion = renderData.getTexture(kMotion);
    auto pReactive = renderData.getTexture(kReactive);
    auto pOut = renderData.getTexture(kOutput);

    if (!mpScene || !mEnabled)
    {
        pRenderContext->blit(pColorIn->getSRV(), pOut->getRTV());
        return;
    }

    if(!pReactive) mUseReactive = false;
    if(!mUseReactive) pReactive = nullptr;

    auto pCamera = mpScene->getCamera();

    ffx::DispatchDescUpscale dispatchUpscale{};
    ID3D12GraphicsCommandList* pCommandList = pRenderContext->getLowLevelData()->getCommandBufferNativeHandle().as<ID3D12GraphicsCommandList*>();
    dispatchUpscale.commandList = pCommandList;
    dispatchUpscale.color = ffxGetResourceApi(pRenderContext, pColorIn.get());
    dispatchUpscale.depth = ffxGetResourceApi(pRenderContext, pDepth.get());
    dispatchUpscale.motionVectors = ffxGetResourceApi(pRenderContext, pMotion.get());
    dispatchUpscale.exposure = ffxGetResourceApi(pRenderContext, nullptr);
    dispatchUpscale.output = ffxGetResourceApi(pRenderContext, pOut.get());
    dispatchUpscale.reactive = ffxGetResourceApi(pRenderContext, pReactive.get());;
    dispatchUpscale.transparencyAndComposition = ffxGetResourceApi(pRenderContext, nullptr);
    float2 jitterOffset = float2(pCamera->getJitterX(), -pCamera->getJitterY()) * float2(pColorIn->getWidth(), pColorIn->getHeight());
    dispatchUpscale.jitterOffset.x = jitterOffset.x;
    dispatchUpscale.jitterOffset.y = jitterOffset.y;
    dispatchUpscale.motionVectorScale.x = float(pColorIn->getWidth());
    dispatchUpscale.motionVectorScale.y = float(pColorIn->getHeight());
    dispatchUpscale.reset = mReset;
    mReset = false;
    dispatchUpscale.enableSharpening = mSharpness > 0.0f;
    dispatchUpscale.sharpness = mSharpness;
    dispatchUpscale.frameTimeDelta = float(mTimer.delta() * 1000.0);// FSR expects milliseconds
    dispatchUpscale.preExposure = 1.0f;
    dispatchUpscale.renderSize.width = pColorIn->getWidth();
    dispatchUpscale.renderSize.height = pColorIn->getHeight();
    dispatchUpscale.upscaleSize.width = pOut->getWidth();
    dispatchUpscale.upscaleSize.height = pOut->getHeight();
    dispatchUpscale.cameraFovAngleVertical = focalLengthToFovY(pCamera->getFocalLength(), Camera::kDefaultFrameHeight);
    dispatchUpscale.cameraFar = pCamera->getFarPlane();
    dispatchUpscale.cameraNear = pCamera->getNearPlane();
    dispatchUpscale.flags = 0;
    //dispatchUpscale.flags = FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
    ffx::ReturnCode retCode = ffx::Dispatch(mContext, dispatchUpscale);
    if (retCode != ffx::ReturnCode::Ok)
    {
        logWarning("Failed ffx::Dispatch");
    }

    pRenderContext->setPendingCommands(true);
    pRenderContext->uavBarrier(pOut.get());
    pRenderContext->flush();
}

void FSR::renderUI(Gui::Widgets& widget)
{
    widget.checkbox("Enabled", mEnabled);
    if (!mEnabled) return;

    widget.slider("Sharpness", mSharpness, 0.0f, 1.0f);

    widget.checkbox("Use Reactive Mask", mUseReactive);

    if(widget.button("Reset"))
    {
        mReset = true;
    }
}
