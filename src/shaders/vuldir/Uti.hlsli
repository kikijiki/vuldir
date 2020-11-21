#pragma once

inline float2 unpackHalf2(in uint v)
{
	float2 ret;
	ret.x = f16tof32(v.x);
	ret.y = f16tof32(v.x >> 16u);
	return ret;
}

inline float3 unpackUnitVector(in uint v)
{
	float3 ret;
	ret.x = (float)((v >>  0u) & 0xFF) / 255.0 * 2 - 1;
	ret.y = (float)((v >>  8u) & 0xFF) / 255.0 * 2 - 1;
	ret.z = (float)((v >> 16u) & 0xFF) / 255.0 * 2 - 1;
	return ret;
}