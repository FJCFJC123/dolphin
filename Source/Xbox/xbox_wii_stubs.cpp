// OG Xbox port: stubs for excluded features referenced by compiled GC code.
// The socket-heavy Wii networking devices, real Wiimote, UDP wiimote, netplay,
// GBA/BBA/Gecko EXI devices, desktop DirectSound, physical-drive reader, AVI
// dump, and OpenCL texture decode don't build on RXDK (BSD sockets / libusb /
// desktop APIs) and are never reached in GameCube operation. These stubs
// satisfy the dangling factory/callback references so the GC path links.
//
// We include the real Dolphin headers so the (class-member) signatures match
// exactly; the bodies are inert.

#include "Common.h"
#include <iomanip> // pull std::setw's out-of-line instantiation

// ---- Netplay settings + per-device netplay callbacks ------------------------
#include "../../Core/Src/NetPlayProto.h"
NetSettings g_NetPlaySettings;

#include "../../Core/Src/HW/SI_Device.h"
#include "../../Core/Src/HW/SI_DeviceGCController.h"
#include "../../Core/Src/HW/SI_DeviceGCSteeringWheel.h"
#include "../../Core/Src/HW/SI_DeviceDanceMat.h"
bool CSIDevice_GCController::NetPlay_GetInput(u8, SPADStatus, u32*) { return false; }
u8   CSIDevice_GCController::NetPlay_InGamePadToLocalPad(u8 n) { return n; }
bool CSIDevice_GCSteeringWheel::NetPlay_GetInput(u8, SPADStatus, u32*) { return false; }
u8   CSIDevice_GCSteeringWheel::NetPlay_InGamePadToLocalPad(u8 n) { return n; }
bool CSIDevice_DanceMat::NetPlay_GetInput(u8, SPADStatus, u32*) { return false; }
u8   CSIDevice_DanceMat::NetPlay_InGamePadToLocalPad(u8 n) { return n; }

#include "../../Core/Src/HW/EXI_DeviceIPL.h"
u32 CEXIIPL::NetPlay_GetGCTime() { return 0; }

// ---- Wii IPC wiimote netplay hooks ------------------------------------------
#include "../../Core/Src/IPC_HLE/WII_IPC_HLE_WiiMote.h"
int  CWII_IPC_HLE_WiiMote::NetPlay_GetWiimoteNum(int) { return 0; }
bool CWII_IPC_HLE_WiiMote::NetPlay_WiimoteInput(int, u16, const void*, u32&) { return false; }

#include "../../Core/Src/IPC_HLE/WII_IPC_HLE_Device_usb.h"
void CWII_IPC_HLE_Device_usb_oh1_57e_305::NetPlay_WiimoteUpdate(int) {}

// ---- Real wiimote (bluetooth) -----------------------------------------------
#include "../../Core/Src/HW/WiimoteReal/WiimoteReal.h"
namespace WiimoteReal {
	Wiimote* g_wiimotes[MAX_BBMOTES] = {0};
	std::recursive_mutex g_refresh_lock;
	void InterruptChannel(int, u16, const void*, u32) {}
	void StateChange(EMUSTATE_CHANGE) {}
	void Update(int) {}
	void Wiimote::EnableDataReporting(u8) {}
	void Wiimote::SetChannel(u16) {}
	void Wiimote::QueueReport(u8, const void*, unsigned int) {}
	static const std::vector<u8> s_emptyQueue;
	const std::vector<u8>& Wiimote::ProcessReadQueue() { return s_emptyQueue; }
}

// ---- UDP wiimote (network sensor) -------------------------------------------
#include "UDPWiimote.h"
void UDPWiimote::getAccel(float&, float&, float&) {}
u32  UDPWiimote::getButtons() { return 0; }
void UDPWiimote::getIR(float&, float&) {}
void UDPWiimote::getNunchuck(float&, float&, u8&) {}
void UDPWiimote::getNunchuckAccel(float&, float&, float&) {}

// ---- BBA ethernet + Gecko EXI devices ---------------------------------------
#include "../../Core/Src/HW/EXI_DeviceEthernet.h"
bool CEXIETHERNET::Activate() { return false; }
void CEXIETHERNET::Deactivate() {}
bool CEXIETHERNET::RecvStart() { return false; }
void CEXIETHERNET::RecvStop() {}
bool CEXIETHERNET::SendFrame(u8*, u32) { return false; }

#include "../../Core/Src/HW/EXI_DeviceGecko.h"
GeckoSockServer::GeckoSockServer() {}
GeckoSockServer::~GeckoSockServer() {}
void CEXIGecko::ImmReadWrite(u32&, u32) {}

// ---- Physical drive reader / AVI dump / OpenCL texture decode ----------------
#include "DriveBlob.h"
DiscIO::DriveReader* DiscIO::DriveReader::Create(const char*) { return 0; }

#include "AVIDump.h"
void AVIDump::Stop() {}

#include "TextureDecoder.h"
PC_TexFormat TexDecoder_Decode_OpenCL(u8*, const u8*, int, int, int, int, int, bool)
{ return PC_TEX_FMT_NONE; }

// ---- GBA link SI device + UDP-wiimote wrapper (SFML-socket backed) ----------
// SFML_Network.lib is a desktop build (needs __imp__ winsock DLL imports), so
// the sf::SocketTCP pieces the GBA device inherits are stubbed inert here.
#include "../../Core/Src/HW/SI_DeviceGBA.h"
namespace sf
{
	SocketTCP::SocketTCP() {}
}
GBASockServer::GBASockServer() {}
GBASockServer::~GBASockServer() {}
void GBASockServer::Transfer(char*) {}
CSIDevice_GBA::CSIDevice_GBA(SIDevices device, int index)
	: ISIDevice(device, index) {}
int CSIDevice_GBA::RunBuffer(u8*, int) { return 0; }

#include "UDPWrapper.h"
UDPWrapper::UDPWrapper(int idx, const char* const name)
	: ControllerEmu::ControlGroup(name), inst(0), index(idx),
	  updIR(false), updAccel(false), updButt(false), updNun(false),
	  updNunAccel(false), udpEn(false), port("4434") {}
UDPWrapper::~UDPWrapper() {}
void UDPWrapper::LoadConfig(IniFile::Section*, const std::string&, const std::string&) {}
void UDPWrapper::SaveConfig(IniFile::Section*, const std::string&, const std::string&) {}
void UDPWrapper::Refresh() {}

