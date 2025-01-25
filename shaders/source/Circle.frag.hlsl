struct Input {
    float4 Position : SV_Position;
    float4 Color1   : TEXCOORD0;
    float4 Color2   : TEXCOORD1;
    float2 UV       : TEXCOORD2;
    [[vk::builtin("PointSize")]]
	float PointSize : PSIZE0; 
};

float4 main(Input input) : SV_Target0 {
    float2 uv = input.UV;
    if (uv.x*uv.x + uv.y*uv.y <= 0.25f)
		return input.Color1; 
    else 
		return input.Color2; 
}
