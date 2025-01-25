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

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
}

RWTexture2D<PackedHitInfo> gVBuffer;
RWTexture2D<float2> gMotion;

struct RayData
{
    int dummy;
};

//Load in shading data from hit and direction
ShadingData loadShadingData(const HitInfo hit, const float3 rayDir, const ITextureSampler lod)
{
    const TriangleHit triangleHit = hit.getTriangleHit();
    VertexData v = gScene.getVertexData(triangleHit);
    uint materialID = gScene.getMaterialID(triangleHit.instanceID);
    ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -rayDir, lod);
    
    return sd;
}

[shader("miss")]
void miss(inout RayData rayData : SV_RayPayload)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    HitInfo hitInfo = { };
    gVBuffer[pixel] = hitInfo.getData();
    // TODO calclate proper motion vectors for environment
    gMotion[pixel] = float2(100.0, 100.0); // use large motion vectors to prevent reprojection (invalidate history)
}

//Use anyhit for alpha test if activated
[shader("anyhit")]
void anyHit(inout RayData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // Alpha test for non-opaque geometry.
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    uint materialID = gScene.getMaterialID(instanceID);
    // TODO LOD?
    // TODO dither options
    if (gScene.materials.alphaTest(v, materialID, 0.f))
        IgnoreHit();
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
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);

    RayData dummy; // dummy payload
    TraceRay(gScene.rtAccel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), dummy);
}
