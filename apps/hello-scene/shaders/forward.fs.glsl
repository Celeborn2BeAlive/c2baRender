#version 330

in vec3 vViewSpacePosition;
in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

out vec3 fColor;

void main()
{
   fColor = vec3(dot(normalize(vViewSpaceNormal), normalize(-vViewSpacePosition)));
}