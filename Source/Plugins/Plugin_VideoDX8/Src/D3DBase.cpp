// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// OG Xbox (NV2A) D3D8 port of D3DBase. See D3DBase.h for the porting notes.
// The device is borrowed from main_xbox via xbox_d3d8_get_device(); this file
// never creates or resets it.

#include "D3DBase.h"
#include "VideoConfig.h"
#include "Render.h"
#include "VideoCommon.h"

// C bridge to the single device owned by main_xbox / xbox_d3d8.cpp.
extern "C" void *xbox_d3d8_get_device(void);

namespace DX8
{

namespace D3D
{

LPDIRECT3DDEVICE8  dev = NULL;          // borrowed rendering device
LPDIRECT3DSURFACE8 back_buffer = NULL;
LPDIRECT3DSURFACE8 back_buffer_z = NULL;
D3DCAPS8 caps;

static int xres, yres;

bool bFrameInProgress = false;

#define MAX_ADAPTERS 1
static Adapter adapters[MAX_ADAPTERS];
static int numAdapters;
static int cur_adapter;

// Value caches for state filtering (sizes preserved from the DX9 backend).
const int MaxStreamSources = 16;
const int MaxTextureStages = 9;
const int MaxRenderStates = 210 + 46;
const int MaxTextureTypes = 33;
const int MaxSamplerSize = 13;
const int MaxSamplerTypes = 15;
static bool m_RenderStatesSet[MaxRenderStates];
static DWORD m_RenderStates[MaxRenderStates];
static bool m_RenderStatesChanged[MaxRenderStates];

static DWORD m_TextureStageStates[MaxTextureStages][MaxTextureTypes];
static bool m_TextureStageStatesSet[MaxTextureStages][MaxTextureTypes];
static bool m_TextureStageStatesChanged[MaxTextureStages][MaxTextureTypes];

static DWORD m_SamplerStates[MaxSamplerSize][MaxSamplerTypes];
static bool m_SamplerStatesSet[MaxSamplerSize][MaxSamplerTypes];
static bool m_SamplerStatesChanged[MaxSamplerSize][MaxSamplerTypes];

static IDirect3DBaseTexture8 *m_Textures[16];
static DWORD m_VtxDecl;
static bool m_VtxDeclChanged;
static DWORD m_PixelShader;
static bool m_PixelShaderChanged;
static DWORD m_VertexShader;
static bool m_VertexShaderChanged;
struct StreamSourceDescriptor
{
	IDirect3DVertexBuffer8* pStreamData;
	UINT OffsetInBytes;
	UINT Stride;
};
static StreamSourceDescriptor m_stream_sources[MaxStreamSources];
static bool m_stream_sources_Changed[MaxStreamSources];
static LPDIRECT3DINDEXBUFFER8 m_index_buffer;
static bool m_index_buffer_Changed;

// D3D8 folds sampler filtering/addressing into the texture-stage states. Map a
// D3DSAMPLERSTATETYPE (D3D9-style value) to the matching D3DTEXTURESTAGESTATETYPE.
static D3DTEXTURESTAGESTATETYPE SamplerToStage(D3DSAMPLERSTATETYPE Type)
{
	switch (Type)
	{
	case D3DSAMP_ADDRESSU:      return D3DTSS_ADDRESSU;
	case D3DSAMP_ADDRESSV:      return D3DTSS_ADDRESSV;
	case D3DSAMP_ADDRESSW:      return D3DTSS_ADDRESSW;
	case D3DSAMP_BORDERCOLOR:   return D3DTSS_BORDERCOLOR;
	case D3DSAMP_MAGFILTER:     return D3DTSS_MAGFILTER;
	case D3DSAMP_MINFILTER:     return D3DTSS_MINFILTER;
	case D3DSAMP_MIPFILTER:     return D3DTSS_MIPFILTER;
	case D3DSAMP_MIPMAPLODBIAS: return D3DTSS_MIPMAPLODBIAS;
	case D3DSAMP_MAXMIPLEVEL:   return D3DTSS_MAXMIPLEVEL;
	case D3DSAMP_MAXANISOTROPY: return D3DTSS_MAXANISOTROPY;
	default:                    return D3DTSS_MINFILTER;
	}
}

int GetNumAdapters() { return numAdapters; }
const Adapter &GetAdapter(int i) { return adapters[i]; }
const Adapter &GetCurAdapter() { return adapters[cur_adapter]; }

bool IsATIDevice()   { return false; }  // NV2A
bool IsIntelDevice() { return false; }
bool CanUseINTZ()    { return false; }

void ShowD3DError(HRESULT err) { (void)err; }

HRESULT Init()
{
	// Borrow the single device created by main_xbox (NV2A = one device only).
	dev = (LPDIRECT3DDEVICE8)xbox_d3d8_get_device();
	if (!dev)
		return E_FAIL;

	dev->GetDeviceCaps(&caps);

	// Grab the current render target / depth as our "back buffer".
	back_buffer = NULL;
	back_buffer_z = NULL;
	dev->GetRenderTarget(&back_buffer);
	if (dev->GetDepthStencilSurface(&back_buffer_z) == D3DERR_NOTFOUND)
		back_buffer_z = NULL;

	D3DSURFACE_DESC desc;
	if (back_buffer && SUCCEEDED(back_buffer->GetDesc(&desc)))
	{
		xres = desc.Width;
		yres = desc.Height;
	}
	else
	{
		xres = 640; yres = 480;
	}

	strcpy(adapters[0].ident.Description, "NVIDIA NV2A (Xbox)");
	adapters[0].aa_levels.clear();
	adapters[0].resolutions.clear();
	adapters[0].supports_alpha_to_coverage = false;
	adapters[0].supports_intz = false;
	adapters[0].supports_rawz = false;
	adapters[0].supports_resz = false;
	adapters[0].supports_null = false;
	numAdapters = 1;
	cur_adapter = 0;
	return S_OK;
}

HRESULT Create(int adapter, HWND wnd, int _resolution, int aa_mode, bool auto_depth)
{
	(void)adapter; (void)wnd; (void)_resolution; (void)aa_mode; (void)auto_depth;
	return Init();
}

void Close()
{
	// We don't own the device; just drop our borrowed references.
	if (back_buffer_z) { back_buffer_z->Release(); back_buffer_z = NULL; }
	if (back_buffer)   { back_buffer->Release();   back_buffer = NULL; }
	dev = NULL;
}

void Shutdown()
{
	Close();
}

void Reset()
{
	// Single shared device on Xbox — no lost-device reset. Just re-apply state.
	if (dev)
		ApplyCachedState();
}

void EnableAlphaToCoverage() {}

int GetBackBufferWidth()  { return xres; }
int GetBackBufferHeight() { return yres; }
LPDIRECT3DSURFACE8 GetBackBufferSurface()      { return back_buffer; }
LPDIRECT3DSURFACE8 GetBackBufferDepthSurface() { return back_buffer_z; }
const D3DCAPS8 &GetCaps() { return caps; }

const char *PixelShaderVersionString()  { return "ps.1.1"; }   // NV2A register combiners
const char *VertexShaderVersionString() { return "vs.1.1"; }   // NV2A vertex programs

bool FixTextureSize(int& width, int& height) { (void)width; (void)height; return false; }
bool CheckTextureSupport(DWORD usage, D3DFORMAT tex_format) { (void)usage; (void)tex_format; return true; }
bool CheckDepthStencilSupport(D3DFORMAT target_format, D3DFORMAT depth_format) { (void)target_format; (void)depth_format; return true; }
D3DFORMAT GetSupportedDepthTextureFormat() { return D3DFMT_D24S8; }
D3DFORMAT GetSupportedDepthSurfaceFormat(D3DFORMAT target_format) { (void)target_format; return D3DFMT_D24S8; }

bool BeginFrame()
{
	if (bFrameInProgress)
		return false;
	bFrameInProgress = true;
	if (dev)
	{
		dev->BeginScene();
		return true;
	}
	return false;
}

void EndFrame()
{
	if (!bFrameInProgress)
		return;
	bFrameInProgress = false;
	if (dev)
		dev->EndScene();
}

void Present()
{
	// main_xbox's loop drives the actual Present (xbox_d3d8_pump) so the overlay
	// and the emu present stay serialized on g_devLock. The Renderer's swap path
	// stages the frame; here we no-op to avoid a double Present race.
}

void ApplyCachedState()
{
	for (int stage = 0; stage < MaxTextureStages; stage++)
		for (int type = 0; type < MaxTextureTypes; type++)
			if (m_TextureStageStatesSet[stage][type])
				dev->SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)type, m_TextureStageStates[stage][type]);

	for (int rs = 0; rs < MaxRenderStates; rs++)
		if (m_RenderStatesSet[rs])
			dev->SetRenderState((D3DRENDERSTATETYPE)rs, m_RenderStates[rs]);

	// Wipe volatile copies so no stale binding lingers.
	memset(m_Textures, 0, sizeof(m_Textures));
	m_VtxDecl = 0;
	m_PixelShader = 0;
	m_VertexShader = 0;
	memset(m_stream_sources, 0, sizeof(m_stream_sources));
	m_index_buffer = NULL;
	m_VtxDeclChanged = false;
	m_PixelShaderChanged = false;
	m_VertexShaderChanged = false;
	memset(m_stream_sources_Changed, 0, sizeof(m_stream_sources_Changed));
	m_index_buffer_Changed = false;
}

void SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture)
{
	if (m_Textures[Stage] != pTexture)
	{
		m_Textures[Stage] = pTexture;
		dev->SetTexture(Stage, pTexture);
	}
}

void RefreshRenderState(D3DRENDERSTATETYPE State)
{
	if (m_RenderStatesSet[State] && m_RenderStatesChanged[State])
	{
		dev->SetRenderState(State, m_RenderStates[State]);
		m_RenderStatesChanged[State] = false;
	}
}

void SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
	if (m_RenderStates[State] != Value || !m_RenderStatesSet[State])
	{
		m_RenderStates[State] = Value;
		m_RenderStatesSet[State] = true;
		m_RenderStatesChanged[State] = false;
		dev->SetRenderState(State, Value);
	}
}

void ChangeRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
	if (m_RenderStates[State] != Value || !m_RenderStatesSet[State])
	{
		m_RenderStatesChanged[State] = m_RenderStatesSet[State];
		dev->SetRenderState(State, Value);
	}
	else
	{
		m_RenderStatesChanged[State] = false;
	}
}

void SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	if (m_TextureStageStates[Stage][Type] != Value || !m_TextureStageStatesSet[Stage][Type])
	{
		m_TextureStageStates[Stage][Type] = Value;
		m_TextureStageStatesSet[Stage][Type] = true;
		m_TextureStageStatesChanged[Stage][Type] = false;
		dev->SetTextureStageState(Stage, Type, Value);
	}
}

void RefreshTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type)
{
	if (m_TextureStageStatesSet[Stage][Type] && m_TextureStageStatesChanged[Stage][Type])
	{
		dev->SetTextureStageState(Stage, Type, m_TextureStageStates[Stage][Type]);
		m_TextureStageStatesChanged[Stage][Type] = false;
	}
}

void ChangeTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	if (m_TextureStageStates[Stage][Type] != Value || !m_TextureStageStatesSet[Stage][Type])
	{
		m_TextureStageStatesChanged[Stage][Type] = m_TextureStageStatesSet[Stage][Type];
		dev->SetTextureStageState(Stage, Type, Value);
	}
	else
	{
		m_TextureStageStatesChanged[Stage][Type] = false;
	}
}

// Sampler states are texture-stage states on D3D8: translate + forward through
// the texture-stage cache so filtering/addressing dedupes correctly.
void SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
	SetTextureStageState(Sampler, SamplerToStage(Type), Value);
}

void RefreshSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type)
{
	RefreshTextureStageState(Sampler, SamplerToStage(Type));
}

void ChangeSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
	ChangeTextureStageState(Sampler, SamplerToStage(Type), Value);
}

// On D3D8 the vertex declaration is carried by the vertex shader handle, so
// there is no separate SetVertexDeclaration device call — track it only.
void RefreshVertexDeclaration()
{
	m_VtxDeclChanged = false;
}

void SetVertexDeclaration(DWORD decl)
{
	m_VtxDecl = decl;
	m_VtxDeclChanged = false;
}

void ChangeVertexDeclaration(DWORD decl)
{
	if (decl != m_VtxDecl)
		m_VtxDeclChanged = true;
}

void ChangeVertexShader(DWORD shader)
{
	if (shader != m_VertexShader)
	{
		dev->SetVertexShader(shader);
		m_VertexShaderChanged = true;
	}
}

void RefreshVertexShader()
{
	if (m_VertexShaderChanged)
	{
		dev->SetVertexShader(m_VertexShader);
		m_VertexShaderChanged = false;
	}
}

void SetVertexShader(DWORD shader)
{
	if (shader != m_VertexShader)
	{
		dev->SetVertexShader(shader);
		m_VertexShader = shader;
		m_VertexShaderChanged = false;
	}
}

void RefreshPixelShader()
{
	if (m_PixelShaderChanged)
	{
		dev->SetPixelShader(m_PixelShader);
		m_PixelShaderChanged = false;
	}
}

void SetPixelShader(DWORD shader)
{
	if (shader != m_PixelShader)
	{
		dev->SetPixelShader(shader);
		m_PixelShader = shader;
		m_PixelShaderChanged = false;
	}
}

void ChangePixelShader(DWORD shader)
{
	if (shader != m_PixelShader)
	{
		dev->SetPixelShader(shader);
		m_PixelShaderChanged = true;
	}
}

// D3D8 SetStreamSource has no per-call OffsetInBytes (the offset is baked into
// the vertex-buffer lock/base). Track it for the cache but drop it in the call.
void SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT OffsetInBytes, UINT Stride)
{
	if (m_stream_sources[StreamNumber].OffsetInBytes != OffsetInBytes
		|| m_stream_sources[StreamNumber].pStreamData != pStreamData
		|| m_stream_sources[StreamNumber].Stride != Stride)
	{
		m_stream_sources[StreamNumber].OffsetInBytes = OffsetInBytes;
		m_stream_sources[StreamNumber].pStreamData = pStreamData;
		m_stream_sources[StreamNumber].Stride = Stride;
		dev->SetStreamSource(StreamNumber, pStreamData, Stride);
		m_stream_sources_Changed[StreamNumber] = false;
	}
}

void ChangeStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT OffsetInBytes, UINT Stride)
{
	if (m_stream_sources[StreamNumber].OffsetInBytes != OffsetInBytes
		|| m_stream_sources[StreamNumber].pStreamData != pStreamData
		|| m_stream_sources[StreamNumber].Stride != Stride)
	{
		dev->SetStreamSource(StreamNumber, pStreamData, Stride);
		m_stream_sources_Changed[StreamNumber] = true;
	}
}

void RefreshStreamSource(UINT StreamNumber)
{
	if (m_stream_sources_Changed[StreamNumber])
	{
		dev->SetStreamSource(
			StreamNumber,
			m_stream_sources[StreamNumber].pStreamData,
			m_stream_sources[StreamNumber].Stride);
		m_stream_sources_Changed[StreamNumber] = false;
	}
}

// D3D8 SetIndices takes a BaseVertexIndex (D3D9 folded it into DrawIndexedPrimitive).
void SetIndices(LPDIRECT3DINDEXBUFFER8 pIndexData)
{
	if (pIndexData != m_index_buffer)
	{
		m_index_buffer = pIndexData;
		dev->SetIndices(pIndexData, 0);
		m_index_buffer_Changed = false;
	}
}

void ChangeIndices(LPDIRECT3DINDEXBUFFER8 pIndexData)
{
	if (pIndexData != m_index_buffer)
	{
		dev->SetIndices(pIndexData, 0);
		m_index_buffer_Changed = true;
	}
}

void RefreshIndices()
{
	if (m_index_buffer_Changed)
	{
		dev->SetIndices(m_index_buffer, 0);
		m_index_buffer_Changed = false;
	}
}

}  // namespace D3D

}  // namespace DX8
