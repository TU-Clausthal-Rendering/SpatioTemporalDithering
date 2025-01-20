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
    float gColorBoxSigma;
};

Texture2D gCurColor;
Texture2D gPrevColor;
Texture2D gMotionVec;
SamplerState gSampler;

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    const int2 offset[9] =
    {
        int2(-1, -1), int2(-1, 1),
        int2(1, -1), int2(1, 1),
        int2(1, 0), int2(0, -1),
        int2(0, 1), int2(-1, 0),
        int2(0, 0) // center for convenience
    };

    uint2 texDim;
    uint levels;
    gCurColor.GetDimensions(0, texDim.x, texDim.y, levels);

    float2 pos = texC * texDim;
    int2 ipos = int2(pos);

    float3 color = gCurColor.Load(int3(ipos, 0)).rgb;
    color = RGBToYCgCo(color);
    float3 colorAvg = color;
    float3 colorVar = color * color;
    float3 colorMin = color;
    float3 colorMax = color;
    [unroll]
    for (int k = 0; k < 8; k++)
    {
        float3 c = gCurColor.Load(int3(ipos + offset[k], 0)).rgb;
        c = RGBToYCgCo(c);
        colorAvg += c;
        colorVar += c * c;
        colorMin = min(colorMin, c);
        colorMax = max(colorMax, c);
    }

    float oneOverNine = 1.f / 9.f;
    colorAvg *= oneOverNine;
    colorVar *= oneOverNine;

    float3 sigma = sqrt(max(0.f, colorVar - colorAvg * colorAvg));
    //if (gUseColorVariance)
    {
        // set color min and max based on variance
        colorMin = colorAvg - gColorBoxSigma * sigma;
        colorMax = colorAvg + gColorBoxSigma * sigma;
    }
    

    float2 motion = gMotionVec.Load(int3(ipos, 0)).xy;

    /*if(gUseMaxMotionVector)
    {
        // Find the longest motion vector
        [unroll]
        for (int a = 0; a < 8; a++)
        {
            float2 m = gTexMotionVec.Load(int3(ipos + offset[a], 0)).rg;
            motion = dot(m, m) > dot(motion, motion) ? m : motion;
        }
    }*/

    // Use motion vector to fetch previous frame color (history)
    float3 history = gPrevColor.SampleLevel(gSampler, texC + motion, 0).rgb;
    history = RGBToYCgCo(history);

    float alpha = 0.5; // same weight for previous and current
    
    if (any(texC + motion != saturate(texC + motion)))
    {
        alpha = 1.0; // use current frame if reprojection is out of bounds
    }

    if(gColorBoxSigma < 499.0 && any(clamp(history, colorMin, colorMax) != history))
    {
        alpha = 1.0; // use current frame if color is not accepted
    }

    return float4(YCgCoToRGB(lerp(history, color, alpha)), 1.0);
}
