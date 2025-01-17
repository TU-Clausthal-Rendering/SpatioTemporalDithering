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
import Scene.RaytracingInline;
import Scene.Intersection;
import Utils.Math.PackedFormats;
#include "Scene/SceneDefines.slangh"


cbuffer PerFrameCB
{
    float gAlpha;
    float gColorBoxSigma;
    bool gAntiFlicker;

    bool gUseMaxMotionVector;
    bool gUseColorVariance; // otherwise bounding box
    bool gBicubicColorFetch; // otherwise bilinear
    bool gUseClipping; // otherwise clamping
    bool gRectifyColor; // otherwise no rectification
    bool gRejectOccluded;
    bool gRejectMotion; // reject on strong motion vector change
    bool gRejectVBufferMotion; // rejects based on motion of the vbuffer: if the reprojected vbuffer hit is in the current pixel, it can be used, otherwise reject.
    float gPrevFrameDelta; // time deltas for motion vector normalization
    float gCurFrameDelta;
};

Texture2D gTexColor;
Texture2D gTexMotionVec;
Texture2D gTexPrevMotionVec;
Texture2D gTexPrevColor;
SamplerState gSampler;

Texture2D<float> gTexLinearZ;
Texture2D<float> gTexPrevLinearZPixel; // the previous linearZ from the current pixel (do not use motion vectors for this)
Texture2D<float> gTexPrevLinearZ; // copy of linearZ from the previous frame (needs motion vectors for lookup)

Texture2D<uint4> gTexVBuffer; // packed vbuffer
Texture2D<uint4> gTexPrevVBuffer; // packed vbuffer from previous frame

float2 getGatherUV(float2 uv)
{
    uint2 resolution;
    uint levels;
    gTexColor.GetDimensions(0, resolution.x, resolution.y, levels);
    return (floor(uv * float2(resolution) - 0.5) + 1.0) / resolution; // snap to center between 4 texels
}

bool previousOccluded(int2 pixelPos, float2 prevUV)
{
    float expectedPrevDepth = gTexPrevLinearZPixel[pixelPos];
    if(expectedPrevDepth <= 0.0) return true; // previous pixel was behind camera (TODO maybe replace with near plane?)

    // 4x neighborhood
    float4 actualPrevDepthGather = gTexPrevLinearZ.Gather(gSampler, getGatherUV(prevUV));
    for (uint i = 0; i < 4; ++i)
    {
        // float actualPrevDepth = gTexPrevLinearZ.SampleLevel(gSampler, prevUV, 0);
        float actualPrevDepth = actualPrevDepthGather[i];
        float relativeDiff = actualPrevDepth / expectedPrevDepth;
        if (relativeDiff < 0.995) return true; // previous pixel was occluded
        //if (relativeDiff < 0.999) return true; // previous pixel was occluded
    }

    // 9x neighborhood


    return false; 
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

// easy: always clips to center
float2 clipEllipse(float2 center, float2 radius, float2 value)
{
    radius = max(radius, 1e-6); // Avoid division by zero

    // Transform to the space where the ellipse is a unit sphere
    let relValue = (value - center) / radius;

    // Calculate the squared distance of the normalized value from the origin
    float distanceSquared = dot(relValue, relValue);

    // If the point is inside the ellipse, return the original value
    if (distanceSquared <= 1.0) {
        return value;
    }

    // Compute the scaling factor to project the point onto the ellipse
    // Scaling factor is r / distance, with r = 1 (unit sphere)
    float scale = rsqrt(distanceSquared);
    return lerp(center, value, scale);
}

// hard: clips to line segment
float2 clipEllipse(float2 center, float2 radius, float2 lineStart, float2 lineEnd)
{
    // Early exit if lineStart is already inside the ellipse
    float2 relStart = lineStart - center;
    float2 normalizedStart = relStart / radius;
    float startDistanceSquared = dot(normalizedStart, normalizedStart);
    if (startDistanceSquared <= 1.0)
    {
        return lineStart;
    }

    // Compute the direction vector and normalized coefficients
    float2 dir = lineEnd - lineStart;
    float2 normalizedDir = dir / radius;

    // Coefficients for the quadratic equation: at^2 + bt + c = 0
    float a = dot(normalizedDir, normalizedDir);
    float b = 2.0 * dot(normalizedStart, normalizedDir);
    float c = dot(normalizedStart, normalizedStart) - 1.0;

    // Solve the quadratic equation
    float discriminant = b * b - 4.0 * a * c;

    float t = 0.0;
    if (discriminant >= 0.0)
    {
        // Compute the two intersection points
        float sqrtDiscriminant = sqrt(discriminant);
        float t1 = (-b - sqrtDiscriminant) / (2.0 * a);
        float t2 = (-b + sqrtDiscriminant) / (2.0 * a);

        // Choose the smallest t (we know that lineStart is not inside)
        t = min(t1, t2);
    }
    else
    {
        // No intersection, find the closest point on the line
        t = dot(-relStart, dir) / dot(dir, dir);
    }

    // Interpolate to find the point
    return lerp(lineStart, lineEnd, saturate(t));
}

float2 clipBox(float2 minv, float2 maxv, float2 lineStart, float2 lineEnd)
{
    // Check if lineStart is already inside the bounding box
    if (lineStart.x >= minv.x && lineStart.x <= maxv.x &&
        lineStart.y >= minv.y && lineStart.y <= maxv.y)
    {
        return lineStart;
    }

    // Direction vector of the line
    float2 dir = lineEnd - lineStart;

    // Parameters for the parametric equation
    float tmin = 0.0;

    // Clip against the left edge (we know that lineEnd is inside the box, so division by zero will never occur)
    if (lineStart.x < minv.x)
        tmin = max(tmin, (minv.x - lineStart.x) / dir.x);

    // Clip against the right edge
    if (lineStart.x > maxv.x)
        tmin = max(tmin, (maxv.x - lineStart.x) / dir.x);

    // Clip against the bottom edge
    if (lineStart.y < minv.y)
        tmin = max(tmin, (minv.y - lineStart.y) / dir.y);

    // Clip against the top edge
    if (lineStart.y > maxv.y)
        tmin = max(tmin, (maxv.y - lineStart.y) / dir.y);

    // Return the clipped point
    // return lineStart + tmin * dir;
    return lerp(lineStart, lineEnd, tmin);
}

struct PSOut
{
    float4 color : SV_TARGET0;
    float motionMask : SV_TARGET1;
};

PSOut main(float2 texC : TEXCOORD) : SV_TARGET0
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
    gTexColor.GetDimensions(0, texDim.x, texDim.y, levels);

    float2 pos = texC * texDim;
    int2 ipos = int2(pos);

    // Fetch the current pixel color and compute the color bounding box
    // Details here: http://www.gdcvault.com/play/1023521/From-the-Lab-Bench-Real
    // and here: http://cwyman.org/papers/siga16_gazeTrackedFoveatedRendering.pdf
    float3 color = gTexColor.Load(int3(ipos, 0)).rgb;
    color = RGBToYCgCo(color);
    float3 colorAvg = color;
    float3 colorVar = color * color;
    float3 colorMin = color;
    float3 colorMax = color;
    [unroll]
    for (int k = 0; k < 8; k++)
    {
        float3 c = gTexColor.Load(int3(ipos + offset[k], 0)).rgb;
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
    if (gUseColorVariance)
    {
        // set color min and max based on variance
        colorMin = colorAvg - gColorBoxSigma * sigma;
        colorMax = colorAvg + gColorBoxSigma * sigma;
    }
    

    float2 motion = gTexMotionVec.Load(int3(ipos, 0)).xy;

    if(gUseMaxMotionVector)
    {
        // Find the longest motion vector
        [unroll]
        for (int a = 0; a < 8; a++)
        {
            float2 m = gTexMotionVec.Load(int3(ipos + offset[a], 0)).rg;
            motion = dot(m, m) > dot(motion, motion) ? m : motion;
        }
    }
    float2 prevMotion = gTexPrevMotionVec.SampleLevel(gSampler, texC + motion, 0).xy;
    {
        int2 iposprev = int2((texC + motion) * texDim);
        prevMotion = gTexPrevMotionVec[iposprev].xy;
        for (int i = 0; i < 8; ++i)
        {
            float2 m = gTexPrevMotionVec[iposprev + offset[i]].xy;
            // take previous motion with largest difference to current motion
            prevMotion = length(motion - m) > length(motion - prevMotion) ? m : prevMotion;
        }
    }

    // Use motion vector to fetch previous frame color (history)
    float3 history;
    if(gBicubicColorFetch)
        history = bicubicSampleCatmullRom(gTexPrevColor, gSampler, (texC + motion) * texDim, texDim);
    else
        history = gTexPrevColor.SampleLevel(gSampler, texC + motion, 0).rgb;

    history = RGBToYCgCo(history);

    float alpha = gAlpha;

    // Anti-flickering, based on Brian Karis talk @Siggraph 2014
    // https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf
    // Reduce blend factor when history is near clamping
    if (gAntiFlicker)
    {
        float distToClamp = min(abs(colorMin.x - history.x), abs(colorMax.x - history.x));
        alpha = clamp((gAlpha * distToClamp) / (distToClamp + colorMax.x - colorMin.x), 0.f, 1.f);
    }

    float3 originalHistory = history;
    if (gRectifyColor)
    {
        if (gUseClipping)
        {
            history.x = clamp(history.x, colorMin.x, colorMax.x); // clamp luma
            if (gUseColorVariance)
                // history.yz = clipEllipse(colorAvg.yz, gColorBoxSigma * sigma.yz, history.yz); // clip chroma
                history.yz = clipEllipse(colorAvg.yz, gColorBoxSigma * sigma.yz, history.yz, color.yz); // clip chroma
            else
                history.yz = clipBox(colorMin.yz, colorMax.yz, history.yz, color.yz); // clip chroma
        }
        else history = clamp(history, colorMin, colorMax); // clamping
    }

    if (gRejectOccluded) if (previousOccluded(ipos, texC + motion))
    {
        // reset color and history
        history = color;
    }

    // reset if reprojection was out of bounds
    if (any(texC + motion != saturate(texC + motion)))
    {
        history = color;
        alpha = 1.0;
    }
    else if (gRejectVBufferMotion)
    {
        int2 iposprev = int2((texC + motion) * texDim);
        HitInfo hitInfo = HitInfo(gTexPrevVBuffer[iposprev]);
        if (hitInfo.getType() == HitType::Triangle)
        {
            VertexData v = gScene.getVertexData(hitInfo.getTriangleHit());
            float3 posW = v.posW; // check if posW projects to the current pixel
            float4 posH = mul(gScene.camera.data.viewProjMatNoJitter, float4(posW, 1.0));
            float2 posUV = posH.xy / posH.w * float2(0.5, -0.5) + 0.5;
            float beta = saturate((length((texC - posUV) * float2(texDim)) - 0.8) * 10.0);
            /*for (int i = 0; i < 8; ++i)
            {
                hitInfo = HitInfo(gTexPrevVBuffer[iposprev + offset[i]]);
                if (hitInfo.getType() == HitType::Triangle)
                {
                    v = gScene.getVertexData(hitInfo.getTriangleHit());
                    posW = v.posW;
                    posH = mul(gScene.camera.data.viewProjMatNoJitter, float4(posW, 1.0));
                    posUV = posH.xy / posH.w * float2(0.5, -0.5) + 0.5;
                    beta = max(beta, saturate((length((texC - posUV) * float2(texDim)) - 1.8) * 10.0));
                }
            }*/
            alpha = lerp(alpha, 1.0, beta);
        }
        else alpha = 1.0; // reset on invalid hit info

        /*for (int i = 0; i < 8; ++i)
        {
            float2 m = gTexPrevMotionVec[iposprev + offset[i]].xy;
            // take previous motion with largest difference to current motion
            prevMotion = length(motion - m) > length(motion - prevMotion) ? m : prevMotion;
        }*/
    }

    PSOut output;
    // motion = motion / gCurFrameDelta;
    // prevMotion = prevMotion / gPrevFrameDelta;
    output.motionMask = length(motion - prevMotion) / max(max(length(motion), length(prevMotion)), 0.001);
    //output.motionMask = length(motion - prevMotion) * 30.0;
   // output.motionMask = sqrt(abs(dot(motion, motion) - dot(prevMotion, prevMotion))) * 100.0;
    // increase alpha based on motion
    if (gRejectMotion) alpha = lerp(alpha, 1.0, saturate(output.motionMask / 1.0));
    output.motionMask = alpha;

    float3 result = YCgCoToRGB(lerp(history, color, alpha));
    output.color = float4(result, 1.f);

    // output.motionMask = length(motion - prevMotion) / (length(motion) + 0.1);
    // normalize motion vector by time delta


    //output.motionMask = abs(length(motion) - length(prevMotion)) / max(min(length(motion), length(prevMotion)), 0.01);

    // output.motionMask = output.motionMask > 0.5 ? 1.0 : 0.0;
    return output;
}
