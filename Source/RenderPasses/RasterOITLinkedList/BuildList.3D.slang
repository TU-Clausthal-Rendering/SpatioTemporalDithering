#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"
#include "Scene/Lighting/Lighting.slangh"
#include "Scene/Lighting/RayShadow.slangh"

import Scene.Raster;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGenerator;
import Utils.Math.PackedFormats;
import Utils.Math.FormatConversion;

RWTexture2D<uint> gHead;

struct BufferData // 4 * 4 byte 
{
    uint next;
    float depth;
    half4 color;
};

RWStructuredBuffer<BufferData> gBuffer;
RWStructuredBuffer<uint> gCount; // current count for the buffer

cbuffer PerFrame
{
    uint2 gFrameDim;
    uint maxElements;
}

VSOut vsMain(VSIn vIn)
{
    return defaultVS(vIn);
}

float computeShadowImpl(uint2 pixel, uint lightIndex, float3 posW, float3 normalW)
{
    return traceShadowRay(posW, normalW, lightIndex);
}

void psMain(VSOut v, uint triangleIndex : SV_PrimitiveID, float3 barycentrics : SV_Barycentrics, bool isFrontFace : SV_IsFrontFace)
{
    uint2 pixel = uint2(v.posH.xy);
    
    TriangleHit triangleHit;
    triangleHit.instanceID = v.instanceID;
    triangleHit.primitiveIndex = triangleIndex;
    triangleHit.barycentrics = barycentrics.yz;

    float3 viewDir = -gScene.camera.computeRayPinhole(pixel, gFrameDim).dir;
    // TODO optimize by first evaluating alpha and then calculating the color
    float4 shadedColor = shadeVBuffer(triangleHit, pixel, gFrameDim, viewDir);
    if (shadedColor.a < 0.01)
        discard;

    uint idx = 0xffffffff;
    InterlockedAdd(gCount[0], 1, idx);
    if (idx >= maxElements)
        discard;

    BufferData data;
    data.depth = v.posH.z;
    data.color = half4(shadedColor);
    // change head pointer to this element, and use previous head as next from this element
    InterlockedExchange(gHead[pixel], idx, data.next);

    // store buffer data
    gBuffer[idx] = data;
}
