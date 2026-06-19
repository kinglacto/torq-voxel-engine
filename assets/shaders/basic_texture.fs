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
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform vec3 skyAmbientColor;
uniform vec3 groundAmbientColor;
uniform float hemisphereStrength;
uniform float sunStrength;
uniform float sunWrap;
uniform float maxLight;

void main() {
    vec4 texColor = texture(texture1, vec3(TexCoord, float(TexLayer)));

    vec3 normal = normalize(Normal);
    vec3 lightDirection = normalize(-sunDirection);
    float direct = clamp(
        (dot(normal, lightDirection) + sunWrap) / (1.0 + sunWrap),
        0.0,
        1.0
    );
    direct = smoothstep(0.0, 1.0, direct);

    float hemisphere = normal.y * 0.5 + 0.5;
    vec3 ambient = mix(groundAmbientColor, skyAmbientColor, hemisphere) *
        hemisphereStrength;
    vec3 lighting = min(ambient + sunColor * sunStrength * direct,
        vec3(maxLight));
    vec3 litColor = texColor.rgb * lighting;

    float distanceFromCamera = length(FragPos - cameraPos);
    float fog = smoothstep(fogStart, fogEnd, distanceFromCamera);
    vec3 color = mix(litColor, fogColor, fog);

    FragColor = vec4(color, texColor.a);
}
