// OG Xbox port: <windows.h> shim.
// RXDK has no windows.h — the Xbox umbrella header is <xtl.h>. This redirects
// there and backfills the desktop-only Win32 bits xtl.h lacks (MessageBox
// icon flags, FormatMessage language macros, a few thread/process helpers).
// Kept minimal; extend reactively as the port surfaces more misses.
#ifndef _DOLPHIN_XBOX_WINDOWS_H_
#define _DOLPHIN_XBOX_WINDOWS_H_

#include <xtl.h>

// ---- Interlocked* : _NTOS_ suppresses RXDK's winbase versions (they clash
// with the VS2022 compiler intrinsics). Map the Win32 names to the intrinsics. ----
#include <intrin.h>
#pragma intrinsic(_InterlockedIncrement, _InterlockedDecrement, _InterlockedExchange, _InterlockedExchangeAdd, _InterlockedCompareExchange)
#ifndef InterlockedIncrement
#define InterlockedIncrement     _InterlockedIncrement
#define InterlockedDecrement     _InterlockedDecrement
#define InterlockedExchange      _InterlockedExchange
#define InterlockedExchangeAdd   _InterlockedExchangeAdd
#define InterlockedCompareExchange _InterlockedCompareExchange
#endif

// ---- MessageBox flags (MsgHandler.cpp; no MessageBox on Xbox — see shim) ----
#ifndef MB_OK
#define MB_OK                  0x00000000
#define MB_OKCANCEL            0x00000001
#define MB_YESNO               0x00000004
#define MB_ICONERROR           0x00000010
#define MB_ICONQUESTION        0x00000020
#define MB_ICONWARNING         0x00000030
#define MB_ICONINFORMATION     0x00000040
#define IDOK                   1
#define IDCANCEL               2
#define IDYES                  6
#define IDNO                   7
#endif

// ---- FormatMessage language macros (StringUtil GetLastErrorMsg) ----
#ifndef LANG_NEUTRAL
#define LANG_NEUTRAL      0x00
#define SUBLANG_DEFAULT   0x01
#define MAKELANGID(p, s)  ((((WORD)(s)) << 10) | (WORD)(p))
#endif
#ifndef FORMAT_MESSAGE_ALLOCATE_BUFFER
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#endif

// Desktop-only kernel helpers absent from the Xbox kernel. Declared here,
// implemented as no-op/best-effort in xbox_win32_shim.cpp.
#ifdef __cplusplus
extern "C" {
#endif
DWORD   Xbox_FormatMessageA(DWORD flags, const void* src, DWORD msgId, DWORD langId,
                            char* buffer, DWORD size, void* args);
DWORD_PTR Xbox_SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);
BOOL    Xbox_IsWow64Process(HANDLE proc, BOOL* wow64);
#ifdef __cplusplus
}
#endif

#ifndef FormatMessageA
#define FormatMessageA        Xbox_FormatMessageA
#endif
#ifndef SetThreadAffinityMask
#define SetThreadAffinityMask Xbox_SetThreadAffinityMask
#endif
#ifndef IsWow64Process
#define IsWow64Process        Xbox_IsWow64Process
#endif

#endif // _DOLPHIN_XBOX_WINDOWS_H_
