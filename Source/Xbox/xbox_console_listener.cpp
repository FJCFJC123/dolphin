// OG Xbox port: ConsoleListener replacement.
// The Xbox has no Win32 text console; all the desktop ConsoleListener does
// (screen buffers, COORD cursor math, color attributes) is meaningless here.
// Log output routes to the kernel debug channel via OutputDebugStringA, which
// a debugger / xbdm capture can read. Everything else is a no-op.

#include "ConsoleListener.h"
#include <windows.h> // shim -> xtl.h (OutputDebugStringA)

ConsoleListener::ConsoleListener()
{
	bUseColor = false;
}

ConsoleListener::~ConsoleListener()
{
}

void ConsoleListener::Open(bool, int, int, const char*) {}
void ConsoleListener::UpdateHandle() {}
void ConsoleListener::Close() {}
bool ConsoleListener::IsOpen() { return false; }
void ConsoleListener::LetterSpace(int, int) {}
void ConsoleListener::BufferWidthHeight(int, int, int, int, bool) {}
void ConsoleListener::PixelSpace(int, int, int, int, bool) {}

void ConsoleListener::Log(LogTypes::LOG_LEVELS, const char* Text)
{
	OutputDebugStringA(Text);
}

void ConsoleListener::ClearScreen(bool) {}
