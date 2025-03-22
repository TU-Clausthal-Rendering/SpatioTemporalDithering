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

static float gObjectHash;

float getObjectHash()
{
    //if(gObjectHashType == OBJECT_HASH_QUADS) return hash3D(float3(InstanceID(), GeometryIndex(), PrimitiveIndex() / 2u));
    //return hash3D(float3(InstanceID(), GeometryIndex(), HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE));
    return gObjectHash; // TODO
}

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

bool isAlphaTested(uint materialID)
{
    #if TRANSPARENCY_WHITELIST == 0
    return false;
    #else
    uint isTransparent = (gTransparencyWhitelist[materialID / 32u] & (1u << (materialID % 32u)));
    return isTransparent == 0u;
    #endif
}

VSOut vsMain(VSIn vIn)
{
    return defaultVS(vIn);
}

struct PsOut
{
    PackedHitInfo vbuffer : SV_Target0;
};

PsOut psMain(VSOut v, uint triangleIndex : SV_PrimitiveID, float3 barycentrics : SV_Barycentrics, bool isFrontFace : SV_IsFrontFace)
{
    PsOut o;
    TriangleHit triangleHit;
    triangleHit.instanceID = v.instanceID;
    triangleHit.primitiveIndex = triangleIndex;
    triangleHit.barycentrics = barycentrics.yz;
    o.vbuffer = triangleHit.pack();

    uint materialID = v.materialID;

    #if ALPHA_TEXTURE_LOD != 0
    let lod = ImplicitLodTextureSampler();
    #else
    let lod = ExplicitLodTextureSampler(0.0);
    #endif

    if(isAlphaTested(materialID))
    {
        // use default alpha test if not marked as transparent
        if (alphaTest(v, triangleIndex, lod)) discard;
        return o;
    }

    let material = gScene.materials.getMaterial(materialID);
    float alpha = material.evalOpacity(gScene.materials, v.texC, lod);
    if(alpha <= 0.01) discard; // no hit
    if(alpha >= 1.0) return o; // opaque

    #if COVERAGE_CORRECTION == COVERAGE_CORRECTION_DLSS
    float dlssAlpha = saturate(0.0113 + 1.6560 * alpha - 0.8210 * alpha * alpha + 0.1560 * alpha * alpha * alpha);
    alpha = lerp(alpha, dlssAlpha, gDLSSCorrectionStrength);
    #endif

    DitherInfo d = {};
    d.pixel = int2(v.posH.xy);
    d.frameIndex = gFrameCount;
    d.posW = v.posW;
    d.alpha = alpha;
    #if (ENABLE_HASH_GRIDS != 0)
    uint2 texDim = material.getDimensions(gScene.materials);
    float2 texAspect = float2(1.0, float(texDim.y) / float(texDim.x));
    d.posUV = v.texC * texAspect;
    d.maxDerivUV = max(length(ddx(v.texC) * texAspect), length(ddy(v.texC) * texAspect));
    #endif // ENABLE_HASH_GRIDS

    // object hash
    if(gObjectHashType == OBJECT_HASH_QUADS) gObjectHash = hash(float2(v.instanceID.index, triangleIndex / 2u));
    else gObjectHash = hash(float2(v.instanceID.index, isFrontFace ? 1 : 0));


    #if DITHER_MODE == DITHER_MODE_DISABLED

    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_2X2)
    if (alpha < getPixelDitherThreshold2x2(d))
            discard;
    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_3X3)
    if (alpha < getPixelDitherThreshold3x3(d))
        discard;
    #endif
    #if (DITHER_MODE == DITHER_MODE_PER_PIXEL_4X4)
    if (alpha < getPixelDitherThreshold4x4(d))
            discard;
    #endif
    #if (DITHER_MODE == DITHER_MODE_DITHER_TEMPORAL_AA)
    if (alpha < ditherTemporalAAThreshold(d))
        discard;
    #endif
    #if DITHER_MODE == DITHER_MODE_RUSSIAN_ROULETTE
    if(!winRoulette(d))
        discard;
    #endif
    #if DITHER_MODE == DITHER_MODE_PERIODIC
    if(!winRotation(d))
        discard;
    #endif
    #if DITHER_MODE == DITHER_MODE_HASH_GRID
    if(!winHashGrid2D(d))
        discard;
    #endif
    #if DITHER_MODE == DITHER_MODE_FRACTAL_DITHERING
    if (!winFractalDither(d, ddx * texAspect, ddy * texAspect, gScene.camera.data.projMat))
        discard;
    #endif
    #if DITHER_MODE == DITHER_MODE_BLUE_NOISE3D
    if (!winBlueNoise3D(d))
        discard;
    #endif

    return o;
}
