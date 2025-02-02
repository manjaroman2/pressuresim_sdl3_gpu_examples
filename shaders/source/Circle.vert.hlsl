struct Particle { 
    float2 Position; 
};

StructuredBuffer<Particle> ParticleDataBuffer: register(t0, space0);

struct Input
{
    float3 Position    : TEXCOORD0;
    float2 UV          : TEXCOORD1;
    float4 Color1      : TEXCOORD2;
    float4 Color2      : TEXCOORD3;
    uint InstanceIndex : SV_InstanceID;
};

struct Output
{
    float4 Position : SV_Position;
    float4 Color1   : TEXCOORD0;
    float4 Color2   : TEXCOORD1;
    float2 UV       : TEXCOORD2;
    [[vk::builtin("PointSize")]]
	float PointSize : PSIZE0; 
};

Output main(Input input)
{
    Output output;
    float x = input.Position.x + ParticleDataBuffer[input.InstanceIndex].Position.x;
    float y = input.Position.y + ParticleDataBuffer[input.InstanceIndex].Position.y;
    output.Color1 = input.Color1;  
    output.Color2 = input.Color2;  
    output.Position = float4(x, y, 0.0f, 1.0f); 
	output.PointSize = 40.0f; 
    output.UV = input.UV - 0.5f; 
    return output;
}
