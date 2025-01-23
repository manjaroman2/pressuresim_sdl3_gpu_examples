struct Input
{
    float3 position     : TEXCOORD0;
    float4 color        : TEXCOORD1;
    uint instance_index : SV_InstanceID;
    uint vertex_index   : SV_VertexID;
};

struct Output
{
    float4 color    : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.color = input.color;
    float3 pos = (input.position * 0.25f) - float3(0.75f, 0.75f, 0.0f);
    pos.x += (float(input.instance_index % 4) * 0.5f);
    pos.y += (floor(float(input.instance_index / 4)) * 0.5f);
    output.position = float4(pos, 1.0f);
    return output;
}
