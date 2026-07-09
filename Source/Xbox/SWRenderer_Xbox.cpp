// OG Xbox port: Xbox replacement for SWRenderer.cpp.
// The desktop SWRenderer presents the software-rasterized XFB via OpenGL; this
// version routes it through the D3D8 present backend (xbox_d3d8). The XFB comes
// in as RGBA8888 (EfbInterface::efbColorTexture) from EfbCopy.cpp.

#include "Common.h"
#include "SWRenderer.h"
#include "SWStatistics.h"
#include "xbox_d3d8.h"

void SWRenderer::Init() {}
void SWRenderer::Shutdown() {}
void SWRenderer::Prepare() {}

void SWRenderer::RenderText(const char* pstr, int left, int top, u32 color)
{
	xbox_d3d8_debug_text(left, top, color, pstr);
}

void SWRenderer::DrawDebugText()
{
	if (!g_SWVideoConfig.bShowStats)
		return;
	char buf[256];
	sprintf(buf, "Tris:%i Pix:%i", swstats.thisFrame.numTrianglesDrawn,
	        swstats.thisFrame.rasterizedPixels);
	xbox_d3d8_debug_text(20, 20, 0xFFFFFF00, buf);
}

// Present the software-rasterized framebuffer (RGBA8888) fullscreen.
void SWRenderer::DrawTexture(u8 *texture, int width, int height)
{
	xbox_d3d8_present_xfb(texture, width, height);
}

// Present happens inside DrawTexture on the Xbox path; SwapBuffer just resets.
void SWRenderer::SwapBuffer()
{
	swstats.ResetFrame();
}
