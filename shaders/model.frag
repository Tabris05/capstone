#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../shared/vertex.h"
#include "../shared/material.h"

#include "utils.glsl"

#define PI 3.141593f
#define EPSILON 0.000001f

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inUV;
layout(location = 5) flat in i32 inMaterialIndex;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D imageHeap[];
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube radianceMap;
layout(set = 1, binding = 2) uniform sampler2D brdfIntegralTex;

layout(buffer_reference, scalar) restrict readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) restrict readonly buffer MaterialBuffer {
    Material materials[];
};

layout(push_constant, scalar) uniform constants {
    VertexBuffer vertexBuffer;
    MaterialBuffer materialBuffer;
    mat4 modelTransform;
    mat4 cameraTransform;
    mat3 normalTransform;
    vec3 cameraPosition;
} pcs;

b8 bitmaskGet(u32 mask, u32 value) {
	return b8(mask & value);
}

float clampedDot(vec3 a, vec3 b) {
	return max(dot(a, b), 0.0f);
}

f32 isotrophicNDFFilter(vec3 normal, f32 alpha) {
	const f32 SIGMA2 = 0.15915494f;
	const f32 KAPPA = 0.18f;

	vec3 dndu = dFdx(normal);
	vec3 dndv = dFdy(normal);
	f32 kernalRoughness2 = 2.0f * SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
	kernalRoughness2 = min(kernalRoughness2, KAPPA);
	return clamp(alpha + kernalRoughness2, 0.0f, 1.0f);
}

f32 ggxNDF(vec3 normal, vec3 halfway, f32 alpha2) {
    f32 nDotH = clampedDot(normal, halfway);
    f32 denom = nDotH * nDotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (PI * denom * denom + EPSILON);
}

f32 geometrySmith(vec3 normal, vec3 view, vec3 light, f32 alpha2) {
    f32 nDotL = clampedDot(normal, light) + EPSILON;
    f32 nDotV = clampedDot(normal, view) + EPSILON;

    f32 denomA = nDotV * sqrt(alpha2 + (1.0f - alpha2) * nDotL * nDotL);
    f32 denomB = nDotL * sqrt(alpha2 + (1.0f - alpha2) * nDotV * nDotV);

    return 2.0f * nDotL * nDotV / (denomA + denomB);
}

vec3 fresnelSchlick(f32 cosTheta, vec3 f0) {
	return f0 + (1.0f - f0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

vec3 fresnelSchlickRoughness(f32 cosTheta, vec3 f0, float alpha) {
	return f0 + (max(vec3(1.0f - alpha), f0) - f0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

vec3 directionalLight(vec3 view, vec3 normal, vec3 albedo, f32 metallic, f32 alpha) {
    vec3 light = vec3(0.0f, 0.0f, -1.0f);
    vec3 halfway = normalize(view + light);
    f32 alpha2 = alpha * alpha;

    f32 distribution = ggxNDF(normal, halfway, alpha2);
    f32 geometry = geometrySmith(normal, view, light, alpha2);
    vec3 fresnel = fresnelSchlick(clampedDot(halfway, view), mix(vec3(0.04f), vec3(albedo), metallic));

    vec3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * albedo / PI;
    vec3 specular = (distribution * geometry * fresnel) / (4.0f * clampedDot(normal, view) * clampedDot(normal, light) + EPSILON);

    return (diffuse + specular) * clampedDot(normal, light);
}


vec3 ambientLight(vec3 view, vec3 normal, vec3 albedo, f32 metallic, f32 alpha, f32 occlusion) {
	vec3 fresnel = fresnelSchlickRoughness(clampedDot(normal, view), mix(vec3(0.04f), albedo, metallic), alpha);
	vec3 radiance = textureLod(radianceMap, reflect(-view, normal), alpha * (countMips(textureSize(radianceMap, 0)) - 1.0f)).rgb;
	vec2 brdf = textureLod(brdfIntegralTex, vec2(clampedDot(normal, view), alpha), 0.0f).rg;

	vec3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * albedo * textureLod(irradianceMap, normal, 0.0f).rgb;
	vec3 specular = radiance * (fresnel * brdf.x + brdf.y);

	return (diffuse + specular) * occlusion;
}

void main() {
    vec3 view = normalize(pcs.cameraPosition - inPosition);
    Material mat = pcs.materialBuffer.materials[inMaterialIndex];

    vec3 albedo = mat.baseColor.rgb;
    vec3 emission = mat.emissiveColor.rgb * mat.emissiveColor.a;
    vec3 normal = normalize(inNormal);
    f32 occlusion = 1.0f;
    f32 metallic = mat.metallic;
    f32 roughness = mat.roughness;

    if(bitmaskGet(mat.texBitfield, HAS_ALBEDO)) albedo *= texture(nonuniformEXT(imageHeap[mat.albedoIndex]), inUV).rgb;
    if(bitmaskGet(mat.texBitfield, HAS_EMISSIVE)) emission *= texture(nonuniformEXT(imageHeap[mat.emissiveIndex]), inUV).rgb;
    if(bitmaskGet(mat.texBitfield, HAS_OCCLUSION)) occlusion *= texture(nonuniformEXT(imageHeap[mat.occlusionIndex]), inUV).r;
    if(bitmaskGet(mat.texBitfield, HAS_NORMAL)) normal = normalize(mat3(normalize(inTangent), normalize(inBitangent), normal) * (texture(nonuniformEXT(imageHeap[mat.normalIndex]), inUV).rgb * 2.0f - 1.0f));
    if(bitmaskGet(mat.texBitfield, HAS_METALLIC_ROUGHNESS)) {
	    metallic *= texture(nonuniformEXT(imageHeap[mat.metallicRoughnessIndex]), inUV).b;
	    roughness *= texture(nonuniformEXT(imageHeap[mat.metallicRoughnessIndex]), inUV).g;	
    }

    roughness = max(roughness, 0.04f);
    roughness = sqrt(isotrophicNDFFilter(normal, roughness * roughness));
    vec3 outputColor = directionalLight(view, normal, albedo, metallic, roughness) + ambientLight(view, normal, albedo, metallic, roughness, occlusion) + emission;

    fragColor = agx(outputColor);
}