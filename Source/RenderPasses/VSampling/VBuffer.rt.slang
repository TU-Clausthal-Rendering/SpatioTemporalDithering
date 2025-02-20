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

struct PathSample
{
    float3 radiance; // unshadowed: thp * opacity * radiance
    float weight; // scalar: thp * opacity * radiance
    float3 lightDir;
    float distance;
    float3 posW; // world space position
}

struct ScatterRayData
{
    PackedHitInfo packedHitInfo; // 16 byte
    SampleGenerator sg; // 16 byte
};

struct ShadowRayData
{
    SampleGenerator sg; // 16 byte
    float visible; // 4 byte
};

ShadingData loadShadingData(const HitInfo hit, const float3 rayOrigin, const float3 rayDir, const ITextureSampler lod)
{
    VertexData v = {};
    uint materialID = {};

    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }

    ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -rayDir, lod);

    return sd;
}

HitInfo traceScatterRay(inout ScatterRayData rayData, float3 origin, float3 direction)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = kRayTMax;

    uint rayFlags = 0;
    TraceRay(gScene.rtAccel, rayFlags, kInstanceCameraParticle , 0 , rayTypeCount, 0 , ray, rayData);
    return HitInfo(rayData.packedHitInfo);
}

float3 evalDirectAnalytic(const ShadingData sd, const IMaterialInstance mi, inout SampleGenerator sg, out float3 lightDir, out float lightDistance, out float3 posW)
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
    posW = computeRayOrigin(sd.posW, dot(sd.faceN, ls.dir) >= 0.f ? sd.faceN : -sd.faceN);

    float V = 1.0; // visibility factor

    // Store information for the shadow ray
    lightDir = ls.dir;
    lightDistance = ls.distance;

    // flip light direction for evaluation when opacity object is hit from the other side
    if(NdotL < 0.0 && sd.opacity < 1.0)
    {
        ls.dir = -ls.dir;
        V *= (1.0 - sd.opacity);
    }

    return V * mi.eval(sd, ls.dir, sg) * ls.Li * invPdf;
}

float traceShadowRay(float3 origin, float3 dir, float distance, inout SampleGenerator sg)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.f;
    ray.TMax = distance;

    ShadowRayData rayData;
    rayData.sg = sg;
    rayData.visible = 1.0;    // accumulated visibility
    TraceRay(gScene.rtAccel, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, kInstanceCameraParticle /* instanceInclusionMask */, 1 /* hitIdx */, rayTypeCount, 1 /* missIdx */, ray, rayData);
    
    sg = rayData.sg;
    return rayData.visible;
}

PathSample handleHit(const HitInfo hit, inout float3 rayOrigin, inout float3 rayDirection, inout float3 thp, inout float3 emission, inout SampleGenerator sg, inout bool terminate)
{
    PathSample output;
    output.radiance = 0.0;
    let lod = ExplicitLodTextureSampler(0.f);

    // Load shading data.
    ShadingData sd = loadShadingData(hit, rayOrigin, rayDirection, lod);
    if(kUseRussianRouletteOpacity) sd.opacity = 1.0;

    // Create material instance.
    let mi = gScene.materials.getMaterialInstance(sd, lod);

    // Add emitted light.
    if (kUseEmissiveLights)
    {
        emission += sd.opacity * thp * mi.getProperties(sd).emission;
    }

    if(terminate) return output;

    if (kUseAnalyticLights)
    {
        float3 Lr = evalDirectAnalytic(sd, mi, sg, output.lightDir, output.distance, output.posW);
        output.radiance += sd.opacity * thp * Lr;
    }

    // Check whether to terminate based on max depth.
    //if (rayData.pathLength >= kMaxBounces)
    //{
    //    rayData.terminated = true;
    //    return;
    //}

    //  generate scatter ray
    float rng1 = sampleNext1D(sg);
    BSDFSample bsdfSample;
    if(rng1 > sd.opacity) // for opacity = 1: always eval bsdf
    {
        rayOrigin = sd.computeNewRayOrigin(false);
    } 
    else if (mi.sample(sd, sg, bsdfSample, true))
    {
        rayOrigin = sd.computeNewRayOrigin(!bsdfSample.isLobe(LobeType::Transmission));
        rayDirection = bsdfSample.wo;
        if(kUseRussianRoulettePath)
        {
            float rng = sampleNext1D(sg);
            if(rng > dot(bsdfSample.weight, 0.33))
               thp = 0.0; // failed roulette
            else thp = (thp * bsdfSample.weight) / dot(bsdfSample.weight, 0.33);
        }
        else thp = thp * bsdfSample.weight;
    }
    else terminate = true;
    if(any(thp <= kMinThroughput)) terminate = true;

    return output;
}


[shader("miss")]
void miss(inout ScatterRayData rayData)
{
    HitInfo hitInfo = { };
    rayData.packedHitInfo = hitInfo.getData(); // Invalid hit
    // Ray missed the scene. Mark the ray as terminated.
    //rayData.terminated = true;

    // Add contribution from distant light (env map) in this direction.
    /*if (kUseEnvLight && rayData.pathLength > 0)
    {
        float3 Le = gScene.envMap.eval(WorldRayDirection());
        rayData.radiance += rayData.thp * Le;
    }*/
}


[shader("anyhit")]
void anyHit(inout ScatterRayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    // Alpha test for non-opaque geometry.
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
    }
}

[shader("closesthit")]
void closestHit(inout ScatterRayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    rayData.packedHitInfo = HitInfo(triangleHit).getData();
}

[shader("anyhit")]
void shadowAnyHit(inout ShadowRayData rayData, BuiltInTriangleIntersectionAttributes attribs)
{
    // Alpha test for non-opaque geometry.
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    const uint materialID = gScene.getMaterialID(instanceID);
    ExplicitLodTextureSampler lod = { 0.0 };

    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v, lod);

    if(kUseRussianRouletteShadow)
    {
        float rng = sampleNext1D(rayData.sg);
        if (rng > alpha) IgnoreHit();
        else
        {
            rayData.visible = 0.0;
            AcceptHitAndEndSearch();
        }
    }

    rayData.visible *= (1.0 - alpha);
    if (rayData.visible > 0.01f) IgnoreHit();
    else AcceptHitAndEndSearch();
}

[shader("miss")]
void shadowMiss(inout ShadowRayData rayData) {}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 frameDim = DispatchRaysDimensions().xy;

    float3 rayOrigin = gScene.camera.getPosition();
    float3 rayDir = gScene.camera.computeRayPinhole(pixel, frameDim).dir;
    float3 thp = 1.0; // path throughput
    float3 emission = 0.0; // emission along the path (does not require shadow rays, since its on the path)
    bool terminate = false;

    PathSample bestSample; // TODO could be extended to a small list (k=2,3,4 etc.)
    bestSample.radiance = 0.0;
    bestSample.weight = 0.0;
    float weightSum = 0.0;

    ScatterRayData rayData;
    rayData.sg = SampleGenerator(pixel, gFrameCount);

    // Follow path into the scene and compute its total contribution.
    for (uint depth = 0; depth <= (kMaxBounces + 1u) && !terminate; depth++)
    {
        HitInfo hit = traceScatterRay(rayData, rayOrigin, rayDir);
        if(!hit.isValid())
        {
            emission += thp * gScene.envMap.eval(rayDir);
            break;
        }

        PathSample ps = handleHit(hit, rayOrigin, rayDir, thp, emission, rayData.sg, terminate);
        ps.weight = dot(ps.radiance, 0.33);
        weightSum += ps.weight;

        // weighted reservoir sampling for best sample
        float rng = sampleNext1D(rayData.sg);
        if(ps.weight > 0.0 && rng <= ps.weight / weightSum)
        {
            bestSample = ps;
        }
    }

    if(bestSample.weight > 0.0)
    {
        // visibility ray
        float V = traceShadowRay(bestSample.posW, bestSample.lightDir, bestSample.distance, rayData.sg);
        emission += V * bestSample.radiance * weightSum / bestSample.weight; // radiance / pdf, pdf = w/wSum
    }

    gColorOut[pixel] = float4(emission, 1.f);
}
