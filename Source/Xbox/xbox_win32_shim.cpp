// OG Xbox port: implementations for the desktop-only Win32 helpers declared
// in shim/windows.h. The Xbox kernel lacks these; best-effort stubs suffice
// (Dolphin only uses them for logging / affinity hints, never correctness).

#include <windows.h> // our shim -> xtl.h
#include <stdio.h>
#include <string.h>

extern "C" {

DWORD Xbox_FormatMessageA(DWORD flags, const void* src, DWORD msgId, DWORD langId,
                          char* buffer, DWORD size, void* args)
{
	// No message-table service on Xbox. Emit the numeric code so the caller's
	// "GetLastError: <text>" logging still says something useful.
	if (!buffer || size == 0)
		return 0;
	int n = _snprintf(buffer, size, "error 0x%08x", (unsigned)msgId);
	if (n < 0) n = 0;
	buffer[size - 1] = '\0';
	return (DWORD)strlen(buffer);
}

DWORD_PTR Xbox_SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask)
{
	// The Xbox has a fixed single-core scheduling model with hardware threads
	// selected via XSetThreadProcessor; a desktop affinity mask is meaningless.
	(void)thread; (void)mask;
	return 1; // pretend prior affinity was CPU 0
}

BOOL Xbox_IsWow64Process(HANDLE proc, BOOL* wow64)
{
	(void)proc;
	if (wow64) *wow64 = FALSE; // native 32-bit, never WOW64
	return TRUE;
}

} // extern "C"
