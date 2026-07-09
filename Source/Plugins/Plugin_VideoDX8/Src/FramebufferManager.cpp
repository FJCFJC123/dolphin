// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 FramebufferManager — FIRST-LIGHT MINIMAL. The DX9 version builds a
// full EFB (color + depth textures, 1x1/4x4 read buffers, sysmem offscreen
// surfaces for AccessEFB, a reinterpret texture). Several of those use D3D9-only
// entry points that differ on D3D8 (no CreateOffscreenPlainSurface -> use
// CreateImageSurface; SetRenderTarget(rt, ds) is one call; CreateDepthStencilSurface
// drops the quality/discard/shared args). For the first-light milestone we create
// only the EFB color render target so Render can bind a target; depth, EFB read-
// back and XFB copies are deferred to task #15.

#include "D3DBase.h"
#include "Render.h"
#include "FramebufferManager.h"
#include "VideoConfig.h"
#include "PixelShaderCache.h"
#include "VertexShaderCache.h"
#include "TextureConverter.h"
#include "HW/Memmap.h"

namespace DX8
{

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = NULL; }

inline void GetSurface(IDirect3DTexture8* texture, IDirect3DSurface8** surface)
{
	if (!texture) return;
	texture->GetSurfaceLevel(0, surface);
}

FramebufferManager::Efb FramebufferManager::s_efb;

FramebufferManager::FramebufferManager()
{
	int target_width = Renderer::GetTargetWidth();
	int target_height = Renderer::GetTargetHeight();
	s_efb.color_surface_Format = D3DFMT_A8R8G8B8;

	// EFB color texture - primary render target. D3D8 CreateTexture has no trailing
	// pSharedHandle argument.
	HRESULT hr = D3D::dev->CreateTexture(target_width, target_height, 1, D3DUSAGE_RENDERTARGET,
		s_efb.color_surface_Format, D3DPOOL_DEFAULT, &s_efb.color_texture);
	GetSurface(s_efb.color_texture, &s_efb.color_surface);
	if (FAILED(hr))
		ERROR_LOG(VIDEO, "EFB color RT create failed (%dx%d hr=%#x)", target_width, target_height, hr);

	// TODO(task#15): depth texture/surface, 1x1/4x4 EFB read buffers + sysmem
	// CreateImageSurface offscreen buffers for AccessEFB, and the reinterpret
	// texture. Left NULL for now (EFB peeks/pokes + reinterpret are inert).
	s_efb.depth_surface_Format = D3DFMT_UNKNOWN;
	s_efb.depth_ReadBuffer_Format = D3DFMT_UNKNOWN;
}

FramebufferManager::~FramebufferManager()
{
	SAFE_RELEASE(s_efb.depth_surface);
	SAFE_RELEASE(s_efb.color_surface);
	SAFE_RELEASE(s_efb.color_ReadBuffer);
	SAFE_RELEASE(s_efb.depth_ReadBuffer);
	SAFE_RELEASE(s_efb.color_OffScreenReadBuffer);
	SAFE_RELEASE(s_efb.depth_OffScreenReadBuffer);
	SAFE_RELEASE(s_efb.color_texture);
	SAFE_RELEASE(s_efb.colorRead_texture);
	SAFE_RELEASE(s_efb.depth_texture);
	SAFE_RELEASE(s_efb.depthRead_texture);
	SAFE_RELEASE(s_efb.color_reinterpret_texture);
	SAFE_RELEASE(s_efb.color_reinterpret_surface);
	s_efb.color_surface_Format = D3DFMT_UNKNOWN;
	s_efb.depth_surface_Format = D3DFMT_UNKNOWN;
	s_efb.depth_ReadBuffer_Format = D3DFMT_UNKNOWN;
}

XFBSourceBase* FramebufferManager::CreateXFBSource(unsigned int target_width, unsigned int target_height)
{
	LPDIRECT3DTEXTURE8 tex = NULL;
	D3D::dev->CreateTexture(target_width, target_height, 1, D3DUSAGE_RENDERTARGET,
		s_efb.color_surface_Format, D3DPOOL_DEFAULT, &tex);
	return new XFBSource(tex);
}

void FramebufferManager::GetTargetSize(unsigned int *width, unsigned int *height, const EFBRectangle& sourceRc)
{
	TargetRectangle targetSource;

	targetSource.top = ScaleToVirtualXfbHeight(sourceRc.top, Renderer::GetBackbufferHeight());
	targetSource.bottom = ScaleToVirtualXfbHeight(sourceRc.bottom, Renderer::GetBackbufferHeight());
	targetSource.left = ScaleToVirtualXfbWidth(sourceRc.left, Renderer::GetBackbufferWidth());
	targetSource.right = ScaleToVirtualXfbWidth(sourceRc.right, Renderer::GetBackbufferWidth());

	*width = targetSource.right - targetSource.left;
	*height = targetSource.bottom - targetSource.top;
}

void XFBSource::Draw(const MathUtil::Rectangle<float> &sourcerc,
	const MathUtil::Rectangle<float> &drawrc, int width, int height) const
{
	// TODO(task#15): shaded blit of the XFB texture to the backbuffer.
	(void)sourcerc; (void)drawrc; (void)width; (void)height;
}

void XFBSource::DecodeToTexture(u32 xfbAddr, u32 fbWidth, u32 fbHeight)
{
	// TODO(task#15): TextureConverter::DecodeToTexture(...)
	(void)xfbAddr; (void)fbWidth; (void)fbHeight;
}

void FramebufferManager::CopyToRealXFB(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma)
{
	// TODO(task#15): EncodeToRamYUYV for real-XFB mode.
	(void)xfbAddr; (void)fbWidth; (void)fbHeight; (void)sourceRc; (void)Gamma;
}

void XFBSource::CopyEFB(float Gamma)
{
	// TODO(task#15): EFB->XFB copy (SetRenderTarget(rt, ds) is one call on D3D8).
	(void)Gamma;
}

}  // namespace DX8
