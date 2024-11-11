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
import Rendering.Materials.Fresnel;
import Rendering.Lights.LightHelpers;
import Utils.Sampling.TinyUniformSampleGenerator;

cbuffer PerFrame
{
    uint gFrameCount;       // Frame count since scene was loaded.
    float gAmbientIntensity;
    float gEnvMapIntensity;
    float gLightIntensity;
    uint gShadowRay; // bool
    float gVisibilityThreshold;
    uint gResMultiplier;
    uint2 gFullResolution;
}

RWTexture2D<float4> gColor;
RWTexture2D<float> gRayT;
Texture2D<float4> gColorPrev;
Texture2D<float> gRayTPrev;

SamplerState gSampler;

struct TraceData
{
    PackedHitInfo packedHitInfo;
    float distance; // RayT
    
    static TraceData create()
    {
        TraceData d;
        d.packedHitInfo = {};
        d.distance = 0.f;
        return d;
    }
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
void miss(inout TraceData rayData : SV_RayPayload)
{
    HitInfo hitInfo = { };
    rayData.packedHitInfo = hitInfo.getData(); // Invalid hit
    rayData.distance = FLT_MAX;
}


[shader("anyhit")] // this is skipped for now (faster without)
void anyHit(inout TraceData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // Alpha test for non-opaque geometry.
    GeometryInstanceID instanceID = getGeometryInstanceID();
    VertexData v = getVertexData(instanceID, PrimitiveIndex(), attribs);
    uint materialID = gScene.getMaterialID(instanceID);
    
    let lod = ExplicitLodTextureSampler(0.0);
    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v, lod);
    if(alpha <= 0.01)
    IgnoreHit();
}

[shader("closesthit")]
void closestHit(inout TraceData rayData : SV_RayPayload, BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes){
    TriangleHit triangleHit;
    triangleHit.instanceID = getGeometryInstanceID();
    triangleHit.primitiveIndex = PrimitiveIndex();
    triangleHit.barycentrics = attribs.barycentrics;
    
    rayData.distance = RayTCurrent();
    rayData.packedHitInfo = HitInfo(triangleHit).getData();
}

float roughnessToLod(float roughness)
{
    float nEnvLods = log2(float(gScene.envMap.getDimensions().x));
    float maxEnvLod = max(nEnvLods - 4.0, 0.0);
    return saturate(roughness) * maxEnvLod;
}

// limit maximal intensity of rough materials (avoids bright sparkles)
float roughnessToIntensity(float roughness)
{
    return saturate(1.0 / (roughness + 0.1));
}

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

#define USE_SHADOWS 1
#define SHADOW_MIN 0.01
float TraceShadowRay(float3 origin, LightData l)
{
    if(!USE_SHADOWS) return 1.0;

    // set up shadow ray
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = 0.01;
    ray.TMax = FLT_MAX;
    switch(l.type)
    {
    case uint(LightType::Point): {
        float3 dir = l.posW - origin;
        ray.TMax = length(dir);
        ray.Direction = dir / ray.TMax;
        } break;
    case uint(LightType::Directional):
        ray.Direction = -l.dirW;
        break;
    default: return 1.0; // not supported
    }

    // prepare ray query (do all computations in the "any-hit shader")
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_NON_OPAQUE |  RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(gScene.rtAccel, RAY_FLAG_NONE, 0xff /* instanceInclusionMask */, ray);
    float transmit = 1.0;

    while(q.Proceed())
    {
        if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) // triangle
        {
            TriangleHit triangleHit;
            triangleHit.instanceID = GeometryInstanceID(q.CandidateInstanceID(), q.CandidateGeometryIndex());
            triangleHit.primitiveIndex = q.CandidatePrimitiveIndex();
            triangleHit.barycentrics = q.CandidateTriangleBarycentrics();
            
            VertexData v = gScene.getVertexData(triangleHit);
            const uint materialID = gScene.getMaterialID(triangleHit.instanceID);
            let lod = ExplicitLodTextureSampler(0.0);
            ShadingData sd = gScene.materials.prepareShadingData(v, materialID, -ray.Direction, lod);
            transmit *= 1.0 - sd.opacity;
        }

        if(transmit <= SHADOW_MIN)
        {
            q.Abort();
            break;
        }
    }

    return max(transmit, SHADOW_MIN);
}

float4 shadeFragment(const HitInfo hit, float3 viewDir, uint2 pixel, uint2 frameDim)
{
    if (hit.isValid()) // valid hit
    {
        // Load shading data.
        VertexData v = { };
        uint materialID = { };
        if (hit.getType() != HitType::Triangle)
            return 0.0;
        
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);

        // ray differentials: (Implicit does not work with vbuffer, because of different triangles per pixels)
        float2 ddx, ddy;
        float hitT = distance(v.posW, gScene.camera.data.posW);
        computeRayDifferentials(triangleHit, -viewDir, hitT, gScene.camera, float2(frameDim), ddx, ddy);
        let lod = ExplicitGradientTextureSampler(ddx, ddy);
        //let lod = ImplicitLodTextureSampler();
        //let lod = ExplicitLodTextureSampler(1000.0);
        
        
        ShadingData sd = gScene.materials.prepareShadingData(v, materialID, viewDir, lod);
        //if(sd.opacity < 0.01) return 0.0;
        // Create material instance at shading point.
        let mi = gScene.materials.getMaterialInstance(sd, lod, (uint) MaterialInstanceHints::AdjustShadingNormal);
    
        // Create material instance.
        let bsdf = mi.getProperties(sd);
    
        // emission
        float3 color = bsdf.emission;
        // constant ambient term
        color += gAmbientIntensity * bsdf.diffuseReflectionAlbedo;
        // envmap ambient term
        float3 R = reflect(-viewDir, bsdf.guideNormal);
        float diffLod = roughnessToLod(bsdf.roughness * 2.0);
        float specLod = roughnessToLod(bsdf.roughness);
        float envMaxIntensity = roughnessToIntensity(bsdf.roughness);
        float F = evalFresnelSchlick(0.3, 1.0, max(0.0, dot(viewDir, bsdf.guideNormal)));
        color += gEnvMapIntensity * bsdf.diffuseReflectionAlbedo * min(gScene.envMap.eval(lerp(sd.frame.N, sd.faceN, 0.8), diffLod), 1.0);
        color +=  gEnvMapIntensity * bsdf.specularReflectionAlbedo * min(gScene.envMap.eval(R, specLod), envMaxIntensity);

        // perfect mirror
        if(bsdf.roughness < 0.08)
            color += F * gScene.envMap.eval(R, specLod);
        
        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(pixel, 0);

        // Direct lighting from analytic light sources
        for (int i = 0; i < gScene.getLightCount(); i++)
        {
            AnalyticLightSample ls;
            evalLightApproximate(sd.posW, gScene.getLight(i), ls);
            float3 unshadowed = mi.eval(sd, ls.dir, sg) * ls.Li * gLightIntensity;
            float shadow = 1.0;
            if(any(unshadowed > 0.0) && gShadowRay)
            {
                shadow = TraceShadowRay(sd.posW, gScene.getLight(i));
            }
        
            color += shadow * unshadowed;
        
        }

        return float4(color, sd.opacity);
    }
    else // invalid hit => envmap
    {
        // get camera ray from pixel
        return float4(gScene.envMap.eval(-viewDir).rgb * gEnvMapIntensity, 1.0); // no visibility
    }
}

[shader("raygeneration")]
void rayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    const uint2 id = pixel;
    uint2 frameDim = DispatchRaysDimensions().xy;

    float tMin = 0.0;
    float4 color = float4(0, 0, 0, 1); // RGB + visibility
    float prevVisibility = 1.0;
    [branch] if(gResMultiplier != 1)
    {
        // fetch the 4 pixels from the previous level
        float2 uv = (pixel + 0.5) / frameDim;
        // (-,+),(+,+),(+,-),(-,-)
        float4 prevT = gRayTPrev.GatherRed(gSampler, uv);
        float4 prevVis = gColorPrev.GatherAlpha(gSampler, uv);

        const uint2 offsets[4] = { uint2(0, 1), uint2(1, 1), uint2(1, 0), uint2(0, 0) };

        // choose one of the 4 pixels
        tMin = prevT[0];
        prevVisibility = prevVis[0];
        pixel = id * gResMultiplier + offsets[0];

        [unroll]for (uint i = 0; i < 4; ++i)
        {
            //if (prevVis[i] > prevVisibility)
            if(prevT[i] < tMin)
            //if(i == (id.x % 2u) + 2u * (id.y % 2u))
            //if(i == 3)
            {
                tMin = prevT[i];
                prevVisibility = prevVis[i];
                pixel = id * gResMultiplier + offsets[i];
            }
        }

        frameDim = gFullResolution;
        if (prevVisibility <= 0.01)
        {
            // finish
            gColor[id] = 0.0;
            gRayT[id] = tMin;
            return;
        }
    }

    Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);
    ray.tMin = tMin; // start from previous hitpoint
    SampleGenerator sg = SampleGenerator(pixel, gFrameCount);

    TraceData data = TraceData::create();
    HitInfo hitInfo;


    for (uint i = 0; i < 64; ++i)
    {

        TraceRay(gScene.rtAccel, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, rayTypeCount, 0, ray.toRayDesc(), data);
        ray.tMin = data.distance; // update for next ray

        // for now take and write this hitpoint
        hitInfo = HitInfo(data.packedHitInfo);
        float4 fcolor = shadeFragment(hitInfo, -ray.dir, pixel, frameDim);

        color.rgb += color.a * fcolor.a * fcolor.rgb;
        color.a *= 1.0 - fcolor.a;

        if (prevVisibility * color.a < gVisibilityThreshold)
            break;
    }
    
    gColor[id] = color;
    gRayT[id] = ray.tMin;
}
