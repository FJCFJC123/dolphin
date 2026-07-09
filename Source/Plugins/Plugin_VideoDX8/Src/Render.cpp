// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// NV2A/D3D8 Renderer — FIRST-LIGHT MINIMAL. The DX9 Renderer is ~1400 lines of EFB
// emulation, GC->D3D render-state translation, EFB access (peek/poke), XFB present
// and screenshotting. Most of that depends on the register-combiner shaders
// (task #14) and full EFB render-target plumbing (task #15). For the first-light
// milestone the Renderer only needs to (a) construct without crashing so
// Video_Prepare succeeds and the FIFO starts draining, and (b) complete each Swap
// so the guest's XFB-swap request finishes and emulation keeps advancing. Real
// state translation + XFB blit are restored under task #15. The actual Present is
// driven by main_xbox's xbox_d3d8_pump() on the main thread (serialized on
// g_devLock), so Swap here only does the swap bookkeeping.

#include "Common.h"
#include "Statistics.h"
#include "Host.h"
#include "Core.h"
#include "VideoConfig.h"
#include "main.h"
#include "Render.h"
#include "FramebufferManager.h"
#include "PixelShaderCache.h"
#include "VertexShaderCache.h"
#include "D3DUtil.h"
#include "FPSCounter.h"
#include "OnScreenDisplay.h"
#include "Fifo.h"          // g_bSkipCurrentFrame

namespace DX8
{

static void SetupDeviceObjects()
{
	g_framebuffer_manager = new FramebufferManager;
	D3D::font.Init();
	VertexShaderCache::Init();
	PixelShaderCache::Init();
}

static void TeardownDeviceObjects()
{
	delete g_framebuffer_manager;
	g_framebuffer_manager = NULL;
	D3D::font.Shutdown();
}

Renderer::Renderer()
{
	InitFPSCounter();

	int x, y, w_temp, h_temp;
	Host_GetRenderWindowSize(x, y, w_temp, h_temp);

	// Device is already created by main_xbox; D3D::Init() (main.cpp) borrowed it.
	D3D::Create(0, NULL, 0, 0, false);

	s_backbuffer_width = D3D::GetBackBufferWidth();
	s_backbuffer_height = D3D::GetBackBufferHeight();

	FramebufferManagerBase::SetLastXfbWidth(MAX_XFB_WIDTH);
	FramebufferManagerBase::SetLastXfbHeight(MAX_XFB_HEIGHT);

	UpdateDrawRectangle(s_backbuffer_width, s_backbuffer_height);

	int SupersampleCoeficient = 1;
	s_LastEFBScale = g_ActiveConfig.iEFBScale;
	CalculateTargetSize(s_backbuffer_width, s_backbuffer_height, SupersampleCoeficient);
	D3D::FixTextureSize(s_target_width, s_target_height);

	// Creates FramebufferManager (EFB color RT), inits font + shader caches.
	SetupDeviceObjects();

	D3DVIEWPORT8 vp;
	vp.X = 0; vp.Y = 0;
	vp.Width  = s_backbuffer_width;
	vp.Height = s_backbuffer_height;
	vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
	D3D::dev->SetViewport(&vp);
	D3D::dev->Clear(0, NULL, D3DCLEAR_TARGET, 0x0, 0, 0);

	// D3D8: SetRenderTarget takes the colour RT and the depth surface together.
	D3D::dev->SetRenderTarget(FramebufferManager::GetEFBColorRTSurface(),
	                          FramebufferManager::GetEFBDepthRTSurface());
	vp.Width  = s_target_width;
	vp.Height = s_target_height;
	D3D::dev->SetViewport(&vp);
	D3D::dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	D3D::BeginFrame();
}

Renderer::~Renderer()
{
	TeardownDeviceObjects();
	D3D::EndFrame();
	D3D::Close();
}

void Renderer::RenderText(const char *text, int left, int top, u32 color)
{
	D3D::font.DrawTextScaled((float)left, (float)top, 20, 20, 0.0f, color, text);
}

TargetRectangle Renderer::ConvertEFBRectangle(const EFBRectangle& rc)
{
	TargetRectangle result;
	result.left   = EFBToScaledX(rc.left);
	result.top    = EFBToScaledY(rc.top);
	result.right  = EFBToScaledX(rc.right);
	result.bottom = EFBToScaledY(rc.bottom);
	return result;
}

bool Renderer::CheckForResize()
{
	return false;   // fixed backbuffer on Xbox
}

u32 Renderer::AccessEFB(EFBAccessType type, u32 x, u32 y, u32 poke_data)
{
	// TODO(task#15): EFB peek/poke via the read-back surfaces. Return the poke value
	// for pokes / 0 for peeks so games that probe the EFB don't stall.
	(void)x; (void)y;
	return (type == POKE_COLOR || type == POKE_Z) ? poke_data : 0;
}

void Renderer::UpdateViewport(Matrix44& vpCorrection)
{
	// TODO(task#15): full GC viewport mapping. Identity correction for now.
	Matrix44::LoadIdentity(vpCorrection);
}

// --- GC -> D3D render-state translation. Minimal for first-light (nothing draws
//     while the shader caches are stubbed); restored fully under task #15. -------
void Renderer::SetColorMask()        {}
void Renderer::SetBlendMode(bool)    {}
void Renderer::SetScissorRect(const TargetRectangle& rc) { (void)rc; }
void Renderer::SetGenerationMode()   {}
void Renderer::SetDepthMode()        {}
void Renderer::SetLogicOpMode()      {}
void Renderer::SetDitherMode()       {}
void Renderer::SetLineWidth()        {}
void Renderer::SetSamplerState(int stage, int texindex) { (void)stage; (void)texindex; }
void Renderer::SetInterlacingMode()  {}

void Renderer::ApplyState(bool bUseDstAlpha)  { (void)bUseDstAlpha; }
void Renderer::RestoreState()                 {}
void Renderer::ResetAPIState()                {}
void Renderer::RestoreAPIState()              {}

void Renderer::ClearScreen(const EFBRectangle& rc, bool colorEnable, bool alphaEnable, bool zEnable, u32 color, u32 z)
{
	(void)rc; (void)alphaEnable;
	DWORD flags = 0;
	if (colorEnable) flags |= D3DCLEAR_TARGET;
	if (zEnable)     flags |= D3DCLEAR_ZBUFFER;
	if (flags && D3D::dev)
		D3D::dev->Clear(0, NULL, flags, color, (float)(z & 0xFFFFFF) / 16777216.0f, 0);
}

void Renderer::ReinterpretPixelData(unsigned int convtype) { (void)convtype; }

void Renderer::Swap(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& rc, float Gamma)
{
	(void)xfbAddr; (void)rc; (void)Gamma;

	if (g_bSkipCurrentFrame || !fbWidth || !fbHeight)
	{
		Core::Callback_VideoCopiedToXFB(false);
		return;
	}

	// TODO(task#15): blit the XFB source to the backbuffer here. main_xbox's
	// xbox_d3d8_pump() presents the backbuffer on the main thread. For first-light we
	// just complete the swap so the guest's XFB request finishes and emulation runs.
	Core::Callback_VideoCopiedToXFB(true);
	++frameCount;

	// Re-arm the EFB as the render target for the next frame's geometry.
	if (D3D::dev)
		D3D::dev->SetRenderTarget(FramebufferManager::GetEFBColorRTSurface(),
		                          FramebufferManager::GetEFBDepthRTSurface());
}

bool Renderer::SaveScreenshot(const std::string &filename, const TargetRectangle &rc)
{
	(void)filename; (void)rc;
	return false;   // TODO(task#15): CreateImageSurface + copy + write
}

// --- Shader constants. No shaders bound yet (task #14) -> store nothing. --------
void Renderer::SetPSConstant4f(unsigned int const_number, float f1, float f2, float f3, float f4)
{ (void)const_number; (void)f1; (void)f2; (void)f3; (void)f4; }
void Renderer::SetPSConstant4fv(unsigned int const_number, const float *f)
{ (void)const_number; (void)f; }
void Renderer::SetMultiPSConstant4fv(unsigned int const_number, unsigned int count, const float *f)
{ (void)const_number; (void)count; (void)f; }
void Renderer::SetVSConstant4f(unsigned int const_number, float f1, float f2, float f3, float f4)
{ (void)const_number; (void)f1; (void)f2; (void)f3; (void)f4; }
void Renderer::SetVSConstant4fv(unsigned int const_number, const float *f)
{ (void)const_number; (void)f; }
void Renderer::SetMultiVSConstant3fv(unsigned int const_number, unsigned int count, const float *f)
{ (void)const_number; (void)count; (void)f; }
void Renderer::SetMultiVSConstant4fv(unsigned int const_number, unsigned int count, const float *f)
{ (void)const_number; (void)count; (void)f; }

}  // namespace DX8
