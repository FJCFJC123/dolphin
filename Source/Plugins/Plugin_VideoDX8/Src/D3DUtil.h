// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma	once

#include "D3DBase.h"
#include <math.h>
#include <MathUtil.h>

// NV2A/D3D8 port: shader params are DWORD handles, textures are IDirect3DTexture8.
// The font + shaded-quad helpers are used for EFB clears, XFB blits and the OSD —
// not the main GX geometry path. drawClearQuad maps to dev->Clear; the textured/
// font helpers are minimal for now (fleshed out with the Render port, task #15).

namespace DX8
{

namespace D3D
{
	// Font creation flags
	#define D3DFONT_BOLD        0x0001
	#define D3DFONT_ITALIC      0x0002

	// Font rendering flags
	#define D3DFONT_CENTERED    0x0001

	//a cut-down variant of the DXSDK CD3DFont class
	class CD3DFont
	{
		LPDIRECT3DTEXTURE8      m_pTexture;   // The d3d texture for this font
		LPDIRECT3DVERTEXBUFFER8 m_pVB;        // VertexBuffer for rendering text
		float   m_fTextScale;
		float   m_fTexCoords[128-32][4];

	public:
		CD3DFont();
		// 2D (no longer 3D) text drawing function
		// Initializing and destroying device-dependent objects
		void SetRenderStates();
		int Init();
		int Shutdown();
		int DrawTextScaled( float x, float y,
							float fXScale, float fYScale,
							float spacing, u32 dwColor,
							const char* strText);
	};

	extern CD3DFont font;

	void quad2d(float x1, float y1, float x2, float y2, u32 color, float u1=0, float v1=0, float u2=1, float v2=1);
	void drawShadedTexQuad(IDirect3DTexture8 *texture,
					   const RECT *rSource,
					   int SourceWidth,
					   int SourceHeight,
					   int DestWidth,
					   int DestHeight,
					   DWORD PShader,
					   DWORD Vshader,
					   float Gamma = 1.0f);
	void drawShadedTexSubQuad(IDirect3DTexture8 *texture,
							const MathUtil::Rectangle<float> *rSource,
							int SourceWidth,
							int SourceHeight,
							const MathUtil::Rectangle<float> *rDest,
							int DestWidth,
							int DestHeight,
							DWORD PShader,
							DWORD Vshader,
							float Gamma = 1.0f);
	void drawClearQuad(u32 Color, float z, DWORD PShader, DWORD Vshader);
	void drawColorQuad(u32 Color, float x1, float y1, float x2, float y2);

	void SaveRenderStates();
	void RestoreRenderStates();
}

}  // namespace DX8
