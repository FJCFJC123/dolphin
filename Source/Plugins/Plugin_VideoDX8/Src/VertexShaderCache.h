// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "D3DBase.h"

#include <map>
#include <string>

#include "D3DBase.h"
#include "VertexShaderGen.h"

namespace DX8
{

class VertexShaderCache
{
private:
	struct VSCacheEntry
	{
		DWORD shader;   // NV2A vertex-program handle

		std::string code;

		VSCacheEntry() : shader(0) {}
		void Destroy()
		{
			if (shader && D3D::dev)
				D3D::dev->DeleteVertexShader(shader);
			shader = 0;
		}
	};

	typedef std::map<VertexShaderUid, VSCacheEntry> VSCache;

	static VSCache vshaders;
	static const VSCacheEntry *last_entry;
	static VertexShaderUid last_uid;

	static UidChecker<VertexShaderUid,VertexShaderCode> vertex_uid_checker;

	static void Clear();

public:
	static void Init();
	static void Shutdown();
	static bool SetShader(u32 components);
	static DWORD GetSimpleVertexShader(int level);
	static DWORD GetClearVertexShader();
	static bool InsertByteCode(const VertexShaderUid &uid, const u8 *bytecode, int bytecodelen, bool activate);

	static std::string GetCurrentShaderCode();
};

}  // namespace DX8