// OG Xbox port — Phase 0 PC harness.
// Minimal Win32 frontend: boots a GC image with the interpreter + DX9 backend.
// Modeled on MainNoGUI.cpp (the 4.0 Linux nogui frontend); no wxWidgets.

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string>
#pragma comment(lib, "psapi.lib")

#include "Common.h"
#include "FileUtil.h"
#include "LogManager.h"
#include "ConfigManager.h"
#include "VideoBackendBase.h"
#include "BootManager.h"
#include "Core.h"
#include "Host.h"
#include "Thread.h"
#include "PowerPC/PowerPC.h"
#include "HW/Wiimote.h"
#include "CPUDetect.h"
#include "x64SSE1Fallback.h"

static volatile bool s_running = true;
static bool s_rendererHasFocus = true;
static HWND s_hwnd = NULL;
static Common::Event updateMainFrameEvent;

// ---- Host_* callbacks (contract in Core/Src/Host.h) ----
void Host_NotifyMapLoaded() {}
void Host_RefreshDSPDebuggerWindow() {}
void Host_ShowJitResults(unsigned int) {}

void Host_Message(int Id)
{
	if (Id == WM_USER_STOP)
		s_running = false;
}

void* Host_GetRenderHandle() { return s_hwnd; }
void* Host_GetInstance() { return NULL; }

void Host_UpdateTitle(const char* title)
{
	if (s_hwnd)
		SetWindowTextA(s_hwnd, title);
}

void Host_UpdateLogDisplay() {}
void Host_UpdateDisasmDialog() {}
void Host_UpdateMainFrame() { updateMainFrameEvent.Set(); }
void Host_UpdateBreakPointView() {}
bool Host_GetKeyState(int) { return false; }

void Host_GetRenderWindowSize(int& x, int& y, int& width, int& height)
{
	x = 0;
	y = 0;
	RECT rc = { 0, 0, 640, 480 };
	if (s_hwnd)
		GetClientRect(s_hwnd, &rc);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;
}

void Host_RequestRenderWindowSize(int, int) {}

void Host_SetStartupDebuggingParameters()
{
	SCoreStartupParameter& StartUp = SConfig::GetInstance().m_LocalCoreStartupParameter;
	StartUp.bEnableDebugging = false;
	StartUp.bBootToPause = false;
}

bool Host_RendererHasFocus() { return s_rendererHasFocus; }
void Host_ConnectWiimote(int, bool) {}
void Host_SetWiiMoteConnectionState(int) {}
void Host_UpdateStatusBar(const char*, int) {}

void Host_SysMessage(const char* fmt, ...)
{
	va_list list;
	char msg[512];
	va_start(list, fmt);
	vsnprintf(msg, sizeof(msg), fmt, list);
	va_end(list);
	fprintf(stderr, "%s\n", msg);
}

// Route PanicAlert & co. to stderr instead of a blocking MessageBox so the
// SSE1 choke-point reports (and everything else) land in captured logs.
static bool HarnessMsgHandler(const char* caption, const char* text, bool yes_no, int style)
{
	fprintf(stderr, "[%s] %s\n", caption, text);
	fflush(stderr);
	return true;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CLOSE:
		s_running = false;
		return 0;
	case WM_SETFOCUS:
		s_rendererHasFocus = true;
		break;
	case WM_KILLFOCUS:
		s_rendererHasFocus = false;
		break;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE)
			s_running = false;
		break;
	}
	return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "%s\n", scm_rev_str);
		fprintf(stderr, "Usage: DolphinHeadless <path-to-dol/elf/gcm/iso> [jit]\n");
		return 1;
	}
	const std::string bootFile = argv[1];
	bool useJit = false;
	bool noSound = false;
	bool fpuInterp = false; // Tier A: JIT integer/branch, interpret all FPU
	bool sse1Mode = false;  // simulate Coppermine: report no SSE2 to the JIT
	for (int i = 2; i < argc; i++)
	{
		if (!strcmp(argv[i], "jit"))       useJit = true;
		if (!strcmp(argv[i], "nosound"))   noSound = true;
		if (!strcmp(argv[i], "fpuinterp")) fpuInterp = true;
		if (!strcmp(argv[i], "sse1"))      sse1Mode = true;
	}

	// Render window (the DX9 backend creates its child EmuWindow inside it)
	WNDCLASSA wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = "DolphinHeadless";
	RegisterClassA(&wc);
	RECT rc = { 0, 0, 640, 480 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	s_hwnd = CreateWindowA("DolphinHeadless", "Dolphin OG-Xbox PC harness",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
		rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, wc.hInstance, NULL);

	RegisterMsgAlertHandler(HarnessMsgHandler);
	LogManager::Init();
	SConfig::Init();

	SCoreStartupParameter& sp = SConfig::GetInstance().m_LocalCoreStartupParameter;
	sp.iCPUCore = useJit ? 1 : 0; // 0 = interpreter, 1 = x86 JIT
	sp.bCPUThread = false;        // single core for deterministic bring-up
	sp.bDSPHLE = true;
	sp.bDSPThread = false;
	sp.bFastmem = false; // TEMP repro: match the Xbox (safe memory path) to see if
	                     // the 0x8023346C JIT exception bug reproduces on PC.
	sp.m_strVideoBackend = "Software"; // software rasterizer (OGL present); no D3D9 needed
	sp.bFullscreen = false;
	sp.bRenderToMain = false;
	sp.iRenderWindowXPos = 100;
	sp.iRenderWindowYPos = 100;
	sp.iRenderWindowWidth = 640;
	sp.iRenderWindowHeight = 480;
	SConfig::GetInstance().sBackend = noSound ? BACKEND_NULLSOUND : BACKEND_DIRECTSOUND;

	if (fpuInterp)
	{
		// Xbox Tier A JIT config: every float category falls back to the
		// interpreter; integer/branch/load-store stay JIT'd. No SSE2 needed
		// by the generated FPU code because there is none.
		sp.bJITFloatingPointOff = true;
		sp.bJITPairedOff = true;
		sp.bJITLoadStoreFloatingOff = true;
		sp.bJITLoadStorePairedOff = true;
	}
	if (sse1Mode)
	{
		// Pretend to be a Coppermine Celeron: SSE1/MMX only. Paths gated on
		// cpu_info take their fallbacks; the emitter SSE1 rewrite layer keys
		// off this too (once it exists).
		cpu_info.bSSE2 = false;
		cpu_info.bSSE3 = false;
		cpu_info.bSSSE3 = false;
		cpu_info.bSSE4_1 = false;
		cpu_info.bSSE4_2 = false;
	}

	VideoBackend::PopulateList();
	VideoBackend::ActivateBackend(sp.m_strVideoBackend);
	WiimoteReal::LoadSettings();

	printf("[harness] booting %s (cpu=%s%s%s audio=%s)\n", bootFile.c_str(),
	       useJit ? "JIT" : "interpreter",
	       sse1Mode ? " +SSE1-fallback" : "",
	       fpuInterp ? " +FPU-interp" : "",
	       noSound ? "null" : "DSound");

	int ret = 0;
	if (BootManager::BootCore(bootFile))
	{
		MSG msg;
		DWORD lastReport = GetTickCount();
		u32 lastPC = 0;
		while (s_running && PowerPC::GetState() != PowerPC::CPU_POWERDOWN)
		{
			while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageA(&msg);
			}
			// Watchdog: report emulated-CPU progress every 2s so a "freeze"
			// is distinguishable from slow interpretation. In SSE1 mode also
			// report thunk-call rates (per second, top offenders).
			DWORD now = GetTickCount();
			if (now - lastReport >= 2000)
			{
				u32 pc = PowerPC::ppcState.pc;
				PROCESS_MEMORY_COUNTERS pmc;
				double wsMB = 0.0, peakMB = 0.0;
				if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
				{
					wsMB = pmc.WorkingSetSize / (1024.0 * 1024.0);
					peakMB = pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
				}
				printf("[watchdog] state=%d pc=%08x%s  RAM=%.1fMB (peak %.1fMB)\n",
				       (int)Core::GetState(), pc,
				       (pc == lastPC) ? "  << PC UNCHANGED" : "",
				       wsMB, peakMB);
				if (sse1Mode)
				{
					static const char* opNames[32] = {
						"PSLLW","PSRLW","PSRAW","PSLLD","PSRLD","PSRAD",
						"PSLLQ","PSRLQ","PSHUFLW","PACKSSDW","PACKSSWB",
						"PACKUSWB","PUNPCKLBW","PUNPCKLWD","CVTDQ2PS",
						"CVTTPS2DQ","CMPSD","CMPPD","MINSD","MAXSD",
						0,0,0,0,0,0,0,0,0,0,0,0 };
					static u32 lastCount[32];
					u32 total = 0;
					char line[256];
					int pos = 0;
					for (int i = 0; i < 32; i++)
					{
						u32 delta = SSE1Fallback::g_thunk_count[i] - lastCount[i];
						lastCount[i] = SSE1Fallback::g_thunk_count[i];
						total += delta;
						if (delta > 0 && opNames[i] && pos < 200)
							pos += sprintf(line + pos, " %s=%u", opNames[i], delta / 2);
					}
					if (total > 0)
						printf("[thunks/s]%s (total %u/s)\n", line, total / 2);
				}
				fflush(stdout);
				lastPC = pc;
				lastReport = now;
			}
			Sleep(10);
		}
		Core::Stop();
		// Give the emu thread a moment to wind down before teardown.
		for (int i = 0; i < 300 && PowerPC::GetState() != PowerPC::CPU_POWERDOWN; i++)
			Sleep(10);
	}
	else
	{
		fprintf(stderr, "[harness] BootCore failed\n");
		ret = 1;
	}

	WiimoteReal::Shutdown();
	VideoBackend::ClearList();
	SConfig::Shutdown();
	LogManager::Shutdown();
	return ret;
}
