// OG Xbox port: <iphlpapi.h> shim. RXDK has no IP Helper API; Wii networking
// (WII_Socket) is unused by the GameCube POC and its .cpp never runs — this
// just lets the header parse.
#ifndef _DOLPHIN_XBOX_IPHLPAPI_H_
#define _DOLPHIN_XBOX_IPHLPAPI_H_
#include <winsock2.h> // shim -> winsockx.h
#endif
