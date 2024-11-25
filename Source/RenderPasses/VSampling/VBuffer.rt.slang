#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raytracing;
import Scene.Intersection;
import Utils.Math.MathHelpers;
import Utils.Geometry.GeometryHelpers;
import Utils.Sampling.SampleGenerator;
import Rendering.Lights.LightHelpers;

cbuffer CB
{
    uint gFrameCount;        // Frame count since scene was loaded.
}

RWTexture2D<float4> gColorOut;

static const uint kMaxBounces = MAX_BOUNCES;
static const bool kUseAnalyticLights = USE_ANALYTIC_LIGHTS;
static const bool kUseEmissiveLights = USE_EMISSIVE_LIGHTS;
static const bool kUseEnvLight = USE_ENV_LIGHT;
static const bool kUseEnvBackground = USE_ENV_BACKGROUND;
static const float3 kDefaultBackgroundColor = float3(0, 0, 0);
static const float kRayTMax = FLT_MAX;

static const float kMinThroughput = 0.00001;
static const bool kUseRussianRoulettePath = USE_RUSSIAN_ROULETTE_PATH;
static const bool kUseRussianRouletteShadow = USE_RUSSIAN_ROULETTE_PATH;
static const bool kUseRussianRouletteOpacity = USE_RUSSIAN_ROULETTE_PATH; // seems to have a little error? but very similar results


struct ScatterRayData
{
    float3  radiance;       ///< Accumulated outgoing radiance from path.
    /*bool    terminated;     ///< Set to true when path is terminated.
    float3  thp;            ///< Current path throughput. This is updated at each path vertex.
    uint    pathLength;     ///< Path length in number of path segments (0 at origin, 1 at first secondary hit, etc.). Max 2^31.
    float3  origin;         ///< Next path segment origin.
    float3  direction;      ///< Next path segment direction.

    SampleGenerator sg;     ///< Per-ray state for the sample generator (up to 16B).

    __init(SampleGenerator sg)
    {
        this.terminated = false;
        this.pathLength = 0;
        this.radiance = float3(0, 0, 0);
        this.thp = float3(1, 1, 1);
        this.origin = float3(0, 0, 0);
        this.direction = float3(0, 0, 0);
        this.sg = sg;
    }*/
};
/*
ShadingData loadShadingData(const HitInfo hit, const float3 rayOrigin, const float3 rayDir, const ITextureSampler lod)
{
    VertexData v = {};
    uint materialID = {};

#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }
#endif

    ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -rayDir, lod);

    return sd;
}

void traceScatterRay(inout ScatterRayData rayData)
{
    RayDesc ray;
    ray.Origin = rayData.origin;
    ray.Direction = rayData.direction;
    ray.TMin = 0.f;
    ray.TMax = kRayTMax;

    uint rayFlags = 0;      // TODO: Set cull mode from the app
    TraceRay(gScene.rtAccel, rayFlags, 0xff , 0 , rayTypeCount, 0 , ray, rayData);
}

float3 evalDirectAnalytic(const ShadingData sd, const IMaterialInstance mi, inout SampleGenerator sg)
{
    const uint lightCount = gScene.getLightCount();
    if (lightCount == 0) return float3(0.f);

    // Pick one of the analytic light sources randomly with equal probability.
    const uint lightIndex = min(uint(sampleNext1D(sg) * lightCount), lightCount - 1);
    float invPdf = lightCount; // Light selection pdf = 1.0 / lightCount.

    // Sample local light source.
    AnalyticLightSample ls;
    if (!sampleLight(sd.posW, gScene.getLight(lightIndex), sg, ls)) return float3(0.f);

    // Reject sample if not in the hemisphere of a BSDF lobe.
    const uint lobeTypes = mi.getLobeTypes(sd);
    const bool hasReflection = lobeTypes & uint(LobeType::Reflection);
    const bool hasTransmission = (lobeTypes & uint(LobeType::Transmission)) || (sd.opacity < 1.0);
    float NdotL = dot(sd.getOrientedFaceNormal(), ls.dir);
    if ((NdotL <= kMinCosTheta && !hasTransmission) || (NdotL >= -kMinCosTheta && !hasReflection))
        return float3(0.f);

    // Get origin with offset applied in direction of the geometry normal to avoid self-intersection.
    const float3 origin = computeRayOrigin(sd.posW, dot(sd.faceN, ls.dir) >= 0.f ? sd.faceN : -sd.faceN);

    // Test visibility by tracing a shadow ray.
    float V = 1.0;//traceShadowRay(origin, ls.dir, ls.distance, sg);
    if (V < 0.01) return float3(0.0);

    // flip light direction for evaluation when opacity object is hit from the other side
    if(NdotL < 0.0 && sd.opacity < 1.0)
    {
        ls.dir = -ls.dir;
        V *= (1.0 - sd.opacity);
    }
    return V * mi.eval(sd, ls.dir, sg) * ls.Li * invPdf;
}

bool generateScatterRay(const ShadingData sd, const IMaterialInstance mi, bool isCurveHit, float3 rayOrigin, inout ScatterRayData rayData)
{
    float rng1 = sampleNext1D(rayData.sg);

    //if (sd.opacity < 1.0)
    if(rng1 > sd.opacity) // for opacity = 1: always eval bsdf
    {
        // throuput is already adjusted via russion roulette (rng1 > sd.opacity)
        // opacity not hit => trace further: uncomment below if the russion roulette on top is disabled (rng1 > sd.opacity)
        //if(kUseRussianRoulettePath)
        //{
        //    float rng = sampleNext1D(rayData.sg);
        //    if(rng > saturate(1.0 - sd.opacity))
        //       rayData.thp = 0.0; // failed roulette
        //}
        //else rayData.thp *= saturate(1.0 - sd.opacity);

        rayData.origin = sd.computeNewRayOrigin(false);
        return any(rayData.thp > kMinThroughput);
    }

    // Sample material.
    BSDFSample bsdfSample;
    if (mi.sample(sd, rayData.sg, bsdfSample, true))
    {
        rayData.origin = rayOrigin;
        if (!isCurveHit && bsdfSample.isLobe(LobeType::Transmission))
        {
            rayData.origin = sd.computeNewRayOrigin(false);
        }
        rayData.direction = bsdfSample.wo;
        if(kUseRussianRoulettePath)
        {
            float rng = sampleNext1D(rayData.sg);
            if(rng > dot(bsdfSample.weight, 0.33))
               rayData.thp = 0.0; // failed roulette
            else rayData.thp *= bsdfSample.weight / dot(bsdfSample.weight, 0.33);
        }
        else rayData.thp *= bsdfSample.weight;

        return any(rayData.thp > kMinThroughput);
    }

    return false;
}

void handleHit(const HitInfo hit, inout ScatterRayData rayData)
{
    const bool isCurveHit = hit.getType() == HitType::Curve;
    let lod = ExplicitLodTextureSampler(0.f);

    // Load shading data.
    ShadingData sd = loadShadingData(hit, rayData.origin, rayData.direction, lod);
    if(kUseRussianRouletteOpacity) sd.opacity = 1.0;

    // Create material instance.
    let mi = gScene.materials.getMaterialInstance(sd, lod);

    // Add emitted light.
    if (kUseEmissiveLights)
    {
        rayData.radiance += sd.opacity * rayData.thp * mi.getProperties(sd).emission;
    }

    // Check whether to terminate based on max depth.
    if (rayData.pathLength >= kMaxBounces)
    {
        rayData.terminated = true;
        return;
    }

    // Compute ray origin for new rays spawned from the hit.
    float3 rayOrigin;
    if (isCurveHit)
    {
        // For curves, we set the new origin at the sphere center.
        rayOrigin = sd.posW - sd.curveRadius * sd.frame.N;
    }
    else
    {
        rayOrigin = sd.computeNewRayOrigin();
    }

    // Add contribution of direct light from analytic lights.
    if (kUseAnalyticLights)
    {
        float3 Lr = evalDirectAnalytic(sd, mi, rayData.sg);
        rayData.radiance += sd.opacity * rayData.thp * Lr;
    }

    // Generate scatter ray for the next path segment.
    // The raygen shader will continue the path based on the returned payload.
    if (!generateScatterRay(sd, mi, isCurveHit, rayOrigin, rayData))
    {
        rayData.terminated = true;
        return;
    }

    rayData.pathLength++;
}
*/

[shader("miss")]
void miss(inout ScatterRayData rayData)
{
    /*// Ray missed the scene. Mark the ray as terminated.
    rayData.terminated = true;

    // Add contribution from distant light (env map) in this direction.
    if (kUseEnvLight && rayData.pathLength > 0)
    {
        float3 Le = gScene.envMap.eval(WorldRayDirection());
        rayData.radiance += rayData.thp * Le;
    }*/
}


[shader("anyhit")]
void anyHit(inout ScatterRayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    /*// Alpha test for non-opaque geometry.
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    ExplicitLodTextureSampler lod = { 0.0 };

    {
        let material = gScene.materials.getMaterial(materialID);
        float alpha = material.evalOpacity(gScene.materials, v, lod);
        if(kUseRussianRouletteOpacity)
        {
            float rng = sampleNext1D(rayData.sg);
            if (rng > alpha) IgnoreHit();
        }

        if (alpha <= 0.01f) IgnoreHit(); 
    }*/
}

[shader("closesthit")]
void closestHit(inout ScatterRayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    /*TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    handleHit(HitInfo(triangleHit), rayData);*/
}

[shader("raygeneration")]
void rayGen()
{
    /*uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    const float3 primaryRayOrigin = gScene.camera.getPosition();
    const float3 primaryRayDir = gScene.camera.computeRayPinhole(pixel, frameDim).dir;
    SampleGenerator sg = SampleGenerator(pixel, gFrameCount);

    ScatterRayData rayData = ScatterRayData(sg);
    rayData.direction = primaryRayDir;
    rayData.origin = primaryRayOrigin;

    // Follow path into the scene and compute its total contribution.
    for (uint depth = 0; depth <= (kMaxBounces + 1u) && !rayData.terminated; depth++)
    {
        // Trace scatter ray. If it hits geometry, the closest hit shader samples
        // direct illumination and generates the next scatter ray.
        traceScatterRay(rayData);
    }

    float3 color = rayData.radiance;


    gColorOut[pixel] = float4(color, 1.f);*/
}
