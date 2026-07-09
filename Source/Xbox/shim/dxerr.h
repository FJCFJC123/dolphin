// OG Xbox port: <dxerr.h> shim.
// RXDK lacks the DirectX error-string helpers. Dolphin's DSoundStream uses
// them only for human-readable error logging; return a fixed placeholder.
#ifndef _DOLPHIN_XBOX_DXERR_H_
#define _DOLPHIN_XBOX_DXERR_H_

#include <windows.h> // shim -> xtl.h (HRESULT, TCHAR)

static inline const char*  DXGetErrorStringA(HRESULT)      { return "DXERR"; }
static inline const char*  DXGetErrorDescriptionA(HRESULT) { return "DirectX error"; }
static inline const wchar_t* DXGetErrorStringW(HRESULT)      { return L"DXERR"; }
static inline const wchar_t* DXGetErrorDescriptionW(HRESULT) { return L"DirectX error"; }

#ifdef UNICODE
#define DXGetErrorString      DXGetErrorStringW
#define DXGetErrorDescription DXGetErrorDescriptionW
#else
#define DXGetErrorString      DXGetErrorStringA
#define DXGetErrorDescription DXGetErrorDescriptionA
#endif

#endif // _DOLPHIN_XBOX_DXERR_H_
