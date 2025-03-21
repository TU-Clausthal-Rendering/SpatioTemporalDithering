#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"
#include "Scene/Lighting/Lighting.slangh"
#include "Scene/Lighting/RayShadow.slangh"

import Scene.Raster;
import Scene.RaytracingInline;
import Scene.Intersection;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGenerator;
import Utils.Math.PackedFormats;
import Utils.Math.FormatConversion;
import Rendering.Materials.IsotropicGGX;
import Utils.Color.ColorHelpers;

#include "../DitherVBuffer/Dither.slangh"

#ifndef ALPHA_TEXTURE_LOD
#define ALPHA_TEXTURE_LOD 0
#endif
#ifndef CULL_BACK_FACES
#define CULL_BACK_FACES 1
#endif
#ifndef TRANSPARENCY_WHITELIST
#define TRANSPARENCY_WHITELIST 0
#endif

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
    uint gSampleCount;      // Sample count per pixel.
    uint gSampleIndex;      // looped jitter sequence sample index
    float gDLSSCorrectionStrength;
    int gAlignMotionVectors; // align motion vectors to grid spacing when moving
    uint2 gFrameDim;
}

StructuredBuffer<uint> gTransparencyWhitelist;

VSOut vsMain(VSIn vIn)
{
    return defaultVS(vIn);
}

struct PsOut
{
    PackedHitInfo vbuffer : SV_Target0;
    float2 mvec : SV_Target1;
};

float2 calcMotionVector(uint2 pixel, uint2 frameDim, TriangleHit triangleHit)
{
    float3 prevPosW = gScene.getPrevPosW(triangleHit);

    float2 pixelPos = float2(pixel) + 0.5;
    float4 prevPosH = mul(gScene.camera.data.prevViewProjMatNoJitter, float4(prevPosW, 1.0));

    float2 prevCrd = prevPosH.xy / prevPosH.w;
#ifdef FALCOR_FLIP_Y
    prevCrd *= float2(0.5, 0.5);
#else
    prevCrd *= float2(0.5, -0.5);
#endif
    prevCrd += 0.5f;
    float2 normalizedCrd = pixelPos / frameDim;
    float2 mvec = prevCrd - normalizedCrd;
    mvec += float2(gScene.camera.data.jitterX, -gScene.camera.data.jitterY); // Remove camera jitter from motion vector

    return mvec;
}

// [earlydepthstencil] TODO test performance
PsOut psMain(VSOut vsOut, uint triangleIndex : SV_PrimitiveID, float3 barycentrics : SV_Barycentrics)
{
    PsOut o;
    TriangleHit triangleHit;
    triangleHit.instanceID = vsOut.instanceID;
    triangleHit.primitiveIndex = triangleIndex;
    triangleHit.barycentrics = barycentrics.yz;
    o.vbuffer = triangleHit.pack();
    o.mvec = calcMotionVector(int2(vsOut.posH.xy), gFrameDim, triangleHit);
    return o;
}
