/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
import Utils.Color.ColorHelpers;

cbuffer PerFrameCB
{
    float gAlpha;
    float gColorBoxSigma;
    int gUseBoundingBoxForTransparency;
    int gTransparencyRadius;
};

Texture2D<float4> gTexColor;
Texture2D<float4> gTexPrevColor;
Texture2D<float2> gTexMotionVec;
Texture2D<float> gTexOpacity;
SamplerState gSampler;

float2 getGatherUV(float2 uv)
{
    uint2 resolution;
    uint levels;
    gTexColor.GetDimensions(0, resolution.x, resolution.y, levels);
    return (floor(uv * float2(resolution) - 0.5) + 1.0) / resolution; // snap to center between 4 texels
}

// Catmull-Rom filtering code from http://vec3.ca/bicubic-filtering-in-fewer-taps/
float3 bicubicSampleCatmullRom(Texture2D tex, SamplerState samp, float2 samplePos, float2 texDim)
{
    float2 invTextureSize = 1.0 / texDim;
    float2 tc = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - tc;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.f;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1 - w0 - w1 - w3;

    float2 w12 = w1 + w2;

    float2 tc0 = (tc - 1.f) * invTextureSize;
    float2 tc12 = (tc + w2 / w12) * invTextureSize;
    float2 tc3 = (tc + 2.f) * invTextureSize;

    float3 result =
        tex.SampleLevel(samp, float2(tc0.x, tc0.y), 0.f).rgb * (w0.x * w0.y) +
        tex.SampleLevel(samp, float2(tc0.x, tc12.y), 0.f).rgb * (w0.x * w12.y) +
        tex.SampleLevel(samp, float2(tc0.x, tc3.y), 0.f).rgb * (w0.x * w3.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0.f).rgb * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0.f).rgb * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0.f).rgb * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc0.y), 0.f).rgb * (w3.x * w0.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc12.y), 0.f).rgb * (w3.x * w12.y) +
        tex.SampleLevel(samp, float2(tc3.x, tc3.y), 0.f).rgb * (w3.x * w3.y);

    return result;
}

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    uint2 texDim;
    uint levels;
    gTexColor.GetDimensions(0, texDim.x, texDim.y, levels);

    float2 pos = texC * texDim;
    int2 ipos = int2(pos);

    // Fetch the current pixel color and compute the color bounding box
    // Details here: http://www.gdcvault.com/play/1023521/From-the-Lab-Bench-Real
    // and here: http://cwyman.org/papers/siga16_gazeTrackedFoveatedRendering.pdf
    float3 color = gTexColor.Load(int3(ipos, 0)).rgb;
    bool hasTransparency = (gTexOpacity.Load(int3(ipos, 0)).r > 0.0);
    //opacity = 0.5;
    color = RGBToYCgCo(color);

    float3 colorAvg = color;
    float3 colorVar = color * color;
    float3 colorMin = color;
    float3 colorMax = color;
    int statRadius = 1;
    if (hasTransparency) statRadius = gTransparencyRadius;

    for(int rx = -statRadius; rx <= statRadius; ++rx) for(int ry = -statRadius; ry <= statRadius; ++ry)
    {
        if (rx == 0 && ry == 0) continue; // skip this
        float3 c = gTexColor.Load(int3(ipos + int2(rx, ry), 0)).rgb;
        c = RGBToYCgCo(c);
        colorAvg += c;
        colorVar += c * c;
        colorMin = min(colorMin, c);
        colorMax = max(colorMax, c);
    }

    float oneOverNine = 1.f / float((2 * statRadius + 1) * (2 * statRadius + 1));
    colorAvg *= oneOverNine;
    colorVar *= oneOverNine;

    float3 sigma = sqrt(max(0.f, colorVar - colorAvg * colorAvg));
    if (!hasTransparency || (gUseBoundingBoxForTransparency == 0)) // only use variance if we there is no transparency involved
    {
        // set color min and max based on variance
        colorMin = colorAvg - gColorBoxSigma * sigma;
        colorMax = colorAvg + gColorBoxSigma * sigma;
    } // else colorMin and colorMax will be the color bounding box
    
    float2 motion = gTexMotionVec.Load(int3(ipos, 0)).xy;

    // Use motion vector to fetch previous frame color (history)
    float3 history = bicubicSampleCatmullRom(gTexPrevColor, gSampler, (texC + motion) * texDim, texDim);
    //float3 history = gTexPrevColor.SampleLevel(gSampler, texC + motion, 0).rgb;
    history = RGBToYCgCo(history);

    float alpha = gAlpha;
    // rectify history
    history = clamp(history, colorMin, colorMax);

    // reset if reprojection was out of bounds
    if (any(texC + motion != saturate(texC + motion)))
        alpha = 1.0;

    float3 result = YCgCoToRGB(lerp(history, color, alpha));
    //if(opacity < 1.0) result.gb = 0.0;
    return float4(result, 1.0);
}
