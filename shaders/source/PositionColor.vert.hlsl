struct Input
{
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct Output
{
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.color = input.color;
    output.position = float4(input.position, 1.0f);
    return output;
}

