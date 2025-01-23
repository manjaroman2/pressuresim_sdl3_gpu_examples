struct Input
{
    float2 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

struct Output
{
    float2 uv        : TEXCOORD0; 
    float4 position  : SV_POSITION;
    float point_size : PSIZE;
};

Output main(Input input)
{
    Output output;
    output.uv = input.position; 
    output.position = float4(input.position, 1.0f, 1.0f);

    output.point_size = 2.0f; 
    return output;
}
