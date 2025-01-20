cbuffer PerFrameCB
{
    float2 dir;
    float gSharpen; // between 0 and 1
};

Texture2D<float4> gInput;
SamplerState gSampler;

float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    uint2 texDim;
    uint levels;
    gInput.GetDimensions(0, texDim.x, texDim.y, levels);
    float2 step = 1.0 / float2(texDim.x, texDim.y);

    //return gInput.SampleLevel(gSampler, texC, 0);

    float2 centerUV = texC + dir * step * 0.5; // in between 2 texels
    float2 leftUV = centerUV - dir * step * 1.5;
    float2 rightUV = centerUV + dir * step * 1.5;

    // TODO amount of sharpening based on local contrast?
    // catmull-like sharpen
    return (1.0 + gSharpen) * gInput.SampleLevel(gSampler, centerUV, 0)
       - gSharpen * 0.5 * gInput.SampleLevel(gSampler, leftUV, 0)
       - gSharpen * 0.5 * gInput.SampleLevel(gSampler, rightUV, 0);
}
