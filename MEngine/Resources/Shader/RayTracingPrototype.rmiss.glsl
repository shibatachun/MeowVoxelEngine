#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 rtPayload;

void main()
{
    rtPayload.a = 0.0;
}
