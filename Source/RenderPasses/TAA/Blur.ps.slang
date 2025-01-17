cbuffer PerFrameCB
{
    float2 dir;
    int radius;
};

Texture2D<float> gAlpha;
SamplerState gSampler;

float main(float2 texC : TEXCOORD) : SV_TARGET0
{
    uint2 texDim;
    uint levels;
    gAlpha.GetDimensions(0, texDim.x, texDim.y, levels);
    float2 step = 1.0 / float2(texDim.x, texDim.y);

    float alpha = 0.0;
    for(int i = -radius; i <= radius; i++)
    {
        alpha = max(alpha, gAlpha.SampleLevel(gSampler, texC + dir * step * i, 0));
    }

    return alpha;
}
