import Utils.Color.ColorHelpers;

Texture2D<float4> gCurColor;
Texture2D<float4> gTaaColor;
Texture2D<float> gAlpha;



float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    uint2 texDim;
    uint levels;
    gCurColor.GetDimensions(0, texDim.x, texDim.y, levels);
    float2 pos = texC * texDim;
    int2 ipos = int2(pos);

    float alpha = gAlpha[ipos];
    float3 curColor = RGBToYCgCo(gCurColor[ipos].rgb);
    float3 taaColor = RGBToYCgCo(gTaaColor[ipos].rgb);

    return float4(YCgCoToRGB(lerp(taaColor, curColor, alpha)), 1.0);
}
