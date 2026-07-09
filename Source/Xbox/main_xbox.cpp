// OG Xbox port: XDK entry shell for Dolphin.
// Creates the D3D8 device (stemmed from the PS1 emu's main.cpp), boots a
// GameCube image from the disc, and runs the emulation loop. Interpreter CPU
// + Software renderer (presented via xbox_d3d8) for the first-boot POC.

#include <xtl.h>

#include "Common.h"
#include "CommonPaths.h"
#include "FileUtil.h"
#include "LogManager.h"
#include "ConfigManager.h"
#include "BootManager.h"
#include "Core.h"
#include "Host.h"
#include "PowerPC/PowerPC.h"
#include "CPUDetect.h"          // cpu_info (force SSE1 JIT path)
#include "VideoBackendBase.h"
#include "AudioCommon.h"

#include <stdio.h>
#include "xbox_d3d8.h"

// The Software backend lives in Plugin_VideoSoftware.
namespace SW { class VideoSoftware; }

static volatile bool s_running = true;
static volatile LONG s_panicSeen = 0;   // any PanicAlert fired (CYAN)
static volatile LONG s_crashSeen = 0;   // unhandled exception on any thread (WHITE)
static volatile DWORD s_crashCode = 0;
static volatile DWORD s_crashAddr = 0;  // faulting address (for access violations)
static volatile DWORD s_crashEip  = 0;  // instruction pointer at fault
static DWORD s_dump[12];                 // raw stack dump: EIP, ESP, 10 stack DWORDs
extern "C" void xbox_d3d8_draw_stackdump(unsigned long*, int);

// ---- Host_* callbacks (contract in Core/Src/Host.h) ----
void Host_NotifyMapLoaded() {}
void Host_RefreshDSPDebuggerWindow() {}
void Host_ShowJitResults(unsigned int) {}
void Host_Message(int Id) { if (Id == WM_USER_STOP) s_running = false; }
void* Host_GetRenderHandle() { return NULL; } // Xbox has no HWND
void* Host_GetInstance() { return NULL; }
void Host_UpdateTitle(const char*) {}
void Host_UpdateLogDisplay() {}
void Host_UpdateDisasmDialog() {}
void Host_UpdateMainFrame() {}
void Host_UpdateBreakPointView() {}
bool Host_GetKeyState(int) { return false; }
void Host_GetRenderWindowSize(int& x, int& y, int& width, int& height)
{
	x = 0; y = 0;
	width = xbox_d3d8_backbuffer_w();
	height = xbox_d3d8_backbuffer_h();
}
void Host_RequestRenderWindowSize(int, int) {}
void Host_SetStartupDebuggingParameters()
{
	SCoreStartupParameter& sp = SConfig::GetInstance().m_LocalCoreStartupParameter;
	sp.bEnableDebugging = false;
	sp.bBootToPause = false;
}
bool Host_RendererHasFocus() { return true; }
void Host_ConnectWiimote(int, bool) {}
void Host_SetWiiMoteConnectionState(int) {}
void Host_UpdateStatusBar(const char*, int) {}
void Host_SysMessage(const char* fmt, ...)
{
	va_list list; char msg[512];
	va_start(list, fmt);
	vsnprintf(msg, sizeof(msg), fmt, list);
	va_end(list);
	OutputDebugStringA(msg);
	OutputDebugStringA("\n");
}

// Route PanicAlert & friends to the kernel debug channel (no MessageBox).
// Cap the logging: a panic firing in the emulation hot loop would otherwise
// flood the debug channel until the guest grinds to a halt.
static bool XboxMsgHandler(const char* caption, const char* text, bool, int)
{
	static volatile LONG count = 0;
	_InterlockedExchange(&s_panicSeen, 1);
	if (_InterlockedIncrement(&count) <= 20)
	{
		OutputDebugStringA(caption); OutputDebugStringA(": ");
		OutputDebugStringA(text);    OutputDebugStringA("\n");
	}
	return true;
}

extern "C" char g_xbox_last_open[260]; // last path the file layer touched (crash diag)
extern "C" char g_xbox_stage[64];        // boot-stage marker
extern "C" unsigned long g_xapip_caller; // return addr recorded by the resolver hook
extern "C" unsigned long g_xmf_caller;   // XMemFree caller (recorded by trampoline)
extern "C" unsigned long g_xmf_ptr;      // pointer XMemFree was freeing
extern "C" void xbox_install_xmemfree_hook();
extern "C" void xbox_install_nuke_hook();
extern "C" void xbox_install_xgetsection_hook();
extern "C" void xbox_install_gdfse_hook();
extern "C" void xbox_install_fiopen_hook();
extern "C" unsigned long g_nuke_caller;
DWORD g_mainTid = 0;                    // main thread id, for crash-thread tagging

// Persistent diagnostic watchdog: an independent thread that paints the RAM
// footprint + current stage + CPU-loop liveness every ~300ms, so we can watch
// memory and detect softlocks regardless of where the (possibly stuck) main
// thread is. Uses only kernel/Win32/D3D8 calls (no CRT-locale, no per-thread
// Xapi), so it's safe on a raw worker thread.
extern "C" void xbox_d3d8_watchpaint(unsigned);
static DWORD WINAPI xbox_watchdog_thread(LPVOID)
{
	unsigned wd = 0;
	for (;;) { Sleep(300); xbox_d3d8_watchpaint(wd++); }
}

// TEST 2026-07-08: run the CPU+GPU loop on its OWN thread (raw CreateThread, which
// gets the per-thread Xapi init that std::thread workers lack) instead of inline on
// the main thread. Tests whether the inline execution model breaks the JIT at
// 0x8023346C. 4MB stack also rules out a main-thread stack overflow.
static void (*g_xbox_cpu_fn)(void) = 0;
static DWORD WINAPI xbox_cpu_tramp(LPVOID) { if (g_xbox_cpu_fn) g_xbox_cpu_fn(); return 0; }
extern "C" void xbox_spawn_cpu(void (*fn)(void))
{
	g_xbox_cpu_fn = fn;
	CreateThread(NULL, 0x400000, xbox_cpu_tramp, NULL, 0, NULL);
}

// Any-thread unhandled exception -> flag for the main loop to paint, and emit
// ONE greppable marker line (code / faulting address / EIP) to the debug
// channel for offline diagnosis.
static LONG WINAPI XboxCrashFilter(EXCEPTION_POINTERS* ep)
{
	DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
	DWORD eip = 0, esp = 0, faultAddr = 0;
	if (ep && ep->ContextRecord) { eip = ep->ContextRecord->Eip; esp = ep->ContextRecord->Esp; }
	if (ep && ep->ExceptionRecord && ep->ExceptionRecord->NumberParameters >= 2)
		faultAddr = (DWORD)ep->ExceptionRecord->ExceptionInformation[1]; // the bad pointer

	// [0]=EIP (red), [1]=faulting address (green) = the null/bad pointer being
	// accessed, [2..11] = first 10 code-range return addresses scanning down.
	s_dump[0] = eip; s_dump[1] = faultAddr;
	for (int i = 2; i < 12; i++) s_dump[i] = 0;
	if (esp)
	{
		const DWORD* p = (const DWORD*)esp;
		int found = 0;
		// Dolphin's own code is runtime ~0x60000..0x1B0000 (libs sit below at
		// 0x15xxx..0x3Bxxx). Filter to Dolphin frames so we see WHICH Dolphin
		// function called into the crashing CRT/locale operation. SEH-guard the
		// read: scanning too far can walk off the stack base and fault the filter
		// itself (which masks the crash as a frozen screen).
		__try
		{
			for (int i = 0; i < 1536 && found < 10; i++)
			{
				DWORD v = p[i];
				if (v > 0x00060000 && v < 0x001B0000)
					s_dump[2 + found++] = v;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	s_crashCode = eip;

	// Tag the path line with which thread faulted (main vs emu/other).
	{
		// Prepend the crashing thread to the (uncropped) stage line.
		const char* tag = (GetCurrentThreadId() == g_mainTid) ? "MAIN " : "WORKER ";
		char combined[64]; int n = 0;
		for (int k = 0; tag[k] && n < 62; k++) combined[n++] = tag[k];
		for (int k = 0; g_xbox_stage[k] && n < 62; k++) combined[n++] = g_xbox_stage[k];
		combined[n] = 0;
		for (int k = 0; k <= n; k++) g_xbox_stage[k] = combined[k];
	}
	_InterlockedExchange(&s_crashSeen, 1);
	// Paint the diagnosis directly here — the crashing thread may BE the main
	// thread (crash before the run loop), in which case the loop never paints.
	// draw_diag uses raw Clear/Present (no devLock), safe from any thread.
	for (;;) { xbox_d3d8_draw_stackdump(s_dump, 12); Sleep(300); }
	return EXCEPTION_EXECUTE_HANDLER; // unreachable; satisfies C4716 after __try
}

// Create the D3D8 device — mirrors the PS1 emu's HW-proven recipe exactly
// (FLIP swap chain, no auto depth-stencil, 480p-gated progressive flag,
// candidate fallback so an unsupported mode steps down cleanly).
static IDirect3DDevice8* create_device()
{
	OutputDebugStringA("[dolphin-xbox] create_device\n");
	IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
	if (!d3d) return NULL;
	d3d->SetPushBufferSize(2 * 1024 * 1024, 64 * 1024); // headroom (PS1 lesson)

	DWORD vf = XGetVideoFlags();
	DWORD flagCand[2];
	int ncand = 0;
	if (vf & XC_VIDEO_FLAGS_HDTV_480p)
		flagCand[ncand++] = D3DPRESENTFLAG_PROGRESSIVE;
	flagCand[ncand++] = D3DPRESENTFLAG_INTERLACED;

	IDirect3DDevice8* dev = NULL;
	for (int ci = 0; ci < ncand && !dev; ci++)
	{
		D3DPRESENT_PARAMETERS pp;
		ZeroMemory(&pp, sizeof(pp));
		pp.BackBufferWidth  = 640;
		pp.BackBufferHeight = 480;
		pp.BackBufferFormat = D3DFMT_X8R8G8B8;
		pp.BackBufferCount  = 1;
		pp.SwapEffect       = D3DSWAPEFFECT_FLIP;
		pp.Flags            = flagCand[ci];
		pp.FullScreen_RefreshRateInHz      = 60;
		pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
		if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
		                             D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)))
			dev = NULL;
	}
	OutputDebugStringA(dev ? "[dolphin-xbox] device OK\n"
	                       : "[dolphin-xbox] device FAILED\n");
	return dev;
}

// Writable HDD partition chosen by the boot probe; FileUtil's _XBOX user path
// reads this (default E:). Probed here with the RAW kernel API so a broken CRT
// file layer can't mask a writable drive.
extern "C" char g_xbox_user_drive[8] = "E:";

// Kernel symbolic-link mount for the large HDD partitions (F: = Partition6,
// G: = Partition7), which aren't auto-mounted on a DVD boot. Mirrors the PS1
// emu's xbox_paths.c. No-op / harmless if the partition doesn't exist.
typedef struct _ANSI_STR { unsigned short Length, MaximumLength; char* Buffer; } ANSI_STR;
extern "C" long __stdcall IoCreateSymbolicLink(ANSI_STR* link, ANSI_STR* dev);
extern "C" long __stdcall IoDeleteSymbolicLink(ANSI_STR* link);
static void astr(ANSI_STR* s, char* b)
{
	unsigned short L = 0; while (b[L]) L++;
	s->Length = L; s->MaximumLength = (unsigned short)(L + 1); s->Buffer = b;
}
static void mount_part(char* drive, char* dev)
{
	ANSI_STR d, t; astr(&d, drive); astr(&t, dev);
	IoDeleteSymbolicLink(&d);
	IoCreateSymbolicLink(&d, &t);
}
static void mount_hdd_partitions(void)
{
	char fl[] = "\\??\\F:", fd[] = "\\Device\\Harddisk0\\Partition6";
	char gl[] = "\\??\\G:", gd[] = "\\Device\\Harddisk0\\Partition7";
	mount_part(fl, fd);
	mount_part(gl, gd);
}

// Try a raw CreateFile write on `drive` (e.g. "E:"). No CRT.
static bool probe_drive_raw(const char* drive)
{
	char path[64];
	wsprintfA(path, "%s\\dolphin_probe.tmp", drive);
	HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD wrote = 0;
	WriteFile(h, "ok", 2, &wrote, NULL);
	CloseHandle(h);
	DeleteFileA(path);
	return wrote == 2;
}

// Recursively copy a directory tree with raw Win32 (main thread only). Used to
// stage the read-only DVD's Sys\ onto the writable HDD so Dolphin's lazy Sys
// opens (font/DSP ROM) happen on F:, never on D: off the emu thread.
static void copy_dir_recursive(const char* src, const char* dst)
{
	CreateDirectoryA(dst, NULL);
	char pattern[MAX_PATH];
	wsprintfA(pattern, "%s\\*", src);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do
	{
		if (fd.cFileName[0] == '.' &&
		    (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
			continue;
		char s[MAX_PATH], d[MAX_PATH];
		wsprintfA(s, "%s\\%s", src, fd.cFileName);
		wsprintfA(d, "%s\\%s", dst, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			copy_dir_recursive(s, d);
		else
			CopyFileA(s, d, FALSE);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

extern "C" void xbox_install_ddrive_hook();

extern "C" void xbox_note_stage(const char*); // per-stage RAM map

void __cdecl main()
{
	g_mainTid = GetCurrentThreadId();
	xbox_note_stage("boot:start"); // baseline free-RAM before any big allocation
	// Drive/content detours disabled (emulation runs on main thread now). But
	// install the XMemFree recorder to capture who frees the bad pointer.
	xbox_install_xmemfree_hook();
	xbox_install_nuke_hook();
	// Root fix: inline-detour std::_Fiopen so all C++ stream opens fail BEFORE
	// entering RXDK's broken path resolver (section lookup / disk-space / heap
	// realloc that fault on null globals). The per-function detours below just
	// pushed the crash down the chain, so they stay off.
	xbox_install_fiopen_hook();
	// xbox_install_xgetsection_hook();
	// xbox_install_gdfse_hook();
	IDirect3DDevice8* dev = create_device();
	xbox_d3d8_set_device(dev);
	xbox_note_stage("boot:d3ddev"); // after the D3D8 device + framebuffers
	if (dev)
	{
		xbox_d3d8_clear_present(0xFF0000FF); // BLUE = shell alive, device OK
		Sleep(1500);
	}

	// Mount the large HDD partitions so F:/G: are reachable from a DVD boot.
	mount_hdd_partitions();

	// Find a writable HDD partition (raw kernel write). Prefer F:/G: (the big
	// data partitions), then E:, then T:/Z:/U:. Paint the winner:
	//   F:=CYAN  G:=YELLOW  E:=GREEN  T:=MAGENTA  Z:=ORANGE  U:=pale-green
	//   none=dim RED(hold).
	{
		const char* cand[] = { "F:", "G:", "E:", "T:", "Z:", "U:" };
		const unsigned long col[] = { 0xFF00FFFF, 0xFFFFFF00, 0xFF00FF00,
		                              0xFFFF00FF, 0xFFFF8000, 0xFF80FF80 };
		int found = -1;
		for (int i = 0; i < 6 && found < 0; i++)
			if (probe_drive_raw(cand[i])) found = i;
		if (found >= 0)
		{
			lstrcpynA(g_xbox_user_drive, cand[found], sizeof(g_xbox_user_drive));
			if (dev) { xbox_d3d8_clear_present(col[found]); Sleep(2500); }
		}
		else if (dev)
		{
			// No writable partition — hold dim red so we know the HDD is the problem.
			for (int k = 0; k < 20 && s_running; k++) { xbox_d3d8_clear_present(0xFF400000); Sleep(500); }
		}
	}

	SetUnhandledExceptionFilter(XboxCrashFilter);
	RegisterMsgAlertHandler(XboxMsgHandler);

	// Create the writable user tree on E: (HDD) BEFORE anything opens a file
	// there — the XBE + Sys are on the read-only DVD, so writes must go to E:.
	// GetUserPath computes the E:\Dolphin\User\ tree (see FileUtil _XBOX).
	File::CreateFullPath(File::GetUserPath(D_USER_IDX));
	File::CreateFullPath(File::GetUserPath(D_CONFIG_IDX));
	File::CreateFullPath(File::GetUserPath(D_LOGS_IDX));
	File::CreateFullPath(File::GetUserPath(D_GCUSER_IDX));
	File::CreateFullPath(File::GetUserPath(D_CACHE_IDX));
	File::CreateFullPath(File::GetUserPath(D_SHADERCACHE_IDX));
	File::CreateFullPath(File::GetUserPath(D_STATESAVES_IDX));

	// Stage Sys\ from the read-only DVD onto the writable HDD (main thread), so
	// the emu thread's lazy Sys opens hit F: instead of crashing the Xbox D:
	// drive resolver (XapipUpdateGetCurrentDDriveMapping off the main thread).
	{
		char sysDst[128];
		wsprintfA(sysDst, "%s\\Dolphin\\Sys", g_xbox_user_drive);
		copy_dir_recursive("D:\\Sys", sysDst);
	}

	LogManager::Init();
	SConfig::Init();

	SCoreStartupParameter& sp = SConfig::GetInstance().m_LocalCoreStartupParameter;
	sp.iCPUCore    = 1;      // SSE1 JIT (cpu_info.bSSE2=false below). Now fits: RAM view
	                         // trimmed 32->24MB (REALRAM) frees 8MB for the iCache.
	sp.bCPUThread  = false;  // single core, deterministic bring-up
	sp.bDSPHLE     = true;
	sp.bDSPThread  = false;
	sp.bSkipIdle   = true;
	sp.bFastmem    = false;  // safe memory path (base+addr fastmem N/A on Xbox arena)
	// Bisect flags REVERTED (2026-07-08): interpreting ops via FallBackToInterpreter
	// replaced the 0x8023346C loop with a consistent CRASH — pointing at the JIT's
	// C++ call/thunk path (ABI_CallFunction) itself, which the exception handler
	// also uses. All ops JIT'd again to keep the clean loop baseline.
	sp.m_strVideoBackend = "Software";
	sp.iRenderWindowWidth  = 640;
	sp.iRenderWindowHeight = 480;
	SConfig::GetInstance().sBackend = BACKEND_NULLSOUND;

	// SSE-MODE PROBE (2026-07-08): the SSE1 forcing was commented out on the belief
	// that "xemu HAS SSE2 so cpu_info detects it" — but xemu masks CPUID to the
	// Xbox Pentium III (no SSE2), so CPUDetect very likely reports bSSE2=false and
	// we've been running the SSE1 REWRITE path, not stock SSE2, this whole time.
	// Force bSSE2=TRUE to disambiguate: if the JIT #UD-crashes EARLY, xemu is a
	// strict P3 (we were in SSE1 mode -> the SSE1 rewrites are the real suspect);
	// if it reaches 0x8023346C unchanged, we truly were SSE2 (base JIT).
	cpu_info.bSSE2 = true;
	// *** For real Coppermine HW, force SSE1 instead: set bSSE2..bSSE4_2 = false. ***

	// Start the persistent RAM/liveness overlay before emulation blocks the main
	// thread inline.
	CreateThread(NULL, 0x8000, xbox_watchdog_thread, NULL, 0, NULL); // small stack

	xbox_note_stage("boot:preVB");  // before video backend populate/activate
	VideoBackend::PopulateList();
	VideoBackend::ActivateBackend(sp.m_strVideoBackend);
	xbox_note_stage("boot:postVB"); // after video backend activate

	// Boot the disc image staged at the mount root.
	const char* bootFile = "D:\\game.iso";
	xbox_note_stage("boot:core");   // right before BootCore (DOL load + HW::Init)
	OutputDebugStringA("[dolphin-xbox] booting D:\\game.iso\n");

	if (BootManager::BootCore(bootFile))
	{
		while (s_running && PowerPC::GetState() != PowerPC::CPU_POWERDOWN)
		{
			// Diagnosis colors take priority: WHITE = a thread crashed,
			// CYAN = PanicAlert fired (emulation hit an unhandled case).
			if (s_crashSeen)
			{
				// 3 rows of 32 bit-bars: exception code (red), faulting
				// address (green), instruction pointer (blue). Screenshot
				// reads back exact hex.
				xbox_d3d8_draw_diag(s_crashCode, s_crashAddr, s_crashEip);
				Sleep(250);
				continue;
			}
			if (s_panicSeen && !xbox_d3d8_xfb_seen())
			{
				xbox_d3d8_clear_present(0xFF00FFFF);
				Sleep(250);
				continue;
			}
			// All D3D happens here on the main thread: present staged XFB
			// frames; until the first one, pulse the PC heartbeat.
			xbox_d3d8_pump();
			if (!xbox_d3d8_xfb_seen())
			{
				xbox_d3d8_heartbeat(PowerPC::ppcState.pc);
				Sleep(100);
			}
			else
			{
				Sleep(5);
			}
		}
		Core::Stop();
	}
	else
	{
		OutputDebugStringA("[dolphin-xbox] BootCore FAILED\n");
		if (dev)
		{
			xbox_d3d8_clear_present(0xFFFF0000); // RED = core init OK, boot failed
			Sleep(5000);                          // hold so it's visible
		}
	}

	VideoBackend::ClearList();
	SConfig::Shutdown();
	LogManager::Shutdown();
	xbox_d3d8_shutdown();
	if (dev) dev->Release();

	// Return to dashboard.
	LD_LAUNCH_DASHBOARD ld = { XLD_LAUNCH_DASHBOARD_MAIN_MENU };
	XLaunchNewImage(NULL, (LAUNCH_DATA*)&ld);
}
