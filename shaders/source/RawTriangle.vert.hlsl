struct Input {
    uint vertex_index : SV_VertexID;
};

struct Output {
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
    float2 pos;
    [forcecase] switch(input.vertex_index) {
        case 0: 
            pos = float2(-1.0f, -1.0f);
            output.color = float4(1.0f, 0.0f, 0.0f, 1.0f);
            break; 
        case 1:
            pos = float2(1.0f, -1.0f);
            output.color = float4(0.0f, 1.0f, 0.0f, 1.0f);
            break; 
        case 2: 
            pos = float2(0.0f, 1.0f);
            output.color = float4(0.0f, 0.0f, 1.0f, 1.0f);
            break; 
    }
    output.position = float4(pos, 0.0f, 1.0f);
    return output;
}
