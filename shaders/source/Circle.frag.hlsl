float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    if (uv.x*uv.x + uv.y*uv.y <= 1.0f)
        return float4(1.0f, 0.0f, 0.0f, 1.0f); 
    else 
        return float4(0.0f, 0.0f, 0.0f, 0.0f); 
}
