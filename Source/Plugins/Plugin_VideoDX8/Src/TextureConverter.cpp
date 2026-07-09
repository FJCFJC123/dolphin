// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 TextureConverter — FIRST-LIGHT STUB. The DX9 version encodes the EFB
// to RAM (YUYV / GC copy formats) and decodes the XFB to a texture using dedicated
// shaders + render-to-texture. That whole path needs the register-combiner shaders
// (task #14) and the D3D8 render-target plumbing (task #15). It's only reached
// through the (currently stubbed) EFB-copy / XFB paths, so stubbing it is safe and
// doesn't affect draining the FIFO. EncodeToRamFromTexture reports 0 bytes encoded.

#include "TextureConverter.h"

namespace DX8
{

namespace TextureConverter
{

void Init() {}
void Shutdown() {}

void EncodeToRamYUYV(LPDIRECT3DTEXTURE8 srcTexture, const TargetRectangle& sourceRc,
	u8* destAddr, int dstWidth, int dstHeight, float Gamma)
{
	(void)srcTexture; (void)sourceRc; (void)destAddr;
	(void)dstWidth; (void)dstHeight; (void)Gamma;
	// TODO(task#15)
}

void DecodeToTexture(u32 xfbAddr, int srcWidth, int srcHeight, LPDIRECT3DTEXTURE8 destTexture)
{
	(void)xfbAddr; (void)srcWidth; (void)srcHeight; (void)destTexture;
	// TODO(task#15)
}

int EncodeToRamFromTexture(u32 address, LPDIRECT3DTEXTURE8 source_texture, u32 SourceW, u32 SourceH,
	bool bFromZBuffer, bool bIsIntensityFmt, u32 copyfmt, int bScaleByHalf, const EFBRectangle& source)
{
	(void)address; (void)source_texture; (void)SourceW; (void)SourceH;
	(void)bFromZBuffer; (void)bIsIntensityFmt; (void)copyfmt; (void)bScaleByHalf; (void)source;
	return 0;   // TODO(task#15)
}

}  // namespace TextureConverter

}  // namespace DX8
