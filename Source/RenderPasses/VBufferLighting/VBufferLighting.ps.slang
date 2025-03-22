#include "Scene/Lighting/Lighting.slangh"
#include "Scene/Lighting/RayShadow.slangh"

Texture2DArray<float> visibilityBuffer;
Texture2D<PackedHitInfo> vbuffer;
Texture2D<float4> rayDir;

#ifndef USE_RAY_SHADOW
#define USE_RAY_SHADOW 0
#endif

float computeShadowImpl(uint2 pixel, uint lightIndex, float3 posW, float3 normalW)
{
    #if USE_RAY_SHADOW != 0
    return traceShadowRay(posW, normalW, lightIndex);
    #else
    uint visWidth, visHeight, visSlices;
    visibilityBuffer.GetDimensions(visWidth, visHeight, visSlices);

    if (lightIndex >= visSlices) return 1.0; // no shadow available
    return visibilityBuffer[uint3(pixel, lightIndex)];
    #endif
}

float4 main(float2 texC : TEXCOORD, float4 svPos : SV_POSITION) : SV_TARGET
{
    // pixel coordinates
    uint2 pixel = uint2(svPos.xy);
    uint2 frameDim;
    vbuffer.GetDimensions(frameDim.x, frameDim.y);
    
    const HitInfo hit = HitInfo(vbuffer[pixel]);

    float3 color = 0.0;
    float3 viewDir = -rayDir[pixel].xyz;
    if (all(viewDir == 0.0)) viewDir = -gScene.camera.computeRayPinhole(pixel, frameDim).dir;
    return shadeVBuffer(hit, pixel, frameDim, viewDir);
}
