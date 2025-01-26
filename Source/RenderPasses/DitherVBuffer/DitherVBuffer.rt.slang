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

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
    uint gSampleCount;      // Sample count per pixel.
    uint gSampleIndex;      // looped jitter sequence sample index
}

RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<float2> gMotion;
RWTexture2D<float> gDepth; // normalized depth for DLSS (0-1 range)

struct RayData
{
    float RayTMax;
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

[shader("miss")]
void miss(inout RayData rayData : SV_RayPayload)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    HitInfo hitInfo = { };
    gVBuffer[pixel] = hitInfo.getData();
    // TODO calclate proper motion vectors for environment
    gMotion[pixel] = 0.0;
    gDepth[pixel] = 1.0;
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
    // lod
    /*TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    float2 ddx, ddy;
    computeRayDifferentials(triangleHit, WorldRayDirection(), RayTCurrent(), gScene.camera, float2(frameDim), ddx, ddy);
    let lod = ExplicitGradientTextureSampler(ddx, ddy);*/
    let lod = ExplicitLodTextureSampler(0.0);

    #if DITHER_MODE == DITHER_MODE_DISABLED
    if (gScene.materials.alphaTest(v, materialID, lod))
        IgnoreHit();
    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_4X) || (DITHER_MODE == DITHER_MODE_PER_PIXEL_16X)
    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v, lod);
    if (alpha < getPixelDitherThreshold(pixel))
        IgnoreHit();
    #endif
    #if DITHER_MODE == DITHER_MODE_PER_JITTER
    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v, lod);
    if(!isCovered(pixel, alpha, gSampleIndex, gSampleCount))
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
    gDepth[pixel] = RayTCurrent() / rayData.RayTMax; // 0-1 range
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);

    RayData data;
    data.RayTMax = ray.tMax;
    TraceRay(gScene.rtAccel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), data);
}
