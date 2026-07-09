// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common.h"
#include "LinearDiskCache.h"
#include "D3DBase.h"

#include <map>

#include "PixelShaderGen.h"
#include "VertexShaderGen.h"

namespace DX8
{

typedef u32 tevhash;

tevhash GetCurrentTEV();

class PixelShaderCache
{
private:
	struct PSCacheEntry
	{
		DWORD shader;   // NV2A pixel-shader (register-combiner) handle
		bool owns_shader;

		std::string code;

		PSCacheEntry() : shader(0), owns_shader(true) {}
		void Destroy()
		{
			if (shader && owns_shader && D3D::dev)
				D3D::dev->DeletePixelShader(shader);
			shader = 0;
		}
	};

	typedef std::map<PixelShaderUid, PSCacheEntry> PSCache;

	static PSCache PixelShaders;
	static const PSCacheEntry *last_entry;
	static PixelShaderUid last_uid;
	static UidChecker<PixelShaderUid,PixelShaderCode> pixel_uid_checker;

	static void Clear();

public:
	static void Init();
	static void Shutdown();
	static bool SetShader(DSTALPHA_MODE dstAlphaMode, u32 componets);
	static bool InsertByteCode(const PixelShaderUid &uid, const u8 *bytecode, int bytecodelen, bool activate);
	static DWORD GetColorMatrixProgram(int SSAAMode);
	static DWORD GetColorCopyProgram(int SSAAMode);
	static DWORD GetDepthMatrixProgram(int SSAAMode, bool depthConversion);
	static DWORD GetClearProgram();
	static DWORD ReinterpRGBA6ToRGB8();
	static DWORD ReinterpRGB8ToRGBA6();
};

}  // namespace DX8