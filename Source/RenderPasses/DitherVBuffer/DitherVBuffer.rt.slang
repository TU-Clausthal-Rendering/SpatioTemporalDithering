#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raytracing;
import Scene.Intersection;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGenerator;
import Utils.Math.PackedFormats;
import Utils.Math.FormatConversion;
import Rendering.Materials.IsotropicGGX;
import Utils.Color.ColorHelpers;

#include "Dither.slangh"

#ifndef ALPHA_TEXTURE_LOD
#define ALPHA_TEXTURE_LOD 0
#endif

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
    uint gSampleCount;      // Sample count per pixel.
    uint gSampleIndex;      // looped jitter sequence sample index
    float gDLSSCorrectionStrength;
}

StructuredBuffer<uint> gTransparencyWhitelist;

RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<float2> gMotion;
RWTexture2D<float> gOpacity;

struct RayData
{
    float minAlphaT; // RayT value of closest alpha-blended hit
};

void computeRayDifferentials(const TriangleHit hit, float3 rayDir, float hitT, const Camera camera, float2 frameDim, out float2 ddx, out float2 ddy)
{
    // Ray differentials
    float3 P[3];
    gScene.getVertexPositionsW(hit.instanceID, hit.primitiveIndex, P);
    float3 e1 = P[1] - P[0];
    float3 e2 = P[2] - P[0];
    float3 d = rayDir;
    float k = dot(cross(e1, e2), d);
    k = abs(k) > 1e-20f ? rcp(k) : 0.0f;
    float3 cu = cross(e2, d);
    float3 cv = cross(d, e1);
        // Assumes a normalized ray direction
    float3 dx = camera.data.cameraU * 2.f / (camera.data.focalDistance * frameDim.x); // dDdx in ray gen
    float3 dy = camera.data.cameraV * 2.f / (camera.data.focalDistance * frameDim.y); // dDdy in ray gen
    float3 q = dx * hitT; // Transfer to primary hit
    float3 r = dy * hitT;
    float dudx = k * dot(cu, q);
    float dudy = k * dot(cu, r);
    float dvdx = k * dot(cv, q);
    float dvdy = k * dot(cv, r);
    float2 T[3];
    gScene.getVertexTexCoords(hit.instanceID, hit.primitiveIndex, T);
    float2 g1 = T[1] - T[0];
    float2 g2 = T[2] - T[0];
    float dsdx = (dudx * g1.x + dvdx * g2.x);
    float dsdy = (dudy * g1.x + dvdy * g2.x);
    float dtdx = (dudx * g1.y + dvdx * g2.y);
    float dtdy = (dudy * g1.y + dvdy * g2.y);
    ddx = float2(dsdx, dtdx);
    ddy = float2(dsdy, dtdy);
}

// posV: view space position (negative z)
// return: texture uv [0, 1]
float2 ViewSpaceToUV(float3 posV)
{
    const float2 imageScale = 0.5 * float2(gScene.camera.data.frameWidth / gScene.camera.data.focalLength, gScene.camera.data.frameHeight / gScene.camera.data.focalLength);
    float2 ndc = posV.xy / (imageScale * posV.z);
    return ndc * float2(-0.5, 0.5) + 0.5; // since posV.z is negative, the sign order is inversed
}

[shader("miss")]
void miss(inout RayData rayData : SV_RayPayload)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    HitInfo hitInfo = { };
    gVBuffer[pixel] = hitInfo.getData();
    float3 rayDirNoJitter = gScene.camera.computeRayPinhole(pixel, frameDim, /*no jitter*/false).dir;
    float3 prevEnvMapDir = mul(gScene.camera.data.prevViewMat, float4(rayDirNoJitter, 0.0)).xyz;
    float2 prevUV = ViewSpaceToUV(prevEnvMapDir);
    gMotion[pixel] = prevUV - (float2(pixel)+0.5) / float2(frameDim);
    gOpacity[pixel] = rayData.minAlphaT < RayTCurrent() ? 1.0 : 0.0;
}

//Use anyhit for alpha test if activated
[shader("anyhit")]
void anyHit(inout RayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;
    // Alpha test for non-opaque geometry.
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    uint materialID = gScene.getMaterialID(instanceID);

    //if(materialID < 111) IgnoreHit();
    // lod
    #if ALPHA_TEXTURE_LOD != 0
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    float2 ddx, ddy;
    computeRayDifferentials(triangleHit, WorldRayDirection(), RayTCurrent(), gScene.camera, float2(frameDim), ddx, ddy);
    let lod = ExplicitGradientTextureSampler(ddx, ddy);
    #else
    let lod = ExplicitLodTextureSampler(0.0);
    #endif

    #if TRANSPARENCY_WHITELIST != 0
    uint isTransparent = (gTransparencyWhitelist[materialID / 32u] & (1u << (materialID % 32u)));
    if(isTransparent == 0u)
    {
        // use default alpha test if not marked as transparent
        if (gScene.materials.alphaTest(v, materialID, lod))
            IgnoreHit();
        return;
    }
    #endif

    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v, lod);
    if(alpha > 0.01)
        rayData.minAlphaT = min(rayData.minAlphaT, RayTCurrent()); // store if alpha was encountered

    #if COVERAGE_CORRECTION == COVERAGE_CORRECTION_DLSS
    //alpha = pow(saturate(alpha), 0.656);
    if(alpha > 0.01)
    {
        float dlssAlpha = saturate(0.0113 + 1.6560 * alpha - 0.8210 * alpha * alpha + 0.1560 * alpha * alpha * alpha);
        alpha = lerp(alpha, dlssAlpha, gDLSSCorrectionStrength);
    }
    #endif

    #if DITHER_MODE == DITHER_MODE_DISABLED
    
    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_4X) || (DITHER_MODE == DITHER_MODE_PER_PIXEL_16X)
    if (alpha < getPixelDitherThreshold(pixel))
        IgnoreHit();
    #endif
    #if DITHER_MODE == DITHER_MODE_PER_JITTER
    if(!isCovered(pixel, alpha, gSampleIndex, gSampleCount, gFrameCount, v.posW))
        IgnoreHit();
    #endif
    #if DITHER_MODE == DITHER_MODE_RUSSIAN_ROULETTE
    if(!winRoulette(alpha, gFrameCount, v.posW))
        IgnoreHit();
    #endif
    #if DITHER_MODE == DITHER_MODE_PERIODIC
    if(!winRotation(pixel, alpha, gFrameCount))
        IgnoreHit();
    #endif
}

float2 calcMotionVector(uint2 pixel, uint2 frameDim, TriangleHit triangleHit)
{
    float3 prevPosW = gScene.getPrevPosW(triangleHit);

    float2 pixelPos = float2(pixel) + 0.5;
    float4 prevPosH = mul(gScene.camera.data.prevViewProjMatNoJitter, float4(prevPosW, 1.0));
    return calcMotionVector(pixelPos, prevPosH, frameDim) + float2(gScene.camera.data.jitterX, -gScene.camera.data.jitterY); // Remove camera jitter from motion vector
}

[shader("closesthit")]
void closestHit(inout RayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes){
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;    

    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    
    gVBuffer[pixel] = HitInfo(triangleHit).getData();
    gMotion[pixel] = calcMotionVector(pixel, frameDim, triangleHit);
    gOpacity[pixel] = rayData.minAlphaT <= RayTCurrent() ? 1.0 : 0.0;
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);

    RayData data;
    data.minAlphaT = ray.tMax; // no alpha encountered yet
    //TraceRay(gScene.rtAccel, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), data);
    TraceRay(gScene.rtAccel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), data);
}
