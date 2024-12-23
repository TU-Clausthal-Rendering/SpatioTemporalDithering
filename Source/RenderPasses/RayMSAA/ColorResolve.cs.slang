Texture2D<float4> gColorIn;
RWTexture2D<float4> gColorOut;

Texture2D<float> gDepthTex;
Texture2D<uint> gEdgeTex;
Texture2DArray<float> gDepthSamples;

SamplerState gSampler;

cbuffer CB
{
    float2 gStep; // = 1 / resolution
    float2 gResolution;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    float2 uv = (id.xy + 0.5) / gResolution;
    //float depth = gDepthTex.SampleLevel(gSampler, uv, 0).r;
    float4 color = gColorIn.SampleLevel(gSampler, uv, 0);

    uint edge = gEdgeTex[id.xy];
    if(!edge)
    {
        gColorOut[id.xy] = color;
        return;
    }

    float2 offsets[9] = {
        float2(-1, -1), float2(0, -1), float2(1, -1),
        float2(-1,  0), float2(0,  0), float2(1,  0),
        float2(-1,  1), float2(0,  1), float2(1,  1)
    };

    float3 colorSum = color.rgb;

    float localDepths[9];
    //float msaaDepths[8];
    float3 colors[9];

    // fetch local depth values
    [unroll] for(uint i = 0; i < 9; ++i)
    {
        localDepths[i] = gDepthTex.SampleLevel(gSampler, uv + offsets[i] * gStep, 0);
    }
    [unroll] for(uint i = 0; i < 9; ++i)
    {
        colors[i] = gColorIn.SampleLevel(gSampler, uv + offsets[i] * gStep, 0).rgb;
    }
    [unroll] for(uint i = 0; i < 8; ++i)
    {
        //msaaDepths[i] = gDepthSamples.Sample(gSampler, float3(uv, i), 0);
        //float msDepth = gDepthSamples.Sample(gSampler, float3(uv, i), 0);
        float msDepth = gDepthSamples[int3(id.xy, i)];
        // find best fit for ms depth in the local depths
        float minDiff = abs(localDepths[4] - msDepth);
        float3 bestColor = colors[4];
        [unroll] for(uint j = 0; j < 9; ++j)
        {
            float diff = abs(localDepths[j] - msDepth);
            if(diff < minDiff)
            {
                minDiff = diff;
                bestColor = colors[j];
            }
        }
        // best color is best fit
        colorSum += bestColor;
    }

    // normalize color
    colorSum /= 9.0f;
    gColorOut[id.xy] = float4(colorSum, 1.0);
}
