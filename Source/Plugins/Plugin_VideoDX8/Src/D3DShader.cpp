// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 shader compile layer. Xbox has no runtime HLSL compiler and its
// shaders are DWORD handles built from an NV2A vertex-program microcode + input
// declaration (CreateVertexShader(decl, func, &handle, usage)) or a register-
// combiner pixel definition (CreatePixelShader(def, &handle)). Dolphin's DX9
// backend hands us compiled HLSL bytecode, which is meaningless here — so for now
// these are stubs returning 0 (handle 0 = fixed-function). Wiring the actual
// GC TEV -> NV2A combiner / VS -> vs.1.1 translation is task #14.

#include <string>

#include "VideoConfig.h"
#include "D3DShader.h"

namespace DX8
{

namespace D3D
{

DWORD CreateVertexShaderFromByteCode(const u8 *bytecode, int len)
{
	(void)bytecode; (void)len;
	return 0;   // TODO(task#14): CreateVertexShader(decl, microcode, &h, 0)
}

bool CompileVertexShader(const char *code, int len, u8 **bytecode, int *bytecodelen)
{
	(void)code; (void)len;
	*bytecode = NULL;
	*bytecodelen = 0;
	return false;   // TODO(task#14): assemble NV2A vertex program from GC VS
}

DWORD CreatePixelShaderFromByteCode(const u8 *bytecode, int len)
{
	(void)bytecode; (void)len;
	return 0;   // TODO(task#14): CreatePixelShader(combiner def, &h)
}

bool CompilePixelShader(const char *code, int len, u8 **bytecode, int *bytecodelen)
{
	(void)code; (void)len;
	*bytecode = NULL;
	*bytecodelen = 0;
	return false;   // TODO(task#14): translate GC TEV stages -> register combiners
}

DWORD CompileAndCreateVertexShader(const char *code, int len)
{
	(void)code; (void)len;
	return 0;
}

DWORD CompileAndCreatePixelShader(const char* code, unsigned int len)
{
	(void)code; (void)len;
	return 0;
}

}  // namespace

}  // namespace DX8
