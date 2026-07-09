// OG Xbox port: plain-C interface between Dolphin's VideoCommon/SW-renderer
// (C++ / Common.h world) and the D3D8 present backend (xtl.h world).
// Kept apart per the RXDK lesson (no TU includes both Common.h's Interlocked
// intrinsics and xtl.h -> C2733). Stemmed from the PS1 emu's xbox_dx8.h.
//
// Phase 2 renderer model: Dolphin's Software renderer rasterizes the GameCube
// frame into an XFB (RGBA8888) in main memory; this backend uploads that XFB
// to a dynamic texture and blits it fullscreen (the PS1 blit_vram_bg model).
// A real Flipper->NV2A TEV backend is a later phase.
#ifndef XBOX_D3D8_H
#define XBOX_D3D8_H

#ifdef __cplusplus
extern "C" {
#endif

// main_xbox creates the IDirect3DDevice8 (mode/HD fallback) and hands it over.
void  xbox_d3d8_set_device(void *dev);
int   xbox_d3d8_backbuffer_w(void);
int   xbox_d3d8_backbuffer_h(void);

// Present an RGBA8888 image (Dolphin XFB) fullscreen: upload -> quad -> Present.
void  xbox_d3d8_present_xfb(const unsigned char *rgba, int width, int height);

// Clear the backbuffer to an ARGB color and Present (boot diagnostics).
void  xbox_d3d8_clear_present(unsigned long argb);

// Diagnostic: paint three 32-bit values as rows of bit-bars (bright=1, dark=0,
// MSB left, gaps at byte boundaries) so a screenshot reads back exact hex with
// no font/log needed. Rows top->bottom = v0, v1, v2.
void  xbox_d3d8_draw_diag(unsigned long v0, unsigned long v1, unsigned long v2);

// Boot heartbeat: paints a color derived from the emulated PC until the first
// real XFB frame arrives, then no-ops forever. MAIN THREAD ONLY.
void  xbox_d3d8_heartbeat(unsigned long emulated_pc);
int   xbox_d3d8_xfb_seen(void);

// Present pump: uploads + draws the latest staged XFB frame. MAIN THREAD ONLY
// (all D3D happens on the main thread; the emu thread only stages frames via
// xbox_d3d8_present_xfb). Call every loop tick.
void  xbox_d3d8_pump(void);

// Tiny debug text overlay (kernel debug font) for the boot watchdog.
void  xbox_d3d8_debug_text(int x, int y, unsigned long argb, const char *str);

void  xbox_d3d8_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // XBOX_D3D8_H
