// OG Xbox port: <winsock2.h> shim.
// RXDK has no desktop winsock2.h; the Xbox socket API lives in <winsockx.h>.
// Networking is unused by the GameCube POC, but several device dispatch
// headers (SFML, ICMP.h, WII net) pull winsock2.h transitively — redirect so
// they parse. The linked networking .cpp files are excluded from the build.
#ifndef _DOLPHIN_XBOX_WINSOCK2_H_
#define _DOLPHIN_XBOX_WINSOCK2_H_
#include <xtl.h>
#include <winsockx.h>
// winsockx.h defines struct timeval but not this guard; set it so code that
// conditionally redefines timeval (libusb, WII hid) skips its own copy.
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
#endif
#endif
