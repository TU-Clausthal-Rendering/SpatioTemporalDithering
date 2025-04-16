#include "Scene/SceneDefines.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Raster;
import Utils.Math.MathHelpers;
import Utils.Sampling.SampleGenerator;
import Utils.Math.PackedFormats;
import Utils.Math.FormatConversion;


#ifndef ALPHA_TEXTURE_LOD
#define ALPHA_TEXTURE_LOD 0
#endif

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

#ifdef OPAQUE_SHADER
    return o;
#else
    #if ALPHA_TEXTURE_LOD != 0
    let lod = ImplicitLodTextureSampler();
    #else
    let lod = ExplicitLodTextureSampler(0.0);
    #endif
    
    if (alphaTest(v, triangleIndex, lod))
        discard;
    return o;
#endif
}
