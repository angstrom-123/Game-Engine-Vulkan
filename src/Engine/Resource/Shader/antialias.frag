#version 450

#define FXAA_SPAN_MAX 8.0
#define FXAA_REDUCE_MUL 1.0 / 8.0
#define FXAA_REDUCE_MIN 1.0 / 128.0
#define FXAA_LUMA_THRESHOLD 0.5

layout (push_constant) uniform Constants {
    vec4 padding;
} constants;

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    // Camera
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;
    // Settings
    float exposure;
    float gamma;
    float ambientIntensity;
    uint lightCount;
    ivec2 screenSize;
} uniforms;

layout (set = 0, binding = 2) uniform sampler2D ldrSampler;

layout (location = 0) in vec2 vUV;

layout (location = 0) out vec4 outColor;

float Luminance(vec3 rgb)
{
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

// void main()
// {
//     vec2 texelSize = vec2(1.0) / vec2(uniforms.screenSize);
//
//     vec3 rgbNW = texture(ldrSampler, vUV + vec2(-texelSize.x, -texelSize.y)).xyz;
//     vec3 rgbNE = texture(ldrSampler, vUV + vec2(texelSize.x, -texelSize.y)).xyz;
//     vec3 rgbSW = texture(ldrSampler, vUV + vec2(-texelSize.x, texelSize.y)).xyz;
//     vec3 rgbSE = texture(ldrSampler, vUV + vec2(texelSize.x, texelSize.y)).xyz;
//     vec3 rgbM = texture(ldrSampler, vUV).xyz;
//
//     vec3 luma = vec3(0.299, 0.587, 0.114);
//     float lumaNW = dot(rgbNW, luma);
//     float lumaNE = dot(rgbNE, luma);
//     float lumaSW = dot(rgbSW, luma);
//     float lumaSE = dot(rgbSE, luma);
//     float lumaM  = dot(rgbM,  luma);
//
//     float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
//     float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
//
//     vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
//
//     float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
//
//     float rcpDirMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);
//
//     dir = min(vec2( FXAA_SPAN_MAX,  FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin));
//     dir /= uniforms.screenSize;
//
//     vec3 rgbA = (1.0 / 2.0) * (
//         texture(ldrSampler, vUV + dir * (1.0 / 3.0 - 0.5)).xyz +
//         texture(ldrSampler, vUV + dir * (2.0 / 3.0 - 0.5)).xyz);
//     vec3 rgbB = rgbA * (1.0 / 2.0) + (1.0 / 4.0) * (
//         texture(ldrSampler, vUV + dir * (0.0 / 3.0 - 0.5)).xyz +
//         texture(ldrSampler, vUV + dir * (3.0 / 3.0 - 0.5)).xyz);
//     float lumaB = dot(rgbB, luma);
//
//     if ((lumaB < lumaMin) || (lumaB > lumaMax)) {
//         outColor = vec4(rgbA, 1.0);
//     } else {
//         outColor = vec4(rgbB, 1.0);
//     }
// }

void main()
{
    vec3 rgbM = texture(ldrSampler, vUV).rgb;

    vec2 texelSize = vec2(1.0) / vec2(uniforms.screenSize);
    vec3 rgbNW = texture(ldrSampler, vUV + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 rgbNE = texture(ldrSampler, vUV + vec2(texelSize.x, texelSize.y)).rgb;
    vec3 rgbSW = texture(ldrSampler, vUV + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 rgbSE = texture(ldrSampler, vUV + vec2(texelSize.x, -texelSize.y)).rgb;

	const vec3 toLuma = vec3(0.299, 0.587, 0.114);
	
	// Convert from RGB to luma.
	float lumaNW = dot(rgbNW, toLuma);
	float lumaNE = dot(rgbNE, toLuma);
	float lumaSW = dot(rgbSW, toLuma);
	float lumaSE = dot(rgbSE, toLuma);
	float lumaM = dot(rgbM, toLuma);

	// Gather minimum and maximum luma.
	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	
    // Early exit if no AA needed
	if (lumaMax - lumaMin <= lumaMax * FXAA_LUMA_THRESHOLD) {
		outColor = vec4(rgbM, 1.0);
		return;
	}  
	
	// Sampling is done along the gradient.
	vec2 samplingDirection;	
	samplingDirection.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    samplingDirection.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    
    // Sampling step distance depends on the luma: The brighter the sampled texels, the smaller the final sampling step direction.
    // This results, that brighter areas are less blurred/more sharper than dark areas.  
    float samplingDirectionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);

	// Factor for norming the sampling direction plus adding the brightness influence. 
	float minSamplingDirectionFactor = 1.0 / (min(abs(samplingDirection.x), abs(samplingDirection.y)) + samplingDirectionReduce);
    
    // Calculate final sampling direction vector by reducing, clamping to a range and finally adapting to the texture size. 
    samplingDirection = clamp(samplingDirection * minSamplingDirectionFactor, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * texelSize;
	
	// Inner samples on the tab.
	vec3 rgbSampleNeg = texture(ldrSampler, vUV + samplingDirection * (1.0/3.0 - 0.5)).rgb;
	vec3 rgbSamplePos = texture(ldrSampler, vUV + samplingDirection * (2.0/3.0 - 0.5)).rgb;

	vec3 rgbTwoTab = (rgbSamplePos + rgbSampleNeg) * 0.5;  

	// Outer samples on the tab.
	vec3 rgbSampleNegOuter = texture(ldrSampler, vUV + samplingDirection * (0.0/3.0 - 0.5)).rgb;
	vec3 rgbSamplePosOuter = texture(ldrSampler, vUV + samplingDirection * (3.0/3.0 - 0.5)).rgb;
	
	vec3 rgbFourTab = (rgbSamplePosOuter + rgbSampleNegOuter) * 0.25 + rgbTwoTab * 0.5;   
	
	// Calculate luma for checking against the minimum and maximum value
	float lumaFourTab = dot(rgbFourTab, toLuma);
	
	// Select higher samples if all within bounds
	if (lumaFourTab < lumaMin || lumaFourTab > lumaMax) {
		outColor = vec4(rgbTwoTab, 1.0); 
	} else {
		outColor = vec4(rgbFourTab, 1.0);
	}
}
