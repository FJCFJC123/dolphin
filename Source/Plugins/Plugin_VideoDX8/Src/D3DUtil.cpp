// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 port of the DX9 quad/font utilities. For the first-light milestone
// (drain the GX FIFO, un-hang the guest) only drawClearQuad needs to do real work
// (mapped to dev->Clear); the textured-quad blits and the font are stubbed and
// get fleshed out with the Render/FramebufferManager port (task #15).

#include "D3DUtil.h"
#include "VideoConfig.h"

namespace DX8
{

namespace D3D
{

CD3DFont font;

CD3DFont::CD3DFont()
{
	m_pTexture = NULL;
	m_pVB = NULL;
	m_fTextScale = 1.0f;
}

int CD3DFont::Init()          { return S_OK; }   // TODO(task#15): system-font texture
int CD3DFont::Shutdown()      { return S_OK; }
void CD3DFont::SetRenderStates() {}
int CD3DFont::DrawTextScaled(float x, float y, float fXScale, float fYScale,
	float spacing, u32 dwColor, const char* strText)
{
	(void)x; (void)y; (void)fXScale; (void)fYScale;
	(void)spacing; (void)dwColor; (void)strText;
	return S_OK;
}

void quad2d(float x1, float y1, float x2, float y2, u32 color, float u1, float v1, float u2, float v2)
{
	(void)x1; (void)y1; (void)x2; (void)y2; (void)color;
	(void)u1; (void)v1; (void)u2; (void)v2;
	// TODO(task#15): DrawPrimitiveUP textured quad
}

void drawShadedTexQuad(IDirect3DTexture8 *texture,
				   const RECT *rSource, int SourceWidth, int SourceHeight,
				   int DestWidth, int DestHeight,
				   DWORD PShader, DWORD Vshader, float Gamma)
{
	(void)texture; (void)rSource; (void)SourceWidth; (void)SourceHeight;
	(void)DestWidth; (void)DestHeight; (void)PShader; (void)Vshader; (void)Gamma;
	// TODO(task#15): textured blit for XFB/EFB copies
}

void drawShadedTexSubQuad(IDirect3DTexture8 *texture,
						const MathUtil::Rectangle<float> *rSource, int SourceWidth, int SourceHeight,
						const MathUtil::Rectangle<float> *rDest, int DestWidth, int DestHeight,
						DWORD PShader, DWORD Vshader, float Gamma)
{
	(void)texture; (void)rSource; (void)SourceWidth; (void)SourceHeight;
	(void)rDest; (void)DestWidth; (void)DestHeight; (void)PShader; (void)Vshader; (void)Gamma;
	// TODO(task#15)
}

void drawClearQuad(u32 Color, float z, DWORD PShader, DWORD Vshader)
{
	(void)PShader; (void)Vshader;
	if (dev)
		dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, Color, z, 0);
}

void drawColorQuad(u32 Color, float x1, float y1, float x2, float y2)
{
	(void)Color; (void)x1; (void)y1; (void)x2; (void)y2;
	// TODO(task#15): DrawPrimitiveUP colored quad
}

void SaveRenderStates()    {}
void RestoreRenderStates() {}

}  // namespace

}  // namespace DX8
