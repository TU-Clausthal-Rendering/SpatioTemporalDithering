#include "Utils/Math/MathConstants.slangh"

RWTexture2D<float4> gColor;
Texture2D<uint> gHead;

struct BufferData // 4 * 4 byte 
{
    uint next;
    float depth;
    //half4 color;
    uint2 color;
    //float2 color;
};

float2 unpackHalf(uint c)
{
    return float2(f16tof32(c), f16tof32(c >> 16));
}

float4 unpackHalf4(uint2 c)
{
    return float4(unpackHalf(c.r), unpackHalf(c.g));
}

struct FragData
{
    // half4 color;
    uint2 color;
    float depth;
};

StructuredBuffer<BufferData> gBuffer;

cbuffer PerFrame
{
    uint2 gFrameDim;
    uint maxElements;
}

#ifndef MAX_ELEMENT
#define MAX_ELEMENT 64
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    uint2 pixel = id.xy;
    if (pixel.x >= gFrameDim.x || pixel.y >= gFrameDim.y)
        return;

    uint head = gHead[pixel];
    if (head >= maxElements)
        return;

    float4 color = float4(0, 0, 0, 1);

    FragData a[MAX_ELEMENT];
    // clear (optional?)
    uint i;
    [unroll] for (i = 0; i < MAX_ELEMENT; ++i)
    {
        a[i].color = 0u;
        a[i].depth = FLT_MAX;
    }

    // insert from linked list
    uint next = head;
    uint count = 0;
    [unroll] for (i = 0; i < MAX_ELEMENT && next < maxElements; ++i)
    {
        BufferData data = gBuffer[next];
        next = data.next;
        a[i].depth = data.depth;
        a[i].color = data.color;
        count++;
    }

    // sort
    for (i = 1; i < MAX_ELEMENT && i < count; ++i)
    {
        for (int j = i; j > 0 && a[j - 1].depth > a[j].depth; --j) {
            let tmp = a[j];
            a[j] = a[j - 1];
            a[j - 1] = tmp;
        }
    }

    // blend
    [unroll] for (i = 0; i < MAX_ELEMENT && i < count; ++i)
    {
        float4 fcolor = unpackHalf4(a[i].color);
        color.rgb += color.a * fcolor.a * fcolor.rgb;
        color.a *= (1.0 - fcolor.a);
    }

    gColor[pixel] = color;
}
