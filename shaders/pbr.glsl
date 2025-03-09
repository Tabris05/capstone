#ifndef PBR_GLSL
#define PBR_GLSL

#include "utils.glsl"
#include "../shared/material.h"

#define PI 3.141593f
#define EPSILON 0.000001f

struct PBRMaterial {
    vec4 albedo;
    vec3 emission;
    vec3 normal;
    f32 occlusion;
    f32 metallic;
    f32 roughness;
};

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

f32 inShadow(vec3 lightspacePos) {
    lightspacePos.xy = lightspacePos.xy * 0.5f + 0.5f;
    return texture(shadowMapTex, lightspacePos);
}

vec3 directionalLight(vec3 view, vec3 light, vec3 lightspacePos, vec3 lightColor, PBRMaterial mat) {
    f32 shadow = inShadow(lightspacePos);

    if(shadow == 0.0f) {
        return vec3(0.0f);
    }
    vec3 halfway = normalize(view + light);
    f32 roughness2 = mat.roughness * mat.roughness;

    f32 distribution = ggxNDF(mat.normal, halfway, roughness2);
    f32 geometry = geometrySmith(mat.normal, view, light, roughness2);
    vec3 fresnel = fresnelSchlick(clampedDot(halfway, view), mix(vec3(0.04f), mat.albedo.rgb, mat.metallic));

    vec3 diffuse = (1.0f - fresnel) * (1.0f - mat.metallic) * mat.albedo.rgb / PI;
    vec3 specular = (distribution * geometry * fresnel) / (4.0f * clampedDot(mat.normal, view) * clampedDot(mat.normal, light) + EPSILON);

    return (diffuse + specular) * clampedDot(mat.normal, light) * lightColor * shadow;
}


vec3 ambientLight(vec3 view, PBRMaterial mat) {
	vec3 fresnel = fresnelSchlickRoughness(clampedDot(mat.normal, view), mix(vec3(0.04f), mat.albedo.rgb, mat.metallic), mat.roughness);
	vec3 radiance = textureLod(radianceMap, reflect(-view, mat.normal), mat.roughness * (countMips(textureSize(radianceMap, 0)) - 1.0f)).rgb;
	vec2 brdf = textureLod(brdfIntegralTex, vec2(clampedDot(mat.normal, view), mat.roughness), 0.0f).rg;

	vec3 diffuse = (1.0f - fresnel) * (1.0f - mat.metallic) * mat.albedo.rgb * textureLod(irradianceMap, mat.normal, 0.0f).rgb;
	vec3 specular = radiance * (fresnel * brdf.x + brdf.y);

	return (diffuse + specular) * mat.occlusion;
}

PBRMaterial getPBRMaterial(Material mat, vec2 inUV) {
    PBRMaterial result;
    result.albedo = mat.baseColor;
    if(bitmaskGet(mat.texBitfield, HAS_ALBEDO)) {
        result.albedo *= texture(nonuniformEXT(imageHeap[mat.albedoIndex]), inUV);
    }

    result.emission = mat.emissiveColor.rgb * mat.emissiveColor.a;
    if(bitmaskGet(mat.texBitfield, HAS_EMISSIVE)) {
        result.emission *= texture(nonuniformEXT(imageHeap[mat.emissiveIndex]), inUV).rgb;
    }

    result.normal = normalize(inNormal);
    if(bitmaskGet(mat.texBitfield, HAS_NORMAL)) {
        result.normal = normalize(mat3(normalize(inTangent), normalize(inBitangent), result.normal) * (texture(nonuniformEXT(imageHeap[mat.normalIndex]), inUV).rgb * 2.0f - 1.0f));
    }
    
    result.occlusion = 1.0f;
    if(bitmaskGet(mat.texBitfield, HAS_OCCLUSION)) {
        result.occlusion *= texture(nonuniformEXT(imageHeap[mat.occlusionIndex]), inUV).r;
    }

    result.metallic = mat.metallic;
    result.roughness = mat.roughness;
    if(bitmaskGet(mat.texBitfield, HAS_METALLIC_ROUGHNESS)) {
	    result.metallic *= texture(nonuniformEXT(imageHeap[mat.metallicRoughnessIndex]), inUV).b;
	    result.roughness *= texture(nonuniformEXT(imageHeap[mat.metallicRoughnessIndex]), inUV).g;	
    }
    
    result.roughness = max(result.roughness, 0.04f);
    result.roughness = sqrt(isotrophicNDFFilter(result.normal, result.roughness * result.roughness));

    return result;
}

#endif