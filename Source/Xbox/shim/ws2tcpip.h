// OG Xbox port: <ws2tcpip.h> shim (see winsock2.h shim). Networking unused by
// the GameCube POC; redirect so the Wii net dispatch headers parse.
#ifndef _DOLPHIN_XBOX_WS2TCPIP_H_
#define _DOLPHIN_XBOX_WS2TCPIP_H_
#include <winsock2.h> // shim -> winsockx.h
#endif
