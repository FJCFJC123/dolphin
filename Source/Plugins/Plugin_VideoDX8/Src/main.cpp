// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 backend entry. Ported from the DX9 backend for the OG Xbox: no window
// (EmuWindow), no WX config dialog, no runtime adapter enumeration. The device is
// not created here — D3D::Init() borrows the single IDirect3DDevice8 that
// main_xbox.cpp created (xbox_d3d8_get_device()). Video_Prepare wires up the same
// VideoCommon Fifo/CommandProcessor/OpcodeDecoder path as the PC backend, which is
// what actually consumes the GX FIFO and un-hangs the guest.

#include "Common.h"
#include "Atomic.h"
#include "Thread.h"
#include "LogManager.h"

#include "MainBase.h"
#include "main.h"
#include "VideoConfig.h"
#include "Fifo.h"
#include "OpcodeDecoding.h"
#include "TextureCache.h"
#include "BPStructs.h"
#include "VertexManager.h"
#include "FramebufferManager.h"
#include "VertexLoaderManager.h"
#include "VertexShaderManager.h"
#include "PixelShaderManager.h"
#include "VertexShaderCache.h"
#include "PixelShaderCache.h"
#include "CommandProcessor.h"
#include "PixelEngine.h"
#include "OnScreenDisplay.h"
#include "D3DTexture.h"
#include "D3DUtil.h"
#include "VideoState.h"
#include "Render.h"
#include "DLCache.h"
#include "IndexGenerator.h"
#include "IniFile.h"
#include "Core.h"
#include "Host.h"

#include "ConfigManager.h"
#include "VideoBackend.h"
#include "PerfQuery.h"

namespace DX8
{

unsigned int VideoBackend::PeekMessages()
{
	// No host window on Xbox — nothing to pump.
	return TRUE;
}

void VideoBackend::UpdateFPSDisplay(const char *text)
{
	(void)text;   // no title bar on Xbox
}

std::string VideoBackend::GetName()
{
	return "DX8";
}

std::string VideoBackend::GetDisplayName()
{
	return "Direct3D8 (NV2A)";
}

void InitBackendInfo()
{
	// NV2A: single fixed adapter, register-combiner pixel path, vs.1.1 vertex path.
	// Report SM2.0-ish so VideoCommon's shared setup is happy; the real shader
	// generation is bypassed while the caches are stubbed (task #14).
	g_Config.backend_info.APIType = API_D3D9_SM20;
	g_Config.backend_info.bUseRGBATextures = false;
	g_Config.backend_info.bUseMinimalMipCount = true;
	g_Config.backend_info.bSupports3DVision = false;
	g_Config.backend_info.bSupportsPrimitiveRestart = false;
	g_Config.backend_info.bSupportsSeparateAlphaFunction = false;
	g_Config.backend_info.bSupportsDualSourceBlend = false;
	g_Config.backend_info.bSupportsFormatReinterpretation = true;
	g_Config.backend_info.bSupportsPixelLighting = false;
	g_Config.backend_info.bSupportsEarlyZ = false;

	g_Config.backend_info.Adapters.clear();
	g_Config.backend_info.Adapters.push_back("NVIDIA NV2A (Xbox)");

	g_Config.backend_info.AAModes.clear();
	g_Config.backend_info.AAModes.push_back("None");

	g_Config.backend_info.PPShaders.clear();
}

void VideoBackend::ShowConfig(void* parent)
{
	(void)parent;   // no config UI on Xbox
}

bool VideoBackend::Initialize(void *&window_handle)
{
	InitializeShared();
	InitBackendInfo();

	frameCount = 0;

	g_Config.Load((File::GetUserPath(D_CONFIG_IDX) + "gfx_dx8.ini").c_str());
	g_Config.GameIniLoad(SConfig::GetInstance().m_LocalCoreStartupParameter.m_strGameIniDefault.c_str(),
	                     SConfig::GetInstance().m_LocalCoreStartupParameter.m_strGameIniLocal.c_str());
	g_Config.UpdateProjectionHack();
	g_Config.VerifyValidity();
	UpdateActiveConfig();

	// Borrow the device main_xbox already created (NV2A = one device only).
	if (FAILED(D3D::Init()))
	{
		ERROR_LOG(VIDEO, "DX8: no shared D3D device available (xbox_d3d8_get_device returned NULL).");
		return false;
	}

	s_BackendInitialized = true;
	return true;
}

void VideoBackend::Video_Prepare()
{
	// Better be safe...
	s_efbAccessRequested = FALSE;
	s_FifoShuttingDown = FALSE;
	s_swapRequested = FALSE;

	// internal interfaces
	g_vertex_manager = new VertexManager;
	g_perf_query = new PerfQuery;
	g_renderer = new Renderer;
	g_texture_cache = new TextureCache;
	// VideoCommon
	BPInit();
	Fifo_Init();
	IndexGenerator::Init();
	VertexLoaderManager::Init();
	OpcodeDecoder_Init();
	VertexShaderManager::Init();
	PixelShaderManager::Init();
	CommandProcessor::Init();
	PixelEngine::Init();
	DLCache::Init();
	// Notify the core that the video backend is ready
	Host_Message(WM_USER_CREATE);
}

void VideoBackend::Shutdown()
{
	s_BackendInitialized = false;

	if (g_renderer)
	{
		s_efbAccessRequested = FALSE;
		s_FifoShuttingDown = FALSE;
		s_swapRequested = FALSE;

		// VideoCommon
		DLCache::Shutdown();
		Fifo_Shutdown();
		CommandProcessor::Shutdown();
		PixelShaderManager::Shutdown();
		VertexShaderManager::Shutdown();
		OpcodeDecoder_Shutdown();
		VertexLoaderManager::Shutdown();

		// internal interfaces
		PixelShaderCache::Shutdown();
		VertexShaderCache::Shutdown();
		delete g_texture_cache;
		delete g_renderer;
		delete g_perf_query;
		delete g_vertex_manager;
		g_renderer = NULL;
		g_texture_cache = NULL;
	}
	D3D::Shutdown();
}

void VideoBackend::Video_Cleanup() {
}

}
