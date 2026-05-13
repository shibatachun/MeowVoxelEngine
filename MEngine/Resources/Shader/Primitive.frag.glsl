#version 450

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_Albedo;
layout(location = 3) in vec2 v_FaceUV;

layout(location = 0) out vec4 out_WorldPosition;
layout(location = 1) out vec4 out_Normal;
layout(location = 2) out vec4 out_Albedo;
layout(location = 3) out vec4 out_Material;

layout(push_constant) uniform GBufferConstants
{
    mat4 viewProjection;
    vec4 materialParameters;
} gbuffer;

void main()
{
    vec2 edgeDistance = min(v_FaceUV, vec2(1.0) - v_FaceUV);
    float nearestEdge = min(edgeDistance.x, edgeDistance.y);
    float edgeWidth = 0.035;
    float edgeSoftness = max(fwidth(nearestEdge), 0.006);
    float edgeMask = 1.0 - smoothstep(edgeWidth, edgeWidth + edgeSoftness, nearestEdge);
    vec3 edgedAlbedo = mix(clamp(v_Albedo, vec3(0.0), vec3(1.0)), vec3(0.015), edgeMask * 0.82);

    out_WorldPosition = vec4(v_WorldPosition, 1.0);
    out_Normal = vec4(normalize(v_Normal) * 0.5 + 0.5, 1.0);
    out_Albedo = vec4(edgedAlbedo, 1.0);
    out_Material = vec4(
        clamp(gbuffer.materialParameters.x, 0.0, 1.0),
        clamp(gbuffer.materialParameters.y, 0.04, 1.0),
        clamp(gbuffer.materialParameters.z, 0.0, 1.0),
        1.0);
}
