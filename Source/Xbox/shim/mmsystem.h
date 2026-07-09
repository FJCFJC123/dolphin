// OG Xbox port: <mmsystem.h> shim.
// RXDK has no mmsystem.h; timeGetTime() is an Xbox kernel export. Declare it
// (and the millisecond timer helpers Dolphin's Timer.cpp uses) here.
#ifndef _DOLPHIN_XBOX_MMSYSTEM_H_
#define _DOLPHIN_XBOX_MMSYSTEM_H_

#include <xtl.h>

#ifdef __cplusplus
extern "C" {
#endif
WINBASEAPI DWORD WINAPI timeGetTime(void);
#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_XBOX_MMSYSTEM_H_
