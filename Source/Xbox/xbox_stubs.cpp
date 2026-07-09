// OG Xbox port: stubs for excluded features referenced by the compiled GC core.
// Netplay, Wii peripherals, GBA/BBA/Gecko EXI devices, OpenCL texture decode,
// AVI dump, the desktop render window, and the IL JIT are all excluded from the
// Xbox build; these no-op/false stubs satisfy the dangling references so the
// GameCube path links. None are reachable in normal GC operation.

#include "Common.h"

// ---- MSVC CRT x87 FP classification helpers (missing from the RXDK CRT) ------
// At /Od with the /arch:SSE FP model, MSVC emits calls to _dtest/_ldtest for
// double/long-double NaN/Inf handling; the old RXDK CRT lacks them (they inline
// at /O2, so the gap only appears at /Od). Pure integer bit-tests — no float
// compares, which would re-emit __dtest and recurse. Codes per MSVC <ymath.h>:
// _NANCODE=2, _INFCODE=1, _DENORM=-2, _FINITE=-1, zero=0.
extern "C" short _dtest(double *px)
{
	unsigned __int64 bits = *(unsigned __int64 *)px;
	unsigned int exp = (unsigned int)((bits >> 52) & 0x7FF);
	unsigned __int64 mant = bits & 0xFFFFFFFFFFFFFui64;
	if (exp == 0x7FF) return mant ? (short)2 : (short)1;   // NaN : INF
	if (exp == 0)     return mant ? (short)-2 : (short)0;  // DENORM : ZERO
	return (short)-1;                                      // FINITE
}
extern "C" short _ldtest(long double *px)
{
	return _dtest((double *)px);  // long double == double (8 bytes) on MSVC x86
}

// ---- byteswap intrinsics (inlined to BSWAP at /O2, emitted as calls at /Od;
// the RXDK CRT lacks the out-of-line versions). This whole file is compiled /Od
// (see vcxproj) so intrinsics are off and these definitions are allowed.
extern "C" unsigned short _byteswap_ushort(unsigned short v)
{
	return (unsigned short)((v >> 8) | (v << 8));
}
extern "C" unsigned long _byteswap_ulong(unsigned long v)
{
	return ((v & 0x000000FFUL) << 24) | ((v & 0x0000FF00UL) << 8) |
	       ((v & 0x00FF0000UL) >> 8)  | ((v & 0xFF000000UL) >> 24);
}
extern "C" unsigned __int64 _byteswap_uint64(unsigned __int64 v)
{
	return ((v & 0x00000000000000FFui64) << 56) | ((v & 0x000000000000FF00ui64) << 40) |
	       ((v & 0x0000000000FF0000ui64) << 24) | ((v & 0x00000000FF000000ui64) << 8)  |
	       ((v & 0x000000FF00000000ui64) >> 8)  | ((v & 0x0000FF0000000000ui64) >> 24) |
	       ((v & 0x00FF000000000000ui64) >> 40) | ((v & 0xFF00000000000000ui64) >> 56);
}

// ---- Wii remote (real) ------------------------------------------------------
unsigned int g_wiimote_sources[5] = {0, 0, 0, 0, 0};
namespace WiimoteReal
{
	void Initialize(bool) {}
	void Stop() {}
	void Shutdown() {}
	void Resume() {}
	void Pause() {}
	void Refresh() {}
	void LoadSettings() {}
}

// ---- Netplay ----------------------------------------------------------------
namespace NetPlay
{
	bool IsNetPlayRunning() { return false; }
}

// ---- OpenCL texture decode (desktop GPU path) -------------------------------
namespace OpenCL
{
	bool Initialize() { return false; }
	void Destroy() {}
}
void TexDecoder_OpenCL_Initialize() {}
void TexDecoder_OpenCL_Shutdown() {}

// ---- GBA link cable waiter --------------------------------------------------
void GBAConnectionWaiter_Shutdown() {}

// ---- IL JIT tables (we use Jit64, not Jit64IL) ------------------------------
namespace JitILTables { void InitTables() {} }

// ---- Desktop render window --------------------------------------------------
namespace EmuWindow { void Close() {} }
