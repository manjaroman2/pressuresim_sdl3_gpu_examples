struct Input {
    float4 Position : SV_Position;
    float4 Color    : TEXCOORD0;
    float2 UV       : TEXCOORD1;
    [[vk::builtin("PointSize")]]
	float PointSize : PSIZE0; 
};

float4 main(Input input) : SV_Target0 {
    float2 uv = input.UV;
    uv -= float2(2.0f, 2.0f); 
    if (uv.x*uv.x + uv.y*uv.y <= 1.0f)
		return input.Color;
		// return float4(1.0f, 0.0f, 0.0f, 1.0f); 
    else 
		return float4(0.0f, 0.0f, 0.0f, 0.0f); 
}
