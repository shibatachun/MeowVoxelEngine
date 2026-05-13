#version 450

layout(location = 0) in vec3 in_Position;
layout(location = 1) in vec3 in_Normal;
layout(location = 2) in vec3 in_Color;
layout(location = 3) in vec2 in_FaceUV;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec3 v_Albedo;
layout(location = 3) out vec2 v_FaceUV;

layout(push_constant) uniform CameraConstants
{
    mat4 viewProjection;
    vec4 materialParameters;
} camera;

void main()
{
    gl_Position = camera.viewProjection * vec4(in_Position, 1.0);
    v_WorldPosition = in_Position;
    v_Normal = normalize(in_Normal);
    v_Albedo = in_Color;
    v_FaceUV = in_FaceUV;
}
