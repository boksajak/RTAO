#include "RTAOCommon.hlsl"

 // ---[ Closest Hit Shader ]---

[shader("closesthit")]
void ClosestHit(inout HitInfo payload : SV_RayPayload,
	Attributes attrib : SV_IntersectionAttributes)
{
	payload.T = RayTCurrent();
}