#include "RTAOCommon.hlsl"

 // ---[ Miss Shader ]---

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload)
{
	// Intentionally left empty
}