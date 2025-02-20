#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"
#include "Scene/Lighting/Lighting.slangh"
#include "Scene/Lighting/RayShadow.slangh"

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
#ifndef CULL_BACK_FACES
#define CULL_BACK_FACES 1
#endif

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
    uint gSampleCount;      // Sample count per pixel.
    uint gSampleIndex;      // looped jitter sequence sample index
    float gDLSSCorrectionStrength;

    float gMinVisibility; // everything below (and including) this transparency will be resolved via russian roulette or dither
    //int maxHits;
}

#define MIN_VISIBILITY 0.001

StructuredBuffer<uint> gTransparencyWhitelist;

RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<float2> gMotion;
RWTexture2D<float> gOpacity; // mask for TAA
RWTexture2D<float4> gTransparent; // rgba transparent color

struct RayData
{
    float4 color;
    float rayT; // input: TMax, output: TCurrent
    float minAlphaT; // RayT value of closest alpha-blended hit
    uint anyCount; // count of any hit shader invocations (can be used for independent mask selection)
};

void computeRayDifferentials(const TriangleHit hit, float3 rayDir, float hitT, const Camera camera, float2 frameDim, out float2 ddx, out float2 ddy, out float3 dOdx, out float3 dOdy)
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
    // Compute texture differentials
    float2 T[3];
    gScene.getVertexTexCoords(hit.instanceID, hit.primitiveIndex, T);
    float2 g1 = T[1] - T[0];
    float2 g2 = T[2] - T[0];
    float dsdx = (dudx * g1.x + dvdx * g2.x);
    float dtdx = (dudx * g1.y + dvdx * g2.y);
    float dsdy = (dudy * g1.x + dvdy * g2.x);
    float dtdy = (dudy * g1.y + dvdy * g2.y);
    ddx = float2(dsdx, dtdx);
    ddy = float2(dsdy, dtdy);
    // Compute object space differentials
    float3 O[3];
    gScene.getVertexPositionsO(hit.instanceID, hit.primitiveIndex, O);
    float3 og1 = O[1] - O[0];
    float3 og2 = O[2] - O[0];
    dOdx = dudx * og1 + dvdx * og2;
    dOdy = dudy * og1 + dvdy * og2;
}

float3 getPosO(const TriangleHit hit)
{
    float3 O[3];
    gScene.getVertexPositionsO(hit.instanceID, hit.primitiveIndex, O);
    float3 barycentrics = float3(1.0 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x, hit.barycentrics.y);
    return barycentrics.x * O[0] + barycentrics.y * O[1] + barycentrics.z * O[2];
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

bool useRussianRouletteForAlphaTest(RayData rayData)
{
    return rayData.color.w <= max(MIN_VISIBILITY, gMinVisibility);
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

    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    float2 ddx, ddy; // texture differentials
    float3 dOdx, dOdy; // object space differentials
    computeRayDifferentials(triangleHit, WorldRayDirection(), RayTCurrent(), gScene.camera, float2(frameDim), ddx, ddy, dOdx, dOdy);

    #if ALPHA_TEXTURE_LOD != 0
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
    if(alpha <= 0.01) IgnoreHit(); // no hit

    rayData.minAlphaT = min(rayData.minAlphaT, RayTCurrent()); // store if alpha was encountered
    rayData.anyCount++; // increase count for independent mask selection

    if(!useRussianRouletteForAlphaTest(rayData)) return; // accept this as hit
    // below: russian roulette for alpha test

    #if COVERAGE_CORRECTION == COVERAGE_CORRECTION_DLSS
    float dlssAlpha = saturate(0.0113 + 1.6560 * alpha - 0.8210 * alpha * alpha + 0.1560 * alpha * alpha * alpha);
    alpha = lerp(alpha, dlssAlpha, gDLSSCorrectionStrength * 4.0);
    #endif

    #if DITHER_MODE == DITHER_MODE_DISABLED
    
    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_4X) || (DITHER_MODE == DITHER_MODE_PER_PIXEL_16X)
    if (alpha < getPixelDitherThreshold(pixel, rayData.anyCount))
        IgnoreHit();
    #endif
    #if (DITHER_MODE == DITHER_PER_PIXEL_4X_ROULETTE)
    if (alpha < getPixelDitherThresholdPlusRoulette(pixel, gFrameCount, v.posW, rayData.anyCount))
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
    #if DITHER_MODE == DITHER_MODE_HASH_GRID
    //float maxDeriv = max(length(dOdx), length(dOdy));
    //float3 posO = getPosO(triangleHit);
    //if(!winHashGrid(alpha, posO, maxDeriv))
    //    IgnoreHit();
    float2 s = float2(1.0, 1.0);
    float maxDeriv = max(length(ddx*s), length(ddy*s));
    if(!winHashGrid2D(alpha, v.texC*s, maxDeriv, pixel, gFrameCount))
        IgnoreHit();
    #endif
    #if DITHER_MODE == DITHER_MODE_FRACTAL_DITHERING
    float maxDeriv = max(length(ddx), length(ddy));
    float2 s = float2(1.0, 1.0);
    if (!winFractalDither(alpha, s*v.texC, s*ddx, s*ddy, gScene.camera.data.projMat, gFrameCount))
        IgnoreHit();
    #endif
}

float2 calcMotionVector(uint2 pixel, uint2 frameDim, TriangleHit triangleHit, bool isTransparent)
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

    #if DITHER_MODE == DITHER_PER_PIXEL_4X_ROULETTE
    if (!isTransparent) return mvec;
    // Apply dithering to motion vector
    if(abs(mvec.x) < 1e-4) mvec.x = 0;
    if(abs(mvec.y) < 1e-4) mvec.y = 0;
    float2 pixelMvec = mvec * frameDim;
    float2 pixelMin = floor(pixelMvec * 0.5) * 2.0;
    float2 pixelMax = ceil(pixelMvec * 0.5) * 2.0;
    float2 f = frac(pixelMvec * 0.5);
    //if (f.x < 1e-2) f.x = 0; if (f.y < 1e-2) f.y = 0;
    //if (f.x > 1 - 1e-2) f.x = 1; if (f.y > 1-1e-2) f.y = 1;
    float rng = hash3D(float3(pixel, gFrameCount));
    // choose between min and max based on rng
    if (rng > f.x)
    {
        if((rng - f.x) / (1 - f.x) > f.y) pixelMvec = pixelMin;
        else pixelMvec = float2(pixelMin.x, pixelMax.y);
    }
    else // rng <= f.x
    {
        if(rng/f.x > f.y) pixelMvec = float2(pixelMax.x, pixelMin.y);
        else pixelMvec = pixelMax;
    }
    //pixelMvec = round(mvec * frameDim * 0.5) * 2.0; // round to 2x2 grid
    mvec = pixelMvec / frameDim;

    #endif
    return mvec;
}

float computeShadowImpl(uint2 pixel, uint lightIndex, float3 posW, float3 normalW)
{
    // posW + normal?
    return traceShadowRay(posW, normalW, lightIndex);
}

[shader("closesthit")]
void closestHit(inout RayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes){
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;    

    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    
    if(useRussianRouletteForAlphaTest(rayData))
    {
        // accept as 100% opaque hit (due to russian roulette)
        gVBuffer[pixel] = HitInfo(triangleHit).getData();
        gMotion[pixel] = calcMotionVector(pixel, frameDim, triangleHit, rayData.minAlphaT <= RayTCurrent());
        gOpacity[pixel] = rayData.minAlphaT <= RayTCurrent() ? 1.0 : 0.0;
        return; // fast path if min visibility is not used
    }

    // lod
    #if ALPHA_TEXTURE_LOD != 0
    float2 ddx, ddy;
    computeRayDifferentials(triangleHit, WorldRayDirection(), RayTCurrent(), gScene.camera, float2(frameDim), ddx, ddy);
    let lod = ExplicitGradientTextureSampler(ddx, ddy);
    #else
    let lod = ExplicitLodTextureSampler(0.0);
    #endif  

    // eval alpha to decide wheter transparent hit or final vbuffer entry (opaque)
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    uint materialID = gScene.getMaterialID(instanceID);
    let material = gScene.materials.getMaterial(materialID);   
    float alpha = material.evalOpacity(gScene.materials, v, lod);

    float newVisibility = rayData.color.w * (1.0 - alpha);
    if(newVisibility <= MIN_VISIBILITY)
    {
        // write to vbuffer (last entry)
        rayData.color.w = rayData.color.w * alpha; // correct visibility to blend with the last object
        // leave rayT as is, to stop ray
        gVBuffer[pixel] = HitInfo(triangleHit).getData();
        gMotion[pixel] = calcMotionVector(pixel, frameDim, triangleHit, rayData.minAlphaT <= RayTCurrent());
        gOpacity[pixel] = rayData.minAlphaT <= RayTCurrent() ? 1.0 : 0.0;
    }
    else // full shading
    {
        rayData.color.rgb += rayData.color.w * alpha * shadeVBuffer(triangleHit, pixel, frameDim, -WorldRayDirection()).rgb;
        rayData.rayT = RayTCurrent(); // continue ray here (if color.w > 0)
        rayData.color.w = newVisibility;
    }
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);

    RayData data;
    data.minAlphaT = ray.tMax; // no alpha encountered yet
    data.color = float4(0, 0, 0, 1); // rbb+visibility
    data.anyCount = 0u;
    uint rayFlags = 0;
    if (CULL_BACK_FACES) rayFlags |= RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

    for(uint i = 0; i < 256; ++i)
    {
        data.rayT = ray.tMax; // overwrite with max t
        TraceRay(gScene.rtAccel, rayFlags, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), data);
        ray.tMin = data.rayT; // set ray t from hitpoint to new start
        if(data.color.w <= MIN_VISIBILITY || ray.tMin >= ray.tMax) break;
    }

    gTransparent[pixel] = data.color;
}
