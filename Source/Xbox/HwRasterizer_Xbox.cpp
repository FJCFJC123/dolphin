// OG Xbox port: HwRasterizer stubs.
// The desktop HwRasterizer is a GL-based hardware-accelerated rasterizer used
// only when g_SWVideoConfig.bHwRasterizer is set (we never enable it — the
// Xbox path is pure software rasterization -> D3D8 XFB blit). These no-op
// stubs satisfy the references in Rasterizer/EfbCopy/DebugUtil at link time.

#include "HwRasterizer.h"

namespace HwRasterizer
{
	void Init() {}
	void Shutdown() {}
	void Prepare() {}
	void BeginTriangles() {}
	void EndTriangles() {}
	void DrawTriangleFrontFace(OutputVertexData*, OutputVertexData*, OutputVertexData*) {}
	void Clear() {}
}
