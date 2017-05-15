#version 330

uniform sampler2D uImage;

out vec3 fColor;

void main()
{
    vec4 value = texelFetch(uImage, ivec2(gl_FragCoord.xy), 0);
    if (value.a == 0.f)
        discard;
    fColor = value.rgb / value.a;
}