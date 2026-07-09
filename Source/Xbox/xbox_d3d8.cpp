// OG Xbox port: D3D8 present backend (xtl.h world; see xbox_d3d8.h).
// Stemmed from the PS1 emu's xbox_gpu_dx8.cpp present/blit path — the HW-proven
// pattern for getting an image on screen on the NV2A via RXDK D3D8.
//
// This is the Phase-2 "software-rasterize -> DX8 blit" presenter: it does NOT
// emulate the Flipper TEV; it just puts Dolphin's finished XFB on screen.

#include <xtl.h>
#include <intrin.h> // _NTOS_ suppresses winbase Interlocked*; use the intrinsic
#include <stdlib.h>
#include <string.h>
#include "xbox_d3d8.h"

static IDirect3DDevice8*  g_dev   = 0;
static IDirect3DTexture8* g_xfbTex = 0;   // dynamic XFB upload texture
static int g_texW = 0, g_texH = 0;
static int g_bbW = 640, g_bbH = 480;
static volatile LONG g_xfbSeen = 0;       // first real frame presented?
static CRITICAL_SECTION g_devLock;        // main-thread heartbeat vs emu present
static int g_devLockInit = 0;

// One screen-space textured vertex (transformed + lit, so no vertex shader).
struct SVert { float x, y, z, rhw; float u, v; };
#define FVF_SVERT (D3DFVF_XYZRHW | D3DFVF_TEX1)

void xbox_d3d8_set_device(void *dev)
{
	if (!g_devLockInit) { InitializeCriticalSection(&g_devLock); g_devLockInit = 1; }
	g_dev = (IDirect3DDevice8*)dev;
	if (g_dev)
	{
		D3DSURFACE_DESC sd;
		IDirect3DSurface8* bb = 0;
		if (SUCCEEDED(g_dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
		{
			if (SUCCEEDED(bb->GetDesc(&sd))) { g_bbW = sd.Width; g_bbH = sd.Height; }
			bb->Release();
		}
	}
}

int xbox_d3d8_backbuffer_w(void) { return g_bbW; }
int xbox_d3d8_backbuffer_h(void) { return g_bbH; }

// The DX8 video backend (Plugin_VideoDX8) SHARES this one device — NV2A allows
// only a single IDirect3DDevice8. D3D::Init() grabs it here instead of creating
// a second one. Returns NULL until main_xbox has called xbox_d3d8_set_device().
void *xbox_d3d8_get_device(void) { return (void*)g_dev; }

// (Re)create the upload texture when the XFB size changes. The NV2A can create
// textures between frames (unlike mid-scene), which is fine for the XFB blit.
static bool ensure_tex(int w, int h)
{
	if (g_xfbTex && g_texW == w && g_texH == h)
		return true;
	if (g_xfbTex) { g_xfbTex->Release(); g_xfbTex = 0; }
	if (!g_dev) return false;
	// LINEAR format: CPU lock-and-copy with row pitch (Xbox default formats
	// are swizzled and would scramble a linear memcpy — PS1 emu lesson).
	if (FAILED(g_dev->CreateTexture(w, h, 1, 0, D3DFMT_LIN_A8R8G8B8,
	                                D3DPOOL_DEFAULT, &g_xfbTex)))
		return false;
	g_texW = w; g_texH = h;
	return true;
}

// ---------------------------------------------------------------------------
// Threading model (PS1 lesson): ALL D3D calls happen on the MAIN thread. The
// emulator thread only copies its finished XFB into a staging buffer here;
// the main loop (xbox_d3d8_pump) uploads and presents it. The XDK D3D8 device
// has thread affinity — cross-thread use wedges silently.
// ---------------------------------------------------------------------------
static unsigned char* g_stage = 0;      // staging XFB copy (RGBA)
static int  g_stageW = 0, g_stageH = 0, g_stageCap = 0;
static volatile LONG g_stageDirty = 0;

// EMULATOR THREAD: stage the frame, no D3D.
void xbox_d3d8_present_xfb(const unsigned char *rgba, int width, int height)
{
	if (!rgba || width <= 0 || height <= 0)
		return;
	EnterCriticalSection(&g_devLock);
	int need = width * height * 4;
	if (need > g_stageCap)
	{
		free(g_stage);
		g_stage = (unsigned char*)malloc(need);
		g_stageCap = g_stage ? need : 0;
	}
	if (g_stage)
	{
		memcpy(g_stage, rgba, need);
		g_stageW = width;
		g_stageH = height;
		g_stageDirty = 1;
		g_xfbSeen = 1; // heartbeat stands down permanently
	}
	LeaveCriticalSection(&g_devLock);

	// OG Xbox port: emulation runs inline on the main thread, so present the
	// staged frame right here (no separate pump loop drives it).
	xbox_d3d8_pump();
}

int xbox_d3d8_xfb_seen(void) { return (int)g_xfbSeen; }

// MAIN THREAD: upload + draw + present the latest staged frame.
// Failure states are painted, never silent: ORANGE = CreateTexture failed,
// YELLOW = LockRect failed.
void xbox_d3d8_pump(void)
{
	if (!g_dev)
		return;
	if (!g_stageDirty)
		return;

	EnterCriticalSection(&g_devLock);
	int w = g_stageW, h = g_stageH;
	if (!ensure_tex(w, h))
	{
		g_stageDirty = 0;
		LeaveCriticalSection(&g_devLock);
		g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFF8000, 1.0f, 0); // ORANGE
		g_dev->Present(NULL, NULL, NULL, NULL);
		return;
	}

	// Upload: Dolphin XFB is RGBA bytes; D3D wants ARGB words.
	D3DLOCKED_RECT lr;
	if (FAILED(g_xfbTex->LockRect(0, &lr, NULL, 0)))
	{
		g_stageDirty = 0;
		LeaveCriticalSection(&g_devLock);
		g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFFFFFF00, 1.0f, 0); // YELLOW
		g_dev->Present(NULL, NULL, NULL, NULL);
		return;
	}
	const unsigned char* src = g_stage;
	unsigned char* dstBase = (unsigned char*)lr.pBits;
	for (int y = 0; y < h; y++)
	{
		unsigned long* dst = (unsigned long*)(dstBase + y * lr.Pitch);
		for (int x = 0; x < w; x++)
		{
			dst[x] = 0xFF000000 | ((unsigned long)src[0] << 16) |
			         ((unsigned long)src[1] << 8) | src[2];
			src += 4;
		}
	}
	g_xfbTex->UnlockRect(0);
	g_stageDirty = 0;
	LeaveCriticalSection(&g_devLock);

	g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_dev->SetTexture(0, g_xfbTex);
	g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

	const float fw = (float)g_bbW, fh = (float)g_bbH;
	SVert q[4] = {
		{ -0.5f,      -0.5f,      0.0f, 1.0f, 0.0f, 0.0f },
		{ fw - 0.5f,  -0.5f,      0.0f, 1.0f, 1.0f, 0.0f },
		{ fw - 0.5f,  fh - 0.5f,  0.0f, 1.0f, 1.0f, 1.0f },
		{ -0.5f,      fh - 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
	};

	g_dev->BeginScene();
	g_dev->SetVertexShader(FVF_SVERT);
	g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, q, sizeof(SVert));
	g_dev->EndScene();
	g_dev->SetTexture(0, NULL);
	g_dev->Present(NULL, NULL, NULL, NULL);
}

void xbox_d3d8_heartbeat(unsigned long emulated_pc)
{
	if (!g_dev || g_xfbSeen)
		return;
	static unsigned frame = 0;
	static unsigned lockMisses = 0;
	frame++;
	// Pulsing red channel = the shell loop is alive; green channel tracks the
	// emulated PC's high bits = the GameCube CPU is moving through code.
	unsigned pulse = frame & 0x3F;
	if (frame & 0x40) pulse = 0x3F - pulse;
	unsigned long argb = 0xFF000030
	                   | ((unsigned long)pulse << 17)
	                   | (((emulated_pc >> 16) & 0xFF) << 8);
	// TryEnter so an emu thread that died holding the lock can't freeze us —
	// after ~3s of misses paint PURPLE (= lock orphaned) without the lock.
	if (!TryEnterCriticalSection(&g_devLock))
	{
		if (++lockMisses > 30)
		{
			g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF8000FF, 1.0f, 0);
			g_dev->Present(NULL, NULL, NULL, NULL);
		}
		return;
	}
	lockMisses = 0;
	if (!g_xfbSeen) // re-check inside the lock
	{
		g_dev->Clear(0, NULL, D3DCLEAR_TARGET, argb, 1.0f, 0);
		g_dev->Present(NULL, NULL, NULL, NULL);
	}
	LeaveCriticalSection(&g_devLock);
}

void xbox_d3d8_clear_present(unsigned long argb)
{
	if (!g_dev) return;
	// No auto depth-stencil on this device — clear the target only.
	g_dev->Clear(0, NULL, D3DCLEAR_TARGET, argb, 1.0f, 0);
	g_dev->Present(NULL, NULL, NULL, NULL);
}

// 3x5 block font for hex digits 0-F. Each row is 3 bits (bit2=left cell).
static const unsigned char s_font3x5[16][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
	{5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2},
	{7,5,7,5,7}, {7,5,7,1,7}, {7,5,7,5,5}, {6,5,6,5,6},
	{7,4,4,4,7}, {6,5,5,5,6}, {7,4,7,4,7}, {7,4,7,4,4},
};

// 3x5 uppercase letter glyphs A-Z (bit2=left cell), for readable text overlays.
static const unsigned char s_fontAZ[26][5] = {
	{7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4}, // A-F
	{7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7}, // G-L
	{5,7,7,5,5},{5,7,5,5,5},{7,5,5,5,7},{7,5,7,4,4},{7,5,5,7,3},{7,5,6,5,5}, // M-R
	{7,4,7,1,7},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5}, // S-X
	{5,5,2,2,2},{7,1,2,4,7},                                                 // Y-Z
};

static const unsigned char* glyph_for(char c)
{
	static const unsigned char sp[5]   = {0,0,0,0,0};
	static const unsigned char col[5]  = {0,2,0,2,0};
	static const unsigned char dot[5]  = {0,0,0,0,2};
	static const unsigned char bs[5]   = {4,4,2,1,1};
	static const unsigned char fs[5]   = {1,1,2,4,4};
	static const unsigned char us[5]   = {0,0,0,0,7};
	static const unsigned char dsh[5]  = {0,0,7,0,0};
	if (c >= '0' && c <= '9') return s_font3x5[c - '0'];
	if (c >= 'a' && c <= 'z') c -= 32;
	if (c >= 'A' && c <= 'Z') return s_fontAZ[c - 'A'];
	switch (c) {
		case ':': return col; case '.': return dot; case '\\': return bs;
		case '/': return fs;  case '_': return us;  case '-': return dsh;
	}
	return sp;
}

static void draw_hex_row(unsigned long v, int top, unsigned long color)
{
	const int cell = 11, digitW = 3 * cell + 8, startX = 14;
	for (int d = 0; d < 8; d++)
	{
		int nib = (int)((v >> ((7 - d) * 4)) & 0xF);
		int dx = startX + d * digitW;
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 3; c++)
				if (s_font3x5[nib][r] & (4 >> c))
				{
					D3DRECT rc;
					rc.x1 = dx + c * cell;      rc.y1 = top + r * cell;
					rc.x2 = rc.x1 + cell - 2;   rc.y2 = rc.y1 + cell - 2;
					g_dev->Clear(1, &rc, D3DCLEAR_TARGET, color, 1.0f, 0);
				}
	}
}

// Draws three 32-bit values as rows of readable hex digits (row0 red, row1
// green, row2 blue). Screenshot reads back exact hex — no bit-counting.
// Draw a string of glyphs at (x,y). Does NOT clear or present — caller composes.
static void draw_text(int x, int y, unsigned long color, const char* str, int cell)
{
	int cx = x;
	for (const char* p = str; *p; ++p)
	{
		const unsigned char* g = glyph_for(*p);
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 3; c++)
				if (g[r] & (4 >> c))
				{
					D3DRECT rc;
					rc.x1 = cx + c * cell;    rc.y1 = y + r * cell;
					rc.x2 = rc.x1 + cell - 1; rc.y2 = rc.y1 + cell - 1;
					g_dev->Clear(1, &rc, D3DCLEAR_TARGET, color, 1.0f, 0);
				}
		cx += 4 * cell;
		if (cx > 620) break;
	}
}

void xbox_d3d8_debug_text(int x, int y, unsigned long argb, const char *str)
{
	if (!g_dev || !str) return;
	draw_text(x, y, argb, str, 5);
}

// last path handed to the file layer (set by FileUtil/fd shim), for crash diag.
extern "C" char g_xbox_last_open[260];
extern "C" char g_xbox_stage[64]; // coarse boot-stage marker
extern "C" char          g_stage_tag[32][8];  // per-stage name (free-RAM log)
extern "C" unsigned char g_stage_free[32];    // free MB at each stage
extern "C" int           g_stage_n;

// Compact hex row (small cells) for the raw-stack dump.
static void draw_hex_row_small(unsigned long v, int top, unsigned long color)
{
	const int cell = 6, digitW = 3 * cell + 4, startX = 14;
	for (int d = 0; d < 8; d++)
	{
		int nib = (int)((v >> ((7 - d) * 4)) & 0xF);
		int dx = startX + d * digitW;
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 3; c++)
				if (s_font3x5[nib][r] & (4 >> c))
				{
					D3DRECT rc;
					rc.x1 = dx + c * cell;      rc.y1 = top + r * cell;
					rc.x2 = rc.x1 + cell - 1;   rc.y2 = rc.y1 + cell - 1;
					g_dev->Clear(1, &rc, D3DCLEAR_TARGET, color, 1.0f, 0);
				}
	}
}

// Raw stack dump: vals[0]=EIP, vals[1]=ESP, vals[2..]=stack DWORDs from ESP.
extern "C" void xbox_d3d8_draw_stackdump(unsigned long* vals, int n)
{
	if (!g_dev) return;
	g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF101018, 1.0f, 0);
	for (int i = 0; i < n && i < 12; i++)
	{
		unsigned long col = (i == 0) ? 0xFFFF5050   // EIP = red
		                  : (i == 1) ? 0xFF50FF50   // ESP = green
		                  : 0xFF7090FF;             // stack = blue
		draw_hex_row_small(vals[i], 12 + i * 38, col);
	}
	// Thread tag + stage (crash filter prepends MAIN/WORKER) — confirms whether a
	// worker thread (missing Xapi per-thread init) is the one faulting.
	draw_text(210, 20, 0xFFFFDD44, g_xbox_stage, 4);
	// Last file path handed to the file layer: show the FILENAME (after the last
	// backslash) so we can identify which file the failing open/read touched.
	{
		const char* fn = g_xbox_last_open;
		for (const char* p = g_xbox_last_open; *p; p++) if (*p == '\\') fn = p + 1;
		draw_text(210, 60, 0xFFFFFFFF, fn, 3);
	}
	// Physical RAM used / free / total (MB) — confirm whether we're near the limit.
	{
		MEMORYSTATUS ms; ms.dwLength = sizeof(ms);
		GlobalMemoryStatus(&ms);
		char mem[64];
		wsprintfA(mem, "MEM USED %u  FREE %u  TOT %u MB",
		          (unsigned)((ms.dwTotalPhys - ms.dwAvailPhys) >> 20),
		          (unsigned)(ms.dwAvailPhys >> 20),
		          (unsigned)(ms.dwTotalPhys >> 20));
		draw_text(210, 100, 0xFF80FF80, mem, 3);
	}
	// Per-stage free-RAM (MB) progression — find WHERE memory dropped.
	{
		int start = g_stage_n > 11 ? g_stage_n - 11 : 0;
		for (int k = start; k < g_stage_n; k++)
		{
			int slot = k & 15;
			char line[32];
			wsprintfA(line, "%s %u", g_stage_tag[slot], (unsigned)g_stage_free[slot]);
			draw_text(360, 20 + (k - start) * 34, 0xFF80C0FF, line, 3);
		}
	}
	g_dev->Present(NULL, NULL, NULL, NULL);
}

// Live heartbeat: green counter (advances = alive), yellow guest PC, orange
// stage. Used both for init-stage progress and the CPU-loop heartbeat, so a
// softlock shows the last stage reached and whether it's still ticking.
extern "C" void xbox_d3d8_hbtext(unsigned counter, unsigned pc)
{
	if (!g_dev) return;
	g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF001830, 1.0f, 0);
	draw_hex_row(counter, 80,  0xFF00FF00);  // heartbeat counter (advances if alive)
	draw_hex_row(pc,      220, 0xFFFFFF00);  // current guest PC
	draw_text(8, 400, 0xFFFFDD44, g_xbox_stage, 5);
	g_dev->Present(NULL, NULL, NULL, NULL);
}

// Persistent watchdog overlay (own thread): RAM footprint + current stage +
// whether the CPU loop is advancing + live guest PC. Runs independently of the
// (possibly stuck) emulation, so we always see memory and liveness.
extern "C" volatile unsigned g_xbox_cpucount;
extern "C" volatile unsigned g_xbox_cpustate;
extern "C" unsigned xbox_live_pc();
extern "C" unsigned xbox_live_lr();
extern "C" unsigned xbox_live_ctr();
extern "C" unsigned xbox_live_srr0();
extern "C" unsigned xbox_live_srr1();
extern "C" unsigned xbox_live_msr();
extern "C" unsigned xbox_live_gpr(unsigned);
extern "C" unsigned xbox_live_sprg(unsigned);
extern "C" unsigned xbox_live_intcause();
extern "C" unsigned xbox_live_intmask();
extern "C" unsigned xbox_live_exceptions();
extern "C" unsigned xbox_live_extsrc();
extern "C" unsigned short g_xbox_fpcw;
extern "C" unsigned g_xbox_mxcsr;
extern "C" unsigned xbox_guest_op(unsigned);
// Exception-take counters (which exception is looping) + last DSI faulting address.
extern "C" unsigned g_exc_dsi, g_exc_isi, g_exc_prog, g_exc_fpu;
extern "C" unsigned g_exc_ext, g_exc_dec, g_exc_align, g_exc_sys;
extern "C" unsigned g_exc_dar, g_exc_srr0;
extern "C" volatile unsigned g_xbox_adma;
extern "C" volatile unsigned g_xbox_adma_en;
extern "C" void xbox_d3d8_watchpaint(unsigned wd)
{
	// NOTE: intentionally NOT gated on g_xfbSeen — the game presents a few frames
	// then hangs, and we need the guest PC at that stall. TryEnter means live
	// frames still win the device; we only paint when the emu isn't presenting.
	if (!g_dev) return;
	if (!TryEnterCriticalSection(&g_devLock)) return; // emu presenting -> skip
	// The DX8 video backend binds the EFB (an offscreen texture) as the render
	// target, so the overlay's Clear-drawn glyphs would land off-screen and Present
	// would show the untouched backbuffer. Force the visible backbuffer as the RT so
	// the diagnostics are drawn where they can be seen, regardless of the backend.
	{
		IDirect3DSurface8* bb = NULL;
		if (SUCCEEDED(g_dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
		{
			g_dev->SetRenderTarget(bb, NULL);
			bb->Release();
		}
	}
	MEMORYSTATUS ms; ms.dwLength = sizeof(ms);
	GlobalMemoryStatus(&ms);
	char line[80];
	g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF0A0F14, 1.0f, 0);
	// Header: current physical MEM.
	wsprintfA(line, "MEM USED %u  FREE %u / %u MB",
	          (unsigned)((ms.dwTotalPhys - ms.dwAvailPhys) >> 20),
	          (unsigned)(ms.dwAvailPhys >> 20), (unsigned)(ms.dwTotalPhys >> 20));
	draw_text(8, 6, 0xFF80FF80, line, 3);

	// LEFT COLUMN — TRUE PER-STAGE MEMORY MAP: every boot stage with its free-RAM
	// (MB). The DROP between consecutive rows = memory that stage allocated, so
	// the big drops localize exactly where the RAM goes.
	int n = g_stage_n; if (n > 26) n = 26;
	for (int k = 0; k < n; k++)
	{
		unsigned f = (unsigned)g_stage_free[k & 31];
		unsigned pf = k ? (unsigned)g_stage_free[(k - 1) & 31] : f;
		int drop = (int)pf - (int)f;                 // MB this stage consumed
		unsigned col = (drop >= 8) ? 0xFFFF6060      // red: big consumer (>=8MB)
		             : (drop >= 3) ? 0xFFFFC040       // amber: moderate
		             : 0xFF80C0FF;                    // blue: small
		wsprintfA(line, "%s %u  (-%d)", g_stage_tag[k & 31], f, drop < 0 ? 0 : drop);
		draw_text(8, 40 + k * 16, col, line, 2);
	}

	// RIGHT COLUMN — live stats.
	draw_text(340, 40, 0xFFFFDD44, g_xbox_stage, 2);
	wsprintfA(line, "PC   %08X", xbox_live_pc());
	draw_text(340, 62, 0xFFFFFF00, line, 2);
	wsprintfA(line, "CPU %u  ST %u", (unsigned)g_xbox_cpucount, (unsigned)g_xbox_cpustate);
	draw_text(340, 82, 0xFFFFFF00, line, 2);
	wsprintfA(line, "RUNQ %08X", xbox_guest_op(0x80488a30));
	draw_text(340, 102, 0xFFFF80FF, line, 2);
	wsprintfA(line, "CURT %08X", xbox_guest_op(0x800000e4));
	draw_text(340, 122, 0xFFFF80FF, line, 2);
	wsprintfA(line, "ADMA %u  EN %u", (unsigned)g_xbox_adma, (unsigned)g_xbox_adma_en);
	draw_text(340, 142, 0xFF80FFFF, line, 2);
	wsprintfA(line, "WD %u", wd);
	draw_text(340, 162, 0xFF60C0FF, line, 2);
	// How did we reach the current PC? LR/CTR = indirect-branch targets;
	// SRR0/SRR1 nonzero = arrived via exception/interrupt (SRR0 = interrupted PC).
	wsprintfA(line, "LR   %08X", xbox_live_lr());
	draw_text(340, 188, 0xFF40FFC0, line, 2);
	wsprintfA(line, "CTR  %08X", xbox_live_ctr());
	draw_text(340, 208, 0xFF40FFC0, line, 2);
	wsprintfA(line, "SRR0 %08X", xbox_live_srr0());
	draw_text(340, 228, 0xFFFF8040, line, 2);
	wsprintfA(line, "SRR1 %08X", xbox_live_srr1());
	draw_text(340, 248, 0xFFFF8040, line, 2);
	wsprintfA(line, "MSR  %08X", xbox_live_msr());
	draw_text(340, 268, 0xFFFF8040, line, 2);
	// r4 = the OSContext pointer the faulting stw uses; r1 = stack; SPRG0/1 =
	// exception scratch. A bad r4 confirms the exception entry set it up wrong.
	wsprintfA(line, "r4   %08X", xbox_live_gpr(4));
	draw_text(340, 292, 0xFFFF4040, line, 2);
	wsprintfA(line, "r1   %08X", xbox_live_gpr(1));
	draw_text(340, 312, 0xFFC0C0FF, line, 2);
	wsprintfA(line, "r3   %08X", xbox_live_gpr(3));
	draw_text(340, 332, 0xFFC0C0FF, line, 2);
	wsprintfA(line, "SPRG0 %08X", xbox_live_sprg(0));
	draw_text(340, 352, 0xFFC0C0FF, line, 2);
	wsprintfA(line, "SPRG1 %08X", xbox_live_sprg(1));
	draw_text(340, 372, 0xFFC0C0FF, line, 2);
	// PI interrupt cause/mask + pending-exceptions. If IC&IM==0 but EXC has bit
	// 0x200 (EXCEPTION_EXTERNAL_INT), the JIT is stuck taking a phantom interrupt.
	wsprintfA(line, "IC %08X  IM %08X", xbox_live_intcause(), xbox_live_intmask());
	draw_text(340, 396, 0xFF80FF80, line, 2);
	wsprintfA(line, "EXC %08X", xbox_live_exceptions());
	draw_text(340, 416, 0xFF80FF80, line, 2);
	// The context's saved SRR0 (OSContext+0x19c) = where returnFromInterrupt rfi's
	// back to. If cSRR0 == 8023346C the handler is rfi'ing into itself = the loop.
	// avenue-5: x87 control word (should be 027F = 53-bit RN masked) + MXCSR
	// (should be 1F80). If FCW != 027F, RXDK's _control87 didn't take and all
	// shared double math is wrong on both cores.
	// Exception-take counters: WHICH exception is looping. DSI = faulting load/store
	// (DAR = the address). If DSI storms with DAR=8041xxxx, the context-save store is
	// hitting unwritable guest memory. PRG = illegal instr (JIT emit bug). ISI = bad
	// instruction fetch. Frozen-all = not an exception storm (plain code loop).
	wsprintfA(line, "DSI %u ISI %u PRG %u", g_exc_dsi, g_exc_isi, g_exc_prog);
	draw_text(340, 436, 0xFF80FFC0, line, 2);
	// avenue-3 forensics: ExS = (cause&mask) when EXCEPTION_EXTERNAL_INT was last
	// set; OSM = the OS's own interrupt-disable words (0x800000C4|C8). Compare ExS
	// vs live IC&IM: equal+nonzero = real device stuck; ExS set but IC&IM==0 = phantom.
	wsprintfA(line, "EXT %u DEC %u DAR %08X", g_exc_ext, g_exc_dec, g_exc_dar);
	draw_text(340, 456, 0xFFFF80C0, line, 2);
	g_dev->Present(NULL, NULL, NULL, NULL);
	LeaveCriticalSection(&g_devLock);
}

void xbox_d3d8_draw_diag(unsigned long v0, unsigned long v1, unsigned long v2)
{
	if (!g_dev) return;
	g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF101018, 1.0f, 0); // dark bg
	draw_hex_row(v0, 40,  0xFFFF5050);
	draw_hex_row(v1, 170, 0xFF50FF50);
	draw_hex_row(v2, 300, 0xFF5080FF);
	// Bottom: boot stage + the TAIL of the last file path (filename stays visible
	// even when the long \Device\... prefix would crop off the right edge).
	draw_text(8, 405, 0xFFFFDD44, g_xbox_stage, 5);
	{
		const char* tail = g_xbox_last_open;
		int plen = 0; while (tail[plen]) plen++;
		if (plen > 46) tail += plen - 46;
		draw_text(8, 435, 0xFFFFFFFF, tail, 4);
	}
	g_dev->Present(NULL, NULL, NULL, NULL);
}

void xbox_d3d8_shutdown(void)
{
	if (g_xfbTex) { g_xfbTex->Release(); g_xfbTex = 0; }
	g_dev = 0; // device owned by main_xbox
}
