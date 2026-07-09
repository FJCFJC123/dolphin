// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// OG Xbox (NV2A) D3D8 port of the DX9 D3DBase. The device is NOT created here —
// main_xbox.cpp owns the single IDirect3DDevice8 and D3D::Init() borrows it via
// xbox_d3d8_get_device() (NV2A allows only one device). Adapter/AA/resolution/
// INTZ/D3DX machinery from the PC DX9 backend is dropped; only the device access
// + the render/sampler/texture-stage/shader state cache the rest of the backend
// depends on is kept. Shader and vertex-declaration handles are DWORDs on D3D8
// (there are no IDirect3DVertexShader9/PixelShader9/VertexDeclaration9 objects).
#pragma once

#include <vector>
#include <set>

// xtl.h is force-included (vcxproj /FI) ahead of Common.h so RXDK's _Interlocked
// intrinsics don't collide (C2733). All D3D8 types come from there.
#include "Common.h"

// Xbox D3D8 predates samplers (filtering/addressing are texture-stage states).
// The DX9-derived backend code speaks D3DSAMP_*, so define the enum here with the
// D3D9 values and translate to D3DTSS_* inside D3DBase (SamplerToStage).
#ifndef D3DSAMP_MINFILTER
typedef enum _D3DSAMPLERSTATETYPE
{
	D3DSAMP_ADDRESSU      = 1,
	D3DSAMP_ADDRESSV      = 2,
	D3DSAMP_ADDRESSW      = 3,
	D3DSAMP_BORDERCOLOR   = 4,
	D3DSAMP_MAGFILTER     = 5,
	D3DSAMP_MINFILTER     = 6,
	D3DSAMP_MIPFILTER     = 7,
	D3DSAMP_MIPMAPLODBIAS = 8,
	D3DSAMP_MAXMIPLEVEL   = 9,
	D3DSAMP_MAXANISOTROPY = 10,
	D3DSAMP_FORCE_DWORD   = 0x7fffffff,
} D3DSAMPLERSTATETYPE;
#endif

// GC/DX9 texture formats and dynamic-lock flags that Xbox D3D8 doesn't declare.
// Placeholder format values (high, outside the Xbox D3DFORMAT enum) keep the
// DX9-derived format switches compiling; A8P8 is remapped to A8L8 at upload time
// and A4L4 is inert until the real texture port (task #15). The Xbox has no
// dynamic vertex/index buffers, so the discard/no-overwrite lock hints are 0.
#ifndef D3DFMT_A4L4
#define D3DFMT_A4L4 ((D3DFORMAT)0xE1)
#endif
#ifndef D3DFMT_A8P8
#define D3DFMT_A8P8 ((D3DFORMAT)0xE2)
#endif
#ifndef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0
#endif
#ifndef D3DLOCK_NOOVERWRITE
#define D3DLOCK_NOOVERWRITE 0
#endif
#ifndef D3DUSAGE_DYNAMIC
#define D3DUSAGE_DYNAMIC 0
#endif

namespace DX8
{

namespace D3D
{

bool IsATIDevice();
bool IsIntelDevice();
HRESULT Init();      // borrows the device from xbox_d3d8
HRESULT Create(int adapter, HWND wnd, int resolution, int aa_mode, bool auto_depth);
void Close();
void Shutdown();

// Direct access to the (shared) device.
extern LPDIRECT3DDEVICE8 dev;
extern bool bFrameInProgress;

void Reset();
bool BeginFrame();
void EndFrame();
void Present();
bool CanUseINTZ();

int GetBackBufferWidth();
int GetBackBufferHeight();
LPDIRECT3DSURFACE8 GetBackBufferSurface();
LPDIRECT3DSURFACE8 GetBackBufferDepthSurface();
const D3DCAPS8 &GetCaps();
const char *PixelShaderVersionString();
const char *VertexShaderVersionString();
void ShowD3DError(HRESULT err);

// returns true if size was changed
bool FixTextureSize(int& width, int& height);

// returns true if format is supported
bool CheckTextureSupport(DWORD usage, D3DFORMAT tex_format);
bool CheckDepthStencilSupport(D3DFORMAT target_format, D3DFORMAT depth_format);

D3DFORMAT GetSupportedDepthTextureFormat();
D3DFORMAT GetSupportedDepthSurfaceFormat(D3DFORMAT target_format);

// The following are "filtered" versions of the corresponding D3Ddev-> functions.
void SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture);
void SetRenderState(D3DRENDERSTATETYPE State, DWORD Value);
void RefreshRenderState(D3DRENDERSTATETYPE State);
void ChangeRenderState(D3DRENDERSTATETYPE State, DWORD Value);

void SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
void RefreshTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type);
void ChangeTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);

// D3D8 has no separate sampler-state object; sampler filtering/addressing live in
// the texture-stage states. These forward Sampler->Stage so the DX9-derived
// callers compile and behave the same.
void SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value);
void RefreshSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type);
void ChangeSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value);

// On D3D8 the vertex "declaration" is baked into the vertex shader handle. These
// keep the cache shape but the DWORD is either an FVF code or a shader handle.
void RefreshVertexDeclaration();
void SetVertexDeclaration(DWORD decl);
void ChangeVertexDeclaration(DWORD decl);

void RefreshVertexShader();
void SetVertexShader(DWORD shader);
void ChangeVertexShader(DWORD shader);

void RefreshPixelShader();
void SetPixelShader(DWORD shader);
void ChangePixelShader(DWORD shader);

void SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT OffsetInBytes, UINT Stride);
void ChangeStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT OffsetInBytes, UINT Stride);
void RefreshStreamSource(UINT StreamNumber);

void SetIndices(LPDIRECT3DINDEXBUFFER8 pIndexData);
void ChangeIndices(LPDIRECT3DINDEXBUFFER8 pIndexData);
void RefreshIndices();

void ApplyCachedState();

void EnableAlphaToCoverage();

struct Resolution
{
	char name[32];
	int xres;
	int yres;
	std::set<D3DFORMAT> bitdepths;
	std::set<int> refreshes;
};

struct AALevel
{
	AALevel(const char *n, D3DMULTISAMPLE_TYPE m, int q) {
		strncpy(name, n, 32);
		name[31] = '\0';
		ms_setting = m;
		qual_setting = q;
	}
	char name[32];
	D3DMULTISAMPLE_TYPE ms_setting;
	int qual_setting;
};

// Xbox is a single fixed adapter; only .ident.Description + aa_levels are read.
struct Adapter
{
	struct { char Description[512]; } ident;
	std::vector<Resolution> resolutions;
	std::vector<AALevel> aa_levels;
	bool supports_alpha_to_coverage;
	bool supports_intz;
	bool supports_rawz;
	bool supports_resz;
	bool supports_null;
};

const Adapter &GetAdapter(int i);
const Adapter &GetCurAdapter();
int GetNumAdapters();

}  // namespace

}  // namespace DX8
