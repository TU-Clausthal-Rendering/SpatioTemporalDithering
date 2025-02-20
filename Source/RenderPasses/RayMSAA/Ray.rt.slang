#include "Utils/Math/MathConstants.slangh"
import Scene.Raytracing;
import Rendering.Materials.TexLODHelpers;
import Rendering.Materials.TexLODTypes;

Texture2D<float> gDepthTex;
Texture2D<uint> gEdgeTex;
RWTexture2DArray<float> gDepthSamples;

SamplerState gSampler;

#ifndef LARGE_DISPATCH
#define LARGE_DISPATCH 1
#endif
#ifndef MSAA_SAMPLES
#define MSAA_SAMPLES 8
#endif

struct RayData
{
    float depth;
};

// sample positions in (-0.5, 0.5)
static const float2 sample_pos[8] = {
    float2( 1.0 / 16.0, -3.0 / 16.0),
    float2(-1.0 / 16.0,  3.0 / 16.0),
    float2( 5.0 / 16.0,  1.0 / 16.0),
    float2(-3.0 / 16.0, -5.0 / 16.0),
    float2(-5.0 / 16.0,  5.0 / 16.0),
    float2(-7.0 / 16.0, -1.0 / 16.0),
    float2( 3.0 / 16.0,  7.0 / 16.0),
    float2( 7.0 / 16.0, -7.0 / 16.0)
};

float RayToViewDepth(float3 rayDir, float rayT)
{
    float cosTheta = dot(normalize(gScene.camera.data.cameraW), rayDir);
    return rayT * cosTheta;
}

float ViewDepthToRay(float3 rayDir, float viewDepth)
{
    float cosTheta = dot(normalize(gScene.camera.data.cameraW), rayDir);
    return viewDepth / cosTheta;    
}

ExplicitRayConesLodTextureSampler computeLod(VertexData v, float3 rayDir)
{
    RayCone rc = RayCone(0.0, RAY_CONE_SPREAD);
    rc = rc.propagateDistance(RayTCurrent());
    float lambda = rc.computeLOD(v.coneTexLODValue, rayDir, v.faceNormalW);
    return ExplicitRayConesLodTextureSampler(lambda);
}

[shader("anyhit")]
void anyHit(inout RayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    //ExplicitLodTextureSampler lod = { 0.0 };
    let lot = computeLod(v, WorldRayDirection());

    if (gScene.materials.alphaTest(v, materialID, 0.f)) IgnoreHit();
}

[shader("miss")]
void miss(inout RayData rayData)
{
}

[shader("closesthit")]
void closestHit(inout RayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    rayData.depth = RayTCurrent();
}

[shader("raygeneration")]
void rayGen()
{
    uint3 id = DispatchRaysIndex();
    float2 uv = (id.xy + 0.5) / DispatchRaysDimensions().xy;
    float2 gStep = 1.0 / DispatchRaysDimensions().xy;
    uint edge = gEdgeTex[id.xy];
    if(!edge) return;

    // obtain ray boundaries
    float2 offsets[9] = {
        float2(-1, -1), float2(0, -1), float2(1, -1),
        float2(-1,  0), float2(0,  0), float2(1,  0),
        float2(-1,  1), float2(0,  1), float2(1,  1)
    };
    
    float minDepth = FLT_MAX;
    float maxDepth = 0.0;
    // fetch local depth values
    [unroll] for(uint i = 0; i < 9; ++i)
    {
        float d = gDepthTex.SampleLevel(gSampler, uv + offsets[i] * gStep, 0);
        minDepth = min(minDepth, d);
        maxDepth = max(maxDepth, d);
    }

    #if LARGE_DISPATCH
    {
    #else // small dispatch size => loop
    for(uint i = 0; i < MSAA_SAMPLES; ++i) {
        id.z = i;
    #endif

        float2 pixel = float2(id.xy) + 0.5 + sample_pos[id.z];
        RayDesc ray;
        ray.Origin = gScene.camera.data.posW;
        ray.Direction = normalize(gScene.camera.computeNonNormalizedRayDirPinhole(id.xy, DispatchRaysDimensions().xy, sample_pos[id.z] + 0.5));    
        ray.TMin = minDepth;
        ray.TMax = maxDepth;
        //ray.TMin = gScene.camera.data.nearZ;
        //ray.TMax = gScene.camera.data.farZ;

        // convert to proper ray depth
        ray.TMin = ViewDepthToRay(ray.Direction, ray.TMin);
        ray.TMax = ViewDepthToRay(ray.Direction, ray.TMax);

        RayData rayData;
        rayData.depth = FLT_MAX;

        TraceRay(
            gScene.rtAccel,
            RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
                 kInstanceCameraParticle /* instanceInclusionMask */, 0 /* hitIdx */, 1 /*ray type count*/, 0 /* missIdx */,
            ray, rayData
        );

        gDepthSamples[id] = RayToViewDepth(ray.Direction, rayData.depth);

    }
}
