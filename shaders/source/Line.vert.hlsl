struct Line { 
    float2 position; 
    uint flags; 
    uint _pad; 
};

StructuredBuffer<Line> line_data_buffer: register(t0, space0);

struct Input {
    float2 position : TEXCOORD0;
    uint instance_index: SV_InstanceID;
	uint vertex_index : SV_VertexID; 
};

struct Output {
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input) {
    Output output;
	if (line_data_buffer[input.instance_index].flags == 1) {
		if (input.vertex_index >= 2) {
			float x = input.position.x + line_data_buffer[input.instance_index].position.x;
			float y = input.position.y + line_data_buffer[input.instance_index].position.y;
			output.position = float4(x, y, 0.0f, 1.0f); 
		}
	} 
    else if (line_data_buffer[input.instance_index].flags == 0) {
		if (input.vertex_index <= 1) {
			float x = input.position.x + line_data_buffer[input.instance_index].position.x;
			float y = input.position.y + line_data_buffer[input.instance_index].position.y;
			output.position = float4(x, y, 0.0f, 1.0f); 
		}
	}
	output.color = float4(1.0f, 0.0f, 0.0f, 1.0f); 
	return output; 
}
