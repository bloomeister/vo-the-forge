/*
 * Minimal SRT layout for the ECS example shaders.
 * Describes the resources used in Basic.vert/frag.
 */
#pragma once

STRUCT(InstanceData)
{
	DATA(float4, posScale, None);
	DATA(float4, colorIndex, None);
};

BEGIN_SRT(SrtData)
	BEGIN_SRT_SET(Persistent)
		DECL_TEXTURE(Persistent, Tex2D(float4), uTexture0)
		DECL_SAMPLER(Persistent, SamplerState, uSampler0)
	END_SRT_SET(Persistent)
	BEGIN_SRT_SET(PerFrame)
		DECL_BUFFER(PerFrame, Buffer(InstanceData), instanceBuffer)
	END_SRT_SET(PerFrame)
END_SRT(SrtData)
