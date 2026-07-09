// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 texture create+upload. Rewritten lean and SSE1-safe: the DX9 backend
// used SSE2/SSSE3 intrinsics (ConvertRGBA_BGRA_SSE2, _mm_mfence) for the RGBA->BGRA
// swizzle, which would execute SSE2 opcodes and fault on the Xbox's SSE1-only
// Coppermine. All uploads here are scalar. D3D8 API deltas: CreateTexture has no
// trailing pSharedHandle; no D3DUSAGE_AUTOGENMIPMAP (create a single level);
// CreateImageSurface/CreateDepthStencilSurface signatures differ from D3D9.
// Swizzled/paletted NV2A texture formats (task #15) come later; for now textures
// are linear and only matter once real shaders draw (task #14).

#include "D3DBase.h"
#include "D3DTexture.h"

namespace DX8
{

namespace D3D
{

// Upload one mip level's worth of pixels into a locked rect. Scalar only.
static void UploadLevel(const D3DLOCKED_RECT &Lock, const u8* buffer, int width, int height,
	int pitch, D3DFORMAT fmt, bool swap_r_b, bool bExpand)
{
	switch (fmt)
	{
	case D3DFMT_L8:
	case D3DFMT_A8:
	case D3DFMT_A4L4:
		{
			const u8 *pIn = buffer;
			for (int y = 0; y < height; y++)
			{
				u8* pBits = ((u8*)Lock.pBits + (y * Lock.Pitch));
				memcpy(pBits, pIn, width);
				pIn += pitch;
			}
		}
		break;
	case D3DFMT_R5G6B5:
		{
			const u16 *pIn = (const u16*)buffer;
			for (int y = 0; y < height; y++)
			{
				u16* pBits = (u16*)((u8*)Lock.pBits + (y * Lock.Pitch));
				memcpy(pBits, pIn, width * 2);
				pIn += pitch;
			}
		}
		break;
	case D3DFMT_A8L8:
		{
			if (bExpand)   // I8 -> A8L8 (duplicate the intensity into both channels)
			{
				const u8 *pIn = buffer;
				for (int y = 0; y < height; y++)
				{
					u8* pBits = ((u8*)Lock.pBits + (y * Lock.Pitch));
					for (int i = 0; i < width * 2; i += 2)
					{
						pBits[i] = pIn[i / 2];
						pBits[i + 1] = pIn[i / 2];
					}
					pIn += pitch;
				}
			}
			else            // IA8
			{
				const u16 *pIn = (const u16*)buffer;
				for (int y = 0; y < height; y++)
				{
					u16* pBits = (u16*)((u8*)Lock.pBits + (y * Lock.Pitch));
					memcpy(pBits, pIn, width * 2);
					pIn += pitch;
				}
			}
		}
		break;
	case D3DFMT_A8R8G8B8:
		{
			const u32* pIn = (const u32*)buffer;
			if (pitch * 4 == Lock.Pitch && !swap_r_b)
			{
				memcpy(Lock.pBits, buffer, Lock.Pitch * height);
			}
			else if (!swap_r_b)
			{
				for (int y = 0; y < height; y++)
				{
					u32 *pBits = (u32*)((u8*)Lock.pBits + (y * Lock.Pitch));
					memcpy(pBits, pIn, width * 4);
					pIn += pitch;
				}
			}
			else   // scalar RGBA -> BGRA (SSE1-safe; no SSE2 swizzle on Coppermine)
			{
				for (int y = 0; y < height; y++)
				{
					const u8 *pIn8 = (const u8 *)pIn;
					u8 *pBits = (u8 *)((u8*)Lock.pBits + (y * Lock.Pitch));
					for (int x = 0; x < width * 4; x += 4)
					{
						pBits[x + 0] = pIn8[x + 2];
						pBits[x + 1] = pIn8[x + 1];
						pBits[x + 2] = pIn8[x + 0];
						pBits[x + 3] = pIn8[x + 3];
					}
					pIn += pitch;
				}
			}
		}
		break;
	case D3DFMT_DXT1:
		memcpy(Lock.pBits, buffer, ((width + 3) / 4) * ((height + 3) / 4) * 8);
		break;
	default:
		ERROR_LOG(VIDEO, "D3D8: unhandled texture upload format %i", fmt);
	}
}

LPDIRECT3DTEXTURE8 CreateTexture2D(const u8* buffer, const int width, const int height, const int pitch, D3DFORMAT fmt, bool swap_r_b, int levels)
{
	LPDIRECT3DTEXTURE8 pTexture = NULL;

	bool bExpand = false;
	if (fmt == D3DFMT_A8P8) { fmt = D3DFMT_A8L8; bExpand = true; }

	// D3D8: no AUTOGENMIPMAP, no trailing pSharedHandle. One level for first-light.
	HRESULT hr = dev->CreateTexture(width, height, (levels > 0) ? levels : 1, 0, fmt, D3DPOOL_MANAGED, &pTexture);
	if (FAILED(hr) || !pTexture)
		return NULL;

	D3DLOCKED_RECT Lock;
	if (SUCCEEDED(pTexture->LockRect(0, &Lock, NULL, 0)))
	{
		UploadLevel(Lock, buffer, width, height, pitch, fmt, swap_r_b, bExpand);
		pTexture->UnlockRect(0);
	}
	return pTexture;
}

LPDIRECT3DTEXTURE8 CreateOnlyTexture2D(const int width, const int height, D3DFORMAT fmt)
{
	LPDIRECT3DTEXTURE8 pTexture = NULL;
	HRESULT hr = dev->CreateTexture(width, height, 1, 0, fmt, D3DPOOL_MANAGED, &pTexture);
	if (FAILED(hr))
		return NULL;
	return pTexture;
}

void ReplaceTexture2D(LPDIRECT3DTEXTURE8 pTexture, const u8* buffer, const int width, const int height, const int pitch, D3DFORMAT fmt, bool swap_r_b, int level)
{
	bool bExpand = false;
	if (fmt == D3DFMT_A8P8) { fmt = D3DFMT_A8L8; bExpand = true; }

	D3DLOCKED_RECT Lock;
	if (SUCCEEDED(pTexture->LockRect(level, &Lock, NULL, 0)))
	{
		UploadLevel(Lock, buffer, width, height, pitch, fmt, swap_r_b, bExpand);
		pTexture->UnlockRect(level);
	}
}

LPDIRECT3DTEXTURE8 CreateRenderTarget(const int width, const int height)
{
	LPDIRECT3DTEXTURE8 pTexture = NULL;
	HRESULT hr = dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTexture);
	if (FAILED(hr))
		return NULL;
	return pTexture;
}

LPDIRECT3DSURFACE8 CreateDepthStencilSurface(const int width, const int height)
{
	LPDIRECT3DSURFACE8 pSurface = NULL;
	// D3D8: CreateDepthStencilSurface(Width, Height, Format, MultiSample, ppSurface).
	HRESULT hr = dev->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &pSurface);
	if (FAILED(hr))
		return NULL;
	return pSurface;
}

}  // namespace D3D

}  // namespace DX8
