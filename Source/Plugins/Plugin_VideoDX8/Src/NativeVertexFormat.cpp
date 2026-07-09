// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 native vertex format. D3D8 has no D3DVERTEXELEMENT9 /
// CreateVertexDeclaration; the vertex layout is either an FVF or a vertex-shader
// declaration token stream coupled to the vertex program. Dolphin keeps the
// declaration (this file) and the vertex program (VertexShaderCache) separate,
// but NV2A binds them together — reconciling that is task #13. For the first-light
// milestone we derive a best-effort FVF from the portable declaration and record
// the stride so the geometry path compiles and the FIFO drains.

#include "D3DBase.h"

#include "CPMemory.h"
#include "NativeVertexFormat.h"
#include "VertexManager.h"

namespace DX8
{

class D3DVertexFormat : public NativeVertexFormat
{
	DWORD m_fvf;

public:
	D3DVertexFormat() : m_fvf(0) {}
	~D3DVertexFormat() {}
	virtual void Initialize(const PortableVertexDeclaration &_vtx_decl);
	virtual void SetupVertexPointers();
};

NativeVertexFormat* VertexManager::CreateNativeVertexFormat()
{
	return new D3DVertexFormat();
}

void D3DVertexFormat::Initialize(const PortableVertexDeclaration &_vtx_decl)
{
	vertex_stride = _vtx_decl.stride;

	// Best-effort FVF: position is always float3 at offset 0. TODO(task#13): this
	// must become an NV2A vertex-shader declaration coupled to the vertex program
	// (the GC layout's arbitrary offsets don't match FVF's fixed packing).
	DWORD fvf = D3DFVF_XYZ;

	bool has_normal = false;
	for (int i = 0; i < 3; i++)
		if (_vtx_decl.normal_offset[i] > 0) has_normal = true;
	if (has_normal)
		fvf |= D3DFVF_NORMAL;

	if (_vtx_decl.color_offset[0] > 0) fvf |= D3DFVF_DIFFUSE;
	if (_vtx_decl.color_offset[1] > 0) fvf |= D3DFVF_SPECULAR;

	int ntex = 0;
	for (int i = 0; i < 8; i++)
		if (_vtx_decl.texcoord_offset[i] > 0) ntex++;
	fvf |= (ntex << D3DFVF_TEXCOUNT_SHIFT);

	m_fvf = fvf;
}

void D3DVertexFormat::SetupVertexPointers()
{
	// On NV2A the "declaration" is the vertex shader; set the FVF as the
	// fixed-function vertex format for now (VertexShaderCache overrides with a
	// real program handle once task #14 lands).
	DX8::D3D::SetVertexShader(m_fvf);
}

} // namespace DX8
