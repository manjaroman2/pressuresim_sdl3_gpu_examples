#version 450
struct LineData {
	vec2 position;
	uint flags;
	uint _pad;
};

layout(std430, binding = 0) buffer LineDataBuffer {
    LineData lines[];  // Array of LineData structs
};

layout(location = 0) in vec2 in_position;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_position;

void main() {
    vec2 offset = lines[gl_InstanceIndex].position; 
    vec2 final_position = in_position + offset;

    out_position = vec4(final_position, 0.0, 1.0);
    out_color = vec4(1.0, 0.0, 0.0, 1.0);
    
    gl_Position = out_position;
}
