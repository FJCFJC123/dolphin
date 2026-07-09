// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 PixelShaderCache — FIRST-LIGHT STUB. The DX9 backend generates HLSL
// from the GC TEV state (PixelShaderGen) and compiles it with D3DXCompileShader;
// neither exists on Xbox, where pixel shading is 8 register combiners + a final
// combiner. Translating TEV -> combiners is task #14. Until then SetShader()
// returns false, which makes VertexManager::vFlush skip the draw (goto shader_fail)
// while still consuming the FIFO — exactly what we need to un-hang the guest.

#include <map>

#include "D3DBase.h"
#include "PixelShaderCache.h"
#include "PixelShaderGen.h"
#include "PixelShaderManager.h"

namespace DX8
{

PixelShaderCache::PSCache PixelShaderCache::PixelShaders;
const PixelShaderCache::PSCacheEntry *PixelShaderCache::last_entry;
PixelShaderUid PixelShaderCache::last_uid;
UidChecker<PixelShaderUid, PixelShaderCode> PixelShaderCache::pixel_uid_checker;

tevhash GetCurrentTEV()
{
	return 0;
}

void PixelShaderCache::Init()
{
	last_entry = NULL;
}

void PixelShaderCache::Clear()
{
	for (PSCache::iterator iter = PixelShaders.begin(); iter != PixelShaders.end(); ++iter)
		iter->second.Destroy();
	PixelShaders.clear();
	pixel_uid_checker.Invalidate();
	last_entry = NULL;
}

void PixelShaderCache::Shutdown()
{
	Clear();
}

bool PixelShaderCache::SetShader(DSTALPHA_MODE dstAlphaMode, u32 components)
{
	(void)dstAlphaMode; (void)components;
	return false;   // TODO(task#14): GC TEV -> NV2A register combiners
}

bool PixelShaderCache::InsertByteCode(const PixelShaderUid &uid, const u8 *bytecode, int bytecodelen, bool activate)
{
	(void)uid; (void)bytecode; (void)bytecodelen; (void)activate;
	return false;
}

DWORD PixelShaderCache::GetColorMatrixProgram(int SSAAMode)                    { (void)SSAAMode; return 0; }
DWORD PixelShaderCache::GetColorCopyProgram(int SSAAMode)                      { (void)SSAAMode; return 0; }
DWORD PixelShaderCache::GetDepthMatrixProgram(int SSAAMode, bool depthConv)    { (void)SSAAMode; (void)depthConv; return 0; }
DWORD PixelShaderCache::GetClearProgram()                                     { return 0; }
DWORD PixelShaderCache::ReinterpRGBA6ToRGB8()                                 { return 0; }
DWORD PixelShaderCache::ReinterpRGB8ToRGBA6()                                 { return 0; }

}  // namespace DX8
