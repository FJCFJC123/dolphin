// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "D3DBase.h"

// NV2A note: D3D8 shaders are DWORD handles, not IDirect3DVertexShader9/
// PixelShader9 objects, and there is no runtime HLSL compiler (D3DXCompileShader)
// on Xbox. Dolphin's generated HLSL therefore can't be consumed as-is; the real
// path (task #14) assembles NV2A vertex programs + register-combiner pixel defs.
// Until then these are stubs returning 0 (fixed-function) so the backend links
// and the GX FIFO drains.

namespace DX8
{

namespace D3D
{
	DWORD CreateVertexShaderFromByteCode(const u8 *bytecode, int len);
	DWORD CreatePixelShaderFromByteCode(const u8 *bytecode, int len);

	// The returned bytecode buffers should be delete[]-d.
	bool CompileVertexShader(const char *code, int len, u8 **bytecode, int *bytecodelen);
	bool CompilePixelShader(const char *code, int len, u8 **bytecode, int *bytecodelen);

	// Utility functions
	DWORD CompileAndCreateVertexShader(const char *code, int len);
	DWORD CompileAndCreatePixelShader(const char *code, unsigned int len);
}

}  // namespace DX8
