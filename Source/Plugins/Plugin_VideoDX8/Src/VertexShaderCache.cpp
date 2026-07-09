// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 VertexShaderCache — FIRST-LIGHT STUB. Like the pixel cache, the DX9
// backend generates HLSL and compiles it at runtime; NV2A uses vs.1.1-style
// vertex programs created via CreateVertexShader(decl, microcode, &handle, 0),
// coupled to the input declaration. Translating the GC vertex shader is task #14.
// SetShader() returns false for now so vFlush skips the draw and the FIFO drains.

#include <map>

#include "D3DBase.h"
#include "VertexShaderCache.h"
#include "VertexShaderGen.h"
#include "VertexShaderManager.h"

namespace DX8
{

VertexShaderCache::VSCache VertexShaderCache::vshaders;
const VertexShaderCache::VSCacheEntry *VertexShaderCache::last_entry;
VertexShaderUid VertexShaderCache::last_uid;
UidChecker<VertexShaderUid, VertexShaderCode> VertexShaderCache::vertex_uid_checker;

void VertexShaderCache::Init()
{
	last_entry = NULL;
}

void VertexShaderCache::Clear()
{
	for (VSCache::iterator iter = vshaders.begin(); iter != vshaders.end(); ++iter)
		iter->second.Destroy();
	vshaders.clear();
	vertex_uid_checker.Invalidate();
	last_entry = NULL;
}

void VertexShaderCache::Shutdown()
{
	Clear();
}

bool VertexShaderCache::SetShader(u32 components)
{
	(void)components;
	return false;   // TODO(task#14): GC vertex shader -> NV2A vertex program
}

bool VertexShaderCache::InsertByteCode(const VertexShaderUid &uid, const u8 *bytecode, int bytecodelen, bool activate)
{
	(void)uid; (void)bytecode; (void)bytecodelen; (void)activate;
	return false;
}

DWORD VertexShaderCache::GetSimpleVertexShader(int level) { (void)level; return 0; }
DWORD VertexShaderCache::GetClearVertexShader()           { return 0; }

std::string VertexShaderCache::GetCurrentShaderCode()
{
	return "";
}

}  // namespace DX8
