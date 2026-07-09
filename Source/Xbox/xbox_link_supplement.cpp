// OG Xbox port: link supplement — CRT/STL symbols RXDK's 2003 CRT and the
// Flycast shim set don't cover but Dolphin references. All standard-behavior
// implementations; no Dolphin-specific logic.

#include <xtl.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cwchar>

// ---- POSIX-ish CRT functions RXDK omits -------------------------------------
extern "C" {

// No working-directory concept on the Xbox; the "cwd" is always the mount root.
char* __cdecl _getcwd(char* buf, int size)
{
	if (buf && size > 2) { buf[0] = 'D'; buf[1] = ':'; buf[2] = '\0'; return buf; }
	return 0;
}
int __cdecl _chdir(const char*) { return 0; }

int __cdecl _chsize_s(int fd, __int64 size)
{
	// RXDK has neither _get_osfhandle nor _chsize. File truncation is only used
	// for memory-card/save-state resize — no-op for the first-boot POC.
	(void)fd; (void)size;
	return 0;
}

size_t __cdecl strnlen(const char* s, size_t maxlen)
{
	size_t n = 0;
	while (n < maxlen && s[n]) n++;
	return n;
}

// 16-byte-aligned _alloca stack probe (compiler helper; raw symbol
// __alloca_probe_16 = C identifier _alloca_probe_16). Rounds the requested
// size (EAX) up to a 16-byte multiple, then tail-jumps to RXDK's _chkstk.
void __cdecl _chkstk(void); // RXDK CRT
extern "C" void __declspec(naked) _alloca_probe_16(void)
{
	__asm {
		push ecx
		lea  ecx, [esp + 8]
		sub  ecx, eax
		and  ecx, 15
		add  eax, ecx
		sbb  ecx, ecx
		or   eax, ecx
		pop  ecx
		jmp  _chkstk
	}
}
// _set_controlfp: RXDK declares it (void return) but doesn't implement it.
// Match that signature and forward to _controlfp.
void __cdecl _set_controlfp(unsigned int newv, unsigned int mask)
{
	_controlfp(newv, mask);
}

// ---- Per-thread C locale API (CommonFuncs.h uselocale/newlocale shim) --------
// Our uselocale rewrite only ever forces the "C" numeric locale, so a dummy
// non-null handle + no-op configuration is sufficient.
static int g_locale_token = 1;
void*        __cdecl _create_locale(int, const char*)        { return &g_locale_token; }
void         __cdecl _free_locale(void*)                     {}
int          __cdecl _configthreadlocale(int)                { return 0; }
void*        __cdecl _get_current_locale(void)               { return &g_locale_token; }

} // extern "C"

// ---- Modern MSVC out-of-line vectorized <algorithm> helpers -----------------
// The v143 STL emits calls to these for find/find_first_of/max_element on
// trivially-comparable ranges. Provide scalar fallbacks (SSE1 build, small
// ranges — correctness over speed).
extern "C" {

// The v143 STL calls these via __stdcall (the @N-decorated symbols).
const void* __stdcall __std_find_trivial_2(const void* first, const void* last, unsigned short value)
{
	const unsigned short* f = (const unsigned short*)first;
	const unsigned short* l = (const unsigned short*)last;
	for (; f != l; ++f) if (*f == value) return f;
	return l;
}

size_t __stdcall __std_find_first_of_trivial_pos_1(
	const void* haystack, size_t hay_n, const void* needle, size_t needle_n)
{
	const unsigned char* h = (const unsigned char*)haystack;
	const unsigned char* n = (const unsigned char*)needle;
	for (size_t i = 0; i < hay_n; i++)
		for (size_t j = 0; j < needle_n; j++)
			if (h[i] == n[j]) return i;
	return hay_n;
}

// max_element over a trivially-comparable range; 3rd arg is the predicate
// tag (ignored — scalar unsigned-byte compare is the trivial default).
const void* __stdcall __std_max_element_1(const void* first, const void* last, int)
{
	const unsigned char* f = (const unsigned char*)first;
	const unsigned char* l = (const unsigned char*)last;
	if (f == l) return l;
	const unsigned char* best = f;
	for (const unsigned char* p = f + 1; p != l; ++p)
		if (*p > *best) best = p;
	return best;
}

} // extern "C"
