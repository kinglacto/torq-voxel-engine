#version 430 core

out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
flat in uint TexLayer;

uniform sampler2DArray texture1;
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

void main() {
    vec4 texColor = texture(texture1, vec3(TexCoord, float(TexLayer)));

    float shade = 0.68;
    if (Normal.y > 0.5) {
        shade = 1.0;
    } else if (Normal.y < -0.5) {
        shade = 0.45;
    }

    float distanceFromCamera = length(FragPos - cameraPos);
    float fog = smoothstep(fogStart, fogEnd, distanceFromCamera);
    vec3 color = mix(texColor.rgb * shade, fogColor, fog);

    FragColor = vec4(color, texColor.a);
}
