// OG Xbox port: raw-Win32 CRT file descriptor layer.
//
// RXDK's CRT file layer (_pioinfo / __free_osfhnd) is unusable in our setup —
// Dolphin's first fopen/ofstream faults in __free_osfhnd (code C0000005, addr
// 0xFFFFFFFC), and it caps out at _SYS_OPEN==20 handles anyway. The PS1 emu
// sidesteps CRT stdio entirely with raw CreateFile/ReadFile/etc.; we do the
// same but at the fd seam, so fopen(), std::ifstream (basic_filebuf), and
// File::IOFile all keep working — they sit on _open/_read/_write/_lseeki64/
// _close, which we override here with a large HANDLE table. /FORCE:MULTIPLE
// makes these win over RXDK's libcmt versions.

#include <xtl.h>
#include <errno.h>
#include <stdlib.h> // free
#include <stdio.h>  // FILE
#include <stdarg.h> // va_list

// Locale-free formatter for the Dolphin string layer. RXDK's CRT *printf walk
// the mbc/locale tables (__updatetmbcinfo) through our fake _locale_t and fault;
// wvsprintfA is a plain Win32 formatter with no locale dependency. Caveat: it
// does NOT support %f/%e/%g (floats) and caps output near 1024 chars — fine for
// panic/log/path strings. Declared here where xtl.h is in scope.
extern "C" int xbox_vformat(char* out, const char* fmt, va_list args)
{
	return wvsprintfA(out, fmt, args);
}

// OG Xbox port: C++ streams (std::ifstream/ofstream) open via the CRT's
// _wfsopen, which runs RXDK's broken path-resolution (XGetSectionHandleA,
// GetDiskFreeSpaceExA, etc. — all fault on null globals). Fail all such opens:
// streams are non-critical here (shader/disk caches, image dumps); every needed
// read/write goes through File::IOFile (raw Win32). Returning NULL sets the
// stream's failbit and callers skip the optional file op.
extern "C" FILE* __cdecl _wfsopen(const wchar_t*, const wchar_t*, int) { return 0; }

// This CRT's std C++ streams open through std::_Fiopen (fiopen.obj), and
// basic_filebuf calls it by DIRECT address inside libcpmt (a symbol override
// only catches external callers), so we INLINE-DETOUR it below (near install_one)
// to return NULL — failing stream opens before they enter RXDK's broken resolver,
// without touching fopen/IOFile. Just declare it here to take its address.
namespace std { FILE* __cdecl _Fiopen(const char*, int, int); }

// OG Xbox port: RXDK routes operator delete(NULL) straight to XMemFree(NULL),
// which faults (reads a block header at ~-10 => 0xFFFFFFF6) instead of being a
// no-op. The C++ standard requires `delete`/`delete[]` on a null pointer to do
// nothing — guard it here (/FORCE:MULTIPLE makes these win over libcpmt's).
void operator delete(void* p) throw()   { if (p) free(p); }
void operator delete[](void* p) throw() { if (p) free(p); }

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

// MSVC _O_* open flags (RXDK fcntl.h values).
#define XO_RDONLY 0x0000
#define XO_WRONLY 0x0001
#define XO_RDWR   0x0002
#define XO_APPEND 0x0008
#define XO_CREAT  0x0100
#define XO_TRUNC  0x0200
#define XO_EXCL   0x0400

// Last path handed to the file layer — shown on the crash screen for diagnosis.
extern "C" char g_xbox_last_open[260] = "(none)";
extern "C" void xbox_note_path(const char* p)
{
	int i = 0;
	for (; p[i] && i < 259; i++) g_xbox_last_open[i] = p[i];
	g_xbox_last_open[i] = 0;
}

// Coarse boot-stage marker — shown on the crash screen to localize the fault.
extern "C" char g_xbox_stage[64] = "start";
// Per-stage free-RAM (MB) log: each xbox_note_stage() records the current free
// physical RAM so the crash screen can show WHERE memory dropped. Ring of 16.
extern "C" char          g_stage_tag[32][8];  // first 7 chars of each stage name
extern "C" unsigned char g_stage_free[32];    // free MB at that stage
extern "C" int           g_stage_n = 0;        // count of stages recorded
extern "C" volatile unsigned g_xbox_cpucount = 0; // CPU-loop iterations (watchdog)
extern "C" volatile unsigned g_xbox_cpustate = 0; // last PowerPC CPU state
char          g_stage_tag[32][8];
unsigned char g_stage_free[32];
extern "C" void xbox_note_stage(const char* s)
{
	int i = 0;
	for (; s[i] && i < 63; i++) g_xbox_stage[i] = s[i];
	g_xbox_stage[i] = 0;

	int slot = g_stage_n & 31;
	int j = 0; for (; s[j] && j < 7; j++) g_stage_tag[slot][j] = s[j];
	g_stage_tag[slot][j] = 0;
	MEMORYSTATUS ms; ms.dwLength = sizeof(ms);
	GlobalMemoryStatus(&ms);
	g_stage_free[slot] = (unsigned char)((ms.dwAvailPhys >> 20) & 0xFF);
	g_stage_n++;
	// (the watchdog thread paints the current stage + MEM continuously)
}

// ---------------------------------------------------------------------------
// Detour the buggy drive-letter resolver. RXDK's XapipUpdateGetCurrentDDriveMapping
// reallocs a per-thread buffer that is NULL on non-main threads -> RtlReAllocateHeap
// (NULL) -> fault at 0xFFFFFFF6. A symbol override only catches external callers;
// xapilib calls it internally by direct address. So we INLINE-DETOUR the real
// function: overwrite its first 5 bytes with a jmp to our stub, catching every
// caller. The stub records the caller and returns STATUS_SUCCESS (the D: mapping
// doesn't change during a run, so refresh callers tolerate the no-op).
extern "C" unsigned long g_xapip_caller = 0;

// Drive-mapping refresh (@8): pure side-effect, so a no-op returning SUCCESS is safe.
static __declspec(naked) void ddrive_hook_impl()
{
	__asm {
		mov eax, dword ptr [esp]
		mov dword ptr [g_xapip_caller], eax
		xor eax, eax                          // STATUS_SUCCESS
		ret 8
	}
}

// Drive-letter -> directory (@24): returns an output; give callers a clean
// "object name not found" so they handle the absence instead of reading garbage.
static __declspec(naked) void mapletter_hook_impl()
{
	__asm {
		mov eax, dword ptr [esp]
		mov dword ptr [g_xapip_caller], eax
		mov eax, 0xC0000034                   // STATUS_OBJECT_NAME_NOT_FOUND
		ret 24
	}
}

// Content-signature installation (@16/@16/@12): irrelevant for an emulator and the
// TOP of the drive-resolution chain that faults on the emu thread. No-op = SUCCESS.
static __declspec(naked) void contsig16_hook_impl()
{
	__asm { mov eax, dword ptr [esp]
	        mov dword ptr [g_xapip_caller], eax
	        xor eax, eax
	        ret 16 }
}
static __declspec(naked) void contsig12_hook_impl()
{
	__asm { xor eax, eax
	        ret 12 }
}

// RXDK's real functions — we take their addresses and patch their entries.
extern "C" long __stdcall XapipUpdateGetCurrentDDriveMapping(void*, void*);
extern "C" long __stdcall XapiMapLetterToDirectory(void*, void*, void*, void*, void*, void*);
// Plain XInstallContentSignatures is declared by xtl.h; these two are not.
extern "C" long __stdcall XInstallContentSignaturesWithFileName(void*, void*, void*, void*);
extern "C" long __stdcall XInstallContentSignaturesEx(void*, void*, void*, void*);
// Xbox kernel: make a code range writable before we patch it.
extern "C" void __stdcall MmSetAddressProtect(void* base, unsigned long bytes, unsigned long protect);
#define X_PAGE_EXECUTE_READWRITE 0x40

static void install_one(void* target, void* hook)
{
	unsigned char* tgt = (unsigned char*)target;
	long rel = (long)((unsigned char*)hook - (tgt + 5));
	__try
	{
		MmSetAddressProtect(tgt, 5, X_PAGE_EXECUTE_READWRITE);
		tgt[0] = 0xE9;                        // JMP rel32
		*(long*)(tgt + 1) = rel;
	}
	__except (1) { /* patch failed: leave original, keep booting */ }
}

// Transparent detour on XMemFree: reliably capture caller + freed pointer (stack
// scans fail — FPO), then pass through to the real function via a trampoline.
extern "C" unsigned long g_xmf_caller = 0;
extern "C" unsigned long g_xmf_ptr = 0;
static unsigned char g_xmf_tramp[16];

static __declspec(naked) void xmemfree_hook()
{
	__asm {
		mov eax, dword ptr [esp]      // caller return address
		mov dword ptr [g_xmf_caller], eax
		mov eax, dword ptr [esp+4]    // ptr being freed
		mov dword ptr [g_xmf_ptr], eax
		lea eax, g_xmf_tramp          // pass through to the real XMemFree
		jmp eax
	}
}

extern "C" void xbox_install_xmemfree_hook()
{
	unsigned char* tgt = (unsigned char*)&XMemFree;
	__try
	{
		// Trampoline = original 5 bytes + jmp to XMemFree+5.
		MmSetAddressProtect(g_xmf_tramp, sizeof(g_xmf_tramp), X_PAGE_EXECUTE_READWRITE);
		for (int i = 0; i < 5; i++) g_xmf_tramp[i] = tgt[i];
		g_xmf_tramp[5] = 0xE9;
		*(long*)(g_xmf_tramp + 6) = (long)((tgt + 5) - (g_xmf_tramp + 10));
		// Patch XMemFree entry -> our hook.
		MmSetAddressProtect(tgt, 5, X_PAGE_EXECUTE_READWRITE);
		tgt[0] = 0xE9;
		*(long*)(tgt + 1) = (long)((unsigned char*)xmemfree_hook - (tgt + 5));
	}
	__except (1) {}
}

// Same transparent-recorder trampoline for XapiNukeDirectoryFromHandle (the
// current crash). Its prologue's clean boundary is at byte 6 (push ebp; mov
// ebp,esp; sub esp,4Ch). Captures the caller so we learn what Dolphin op nukes
// a directory during emulation.
extern "C" long __stdcall XapiNukeDirectoryFromHandle(void*, void*);
extern "C" unsigned long g_nuke_caller = 0;
static unsigned char g_nuke_tramp[16];

static __declspec(naked) void nuke_hook()
{
	__asm {
		mov eax, dword ptr [esp]
		mov dword ptr [g_nuke_caller], eax
		lea eax, g_nuke_tramp
		jmp eax
	}
}

extern "C" void xbox_install_nuke_hook()
{
	unsigned char* tgt = (unsigned char*)&XapiNukeDirectoryFromHandle;
	__try
	{
		MmSetAddressProtect(g_nuke_tramp, sizeof(g_nuke_tramp), X_PAGE_EXECUTE_READWRITE);
		for (int i = 0; i < 6; i++) g_nuke_tramp[i] = tgt[i]; // 6 = clean boundary
		g_nuke_tramp[6] = 0xE9;
		*(long*)(g_nuke_tramp + 7) = (long)((tgt + 6) - (g_nuke_tramp + 11));
		MmSetAddressProtect(tgt, 5, X_PAGE_EXECUTE_READWRITE);
		tgt[0] = 0xE9;
		*(long*)(tgt + 1) = (long)((unsigned char*)nuke_hook - (tgt + 5));
	}
	__except (1) {}
}

// RXDK's CRT file-open resolves a path as an embedded XBE section via
// XGetSectionHandleA, which faults on our null section table (we have no
// registered sections). Detour it to return NULL ("no such section") so the
// caller falls through to the real file. @4 = 1 arg -> ret 4.
static __declspec(naked) void xgetsection_stub()
{
	__asm { xor eax, eax
	        ret 4 }
}
extern "C" void xbox_install_xgetsection_hook()
{
	install_one((void*)&XGetSectionHandleA, (void*)xgetsection_stub);
}

// Next in RXDK's file-open path: GetDiskFreeSpaceExA faults on a null drive
// table. Return "plenty of space" so the (write) open proceeds.
static int __stdcall gdfse_hook(const char*, ULARGE_INTEGER* a, ULARGE_INTEGER* b, ULARGE_INTEGER* c)
{
	if (a) a->QuadPart = 0x40000000ULL;
	if (b) b->QuadPart = 0x40000000ULL;
	if (c) c->QuadPart = 0x40000000ULL;
	return 1;
}
extern "C" void xbox_install_gdfse_hook()
{
	install_one((void*)&GetDiskFreeSpaceExA, (void*)gdfse_hook);
}

// std::_Fiopen (C++ stream open) is called by direct address inside libcpmt, so
// inline-detour the real function to return NULL. __cdecl -> plain ret (caller
// cleans args). Fails all C++ stream opens before RXDK's resolver runs.
static __declspec(naked) void fiopen_stub()
{
	__asm { xor eax, eax
	        ret }
}
extern "C" void xbox_install_fiopen_hook()
{
	install_one((void*)&std::_Fiopen, (void*)fiopen_stub);
}

extern "C" void xbox_install_ddrive_hook()
{
	// Cut the whole chain at the top (content-signature install), plus the two
	// drive resolvers below as belt-and-suspenders.
	install_one((void*)&XInstallContentSignaturesWithFileName, (void*)contsig16_hook_impl);
	install_one((void*)&XInstallContentSignaturesEx,           (void*)contsig16_hook_impl);
	install_one((void*)&XInstallContentSignatures,             (void*)contsig12_hook_impl);
	install_one((void*)&XapipUpdateGetCurrentDDriveMapping,    (void*)ddrive_hook_impl);
	install_one((void*)&XapiMapLetterToDirectory,              (void*)mapletter_hook_impl);
}

// ---------------------------------------------------------------------------
// NT native-API file open, bypassing the Xbox drive-letter resolver.
//
// CreateFileA("F:\\...") resolves the drive letter via
// _XapipUpdateGetCurrentDDriveMapping, which faults on the emu thread (a
// per-thread cached buffer is NULL there -> RtlReAllocateHeap(NULL), fault at
// 0xFFFFFFF6). NtCreateFile with a full \Device\... object path skips that
// resolution, so file opens work from ANY thread. We map drive letters to the
// partition devices we mounted (Partition6=F:, 7=G:, 1=E:, 2=C:, CdRom0=D:).
// ---------------------------------------------------------------------------
typedef long NTSTATUS_X;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } OBJSTR_X;
typedef struct { HANDLE RootDirectory; OBJSTR_X* ObjectName; unsigned long Attributes; } OBJATTR_X;
typedef struct { union { NTSTATUS_X Status; void* Pointer; } u; unsigned long* Information; } IOSTAT_X;
typedef struct {
	LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
	LARGE_INTEGER AllocationSize, EndOfFile;
	unsigned long FileAttributes;
} FNETOPEN_X;

extern "C" NTSTATUS_X __stdcall NtCreateFile(HANDLE*, unsigned long, OBJATTR_X*, IOSTAT_X*,
	LARGE_INTEGER*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" NTSTATUS_X __stdcall NtQueryFullAttributesFile(OBJATTR_X*, FNETOPEN_X*);

#define X_FILE_OPEN            1
#define X_FILE_CREATE          2
#define X_FILE_OVERWRITE_IF    5
#define X_FILE_OPEN_IF         3
#define X_FILE_OVERWRITE       4
#define X_FILE_SYNCHRONOUS_IO  0x00000020
#define X_FILE_NON_DIRECTORY   0x00000040
#define X_OBJ_CASE_INSENSITIVE 0x00000040
#define X_SYNCHRONIZE          0x00100000
#define X_GENERIC_READ_NT      0x120089
#define X_GENERIC_WRITE_NT     0x120116

// Map "F:\a/b" -> "\Device\Harddisk0\Partition6\a\b" (slashes normalized).
// Returns false for drives we don't map (caller falls back to CreateFileA).
static bool drive_to_device(const char* path, char* out, int outsz)
{
	if (!path[0] || path[1] != ':') return false;
	const char* dev;
	switch (path[0]) {
		case 'E': case 'e': dev = "\\Device\\Harddisk0\\Partition1"; break;
		case 'C': case 'c': dev = "\\Device\\Harddisk0\\Partition2"; break;
		case 'F': case 'f': dev = "\\Device\\Harddisk0\\Partition6"; break;
		case 'G': case 'g': dev = "\\Device\\Harddisk0\\Partition7"; break;
		case 'D': case 'd': dev = "\\Device\\CdRom0"; break;
		default: return false;
	}
	int n = 0;
	for (const char* p = dev; *p && n < outsz - 1; p++) out[n++] = *p;
	for (const char* p = path + 2; *p && n < outsz - 1; p++)
		out[n++] = (*p == '/') ? '\\' : *p;
	out[n] = 0;
	return true;
}

extern "C" HANDLE xbox_nt_open(const char* path, unsigned long access, unsigned long creation)
{
	char dev[MAX_PATH];
	if (!drive_to_device(path, dev, sizeof(dev)))
	{
		char note[300]; note[0]='C'; note[1]='F'; note[2]='>';
		int i=0; for (; path[i] && i<290; i++) note[3+i]=path[i]; note[3+i]=0;
		xbox_note_path(note); // fallback -> CreateFileA (hits drive resolver)
		return CreateFileA(path, access, FILE_SHARE_READ, NULL, creation,
		                   FILE_ATTRIBUTE_NORMAL, NULL);
	}
	{
		char note[300]; note[0]='N'; note[1]='T'; note[2]='>';
		int i=0; for (; dev[i] && i<290; i++) note[3+i]=dev[i]; note[3+i]=0;
		xbox_note_path(note); // NT device-path open (bypasses resolver)
	}

	OBJSTR_X name;
	unsigned short L = 0; while (dev[L]) L++;
	name.Length = L; name.MaximumLength = (unsigned short)(L + 1); name.Buffer = dev;
	OBJATTR_X oa; oa.RootDirectory = NULL; oa.ObjectName = &name; oa.Attributes = X_OBJ_CASE_INSENSITIVE;

	unsigned long desired = X_SYNCHRONIZE;
	if (access & GENERIC_READ)  desired |= X_GENERIC_READ_NT;
	if (access & GENERIC_WRITE) desired |= X_GENERIC_WRITE_NT;

	unsigned long disp;
	switch (creation) {
		case CREATE_ALWAYS:      disp = X_FILE_OVERWRITE_IF; break;
		case CREATE_NEW:         disp = X_FILE_CREATE;       break;
		case OPEN_ALWAYS:        disp = X_FILE_OPEN_IF;      break;
		case TRUNCATE_EXISTING:  disp = X_FILE_OVERWRITE;    break;
		default:                 disp = X_FILE_OPEN;         break; // OPEN_EXISTING
	}

	HANDLE h = NULL;
	IOSTAT_X iosb;
	NTSTATUS_X st = NtCreateFile(&h, desired, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ, disp, X_FILE_SYNCHRONOUS_IO | X_FILE_NON_DIRECTORY);
	if (st < 0) return INVALID_HANDLE_VALUE;
	return h;
}

extern "C" unsigned long xbox_nt_attrs(const char* path)
{
	char dev[MAX_PATH];
	if (!drive_to_device(path, dev, sizeof(dev)))
	{
		char note[300]; note[0]='C'; note[1]='F'; note[2]='?';
		int i=0; for (; path[i] && i<290; i++) note[3+i]=path[i]; note[3+i]=0;
		xbox_note_path(note);
		return GetFileAttributesA(path);
	}
	{
		char note[300]; note[0]='N'; note[1]='T'; note[2]='?';
		int i=0; for (; dev[i] && i<290; i++) note[3+i]=dev[i]; note[3+i]=0;
		xbox_note_path(note);
	}

	OBJSTR_X name;
	unsigned short L = 0; while (dev[L]) L++;
	name.Length = L; name.MaximumLength = (unsigned short)(L + 1); name.Buffer = dev;
	OBJATTR_X oa; oa.RootDirectory = NULL; oa.ObjectName = &name; oa.Attributes = X_OBJ_CASE_INSENSITIVE;

	FNETOPEN_X info;
	NTSTATUS_X st = NtQueryFullAttributesFile(&oa, &info);
	if (st < 0) return 0xFFFFFFFF;
	return info.FileAttributes;
}

#define XFD_MAX 512
static HANDLE g_fdtab[XFD_MAX];
static int    g_fdInit = 0;
static CRITICAL_SECTION g_fdLock;

static void fd_init_once(void)
{
	if (g_fdInit) return;
	InitializeCriticalSection(&g_fdLock);
	for (int i = 0; i < XFD_MAX; i++)
		g_fdtab[i] = INVALID_HANDLE_VALUE;
	g_fdInit = 1;
}

static int fd_alloc(HANDLE h)
{
	fd_init_once();
	EnterCriticalSection(&g_fdLock);
	int fd = -1;
	for (int i = 3; i < XFD_MAX; i++) // 0,1,2 reserved for std streams
	{
		if (g_fdtab[i] == INVALID_HANDLE_VALUE)
		{
			g_fdtab[i] = h;
			fd = i;
			break;
		}
	}
	LeaveCriticalSection(&g_fdLock);
	return fd;
}

static HANDLE fd_get(int fd)
{
	if (fd < 0 || fd >= XFD_MAX) return INVALID_HANDLE_VALUE;
	return g_fdtab[fd];
}

extern "C" {

int __cdecl _open(const char* name, int flags, ...)
{
	fd_init_once();

	// Dolphin builds paths with '/' (DIR_SEP="/"), but the Xbox drive resolver
	// (XapipUpdateGetCurrentDDriveMapping) requires '\'. Normalize.
	char path[MAX_PATH];
	{
		int i = 0;
		for (; name[i] && i < MAX_PATH - 1; i++)
			path[i] = (name[i] == '/') ? '\\' : name[i];
		path[i] = 0;
		name = path;
	}
	xbox_note_path(name);

	DWORD access = 0;
	if ((flags & 3) == XO_RDONLY) access = GENERIC_READ;
	else if ((flags & 3) == XO_WRONLY) access = GENERIC_WRITE;
	else access = GENERIC_READ | GENERIC_WRITE;

	DWORD creation;
	if ((flags & XO_CREAT) && (flags & XO_TRUNC)) creation = CREATE_ALWAYS;
	else if ((flags & XO_CREAT) && (flags & XO_EXCL)) creation = CREATE_NEW;
	else if (flags & XO_CREAT) creation = OPEN_ALWAYS;
	else if (flags & XO_TRUNC) creation = TRUNCATE_EXISTING;
	else creation = OPEN_EXISTING;

	HANDLE h = xbox_nt_open(name, access, creation);
	if (h == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }

	if (flags & XO_APPEND)
		SetFilePointer(h, 0, NULL, FILE_END);

	int fd = fd_alloc(h);
	if (fd < 0) { CloseHandle(h); errno = EMFILE; return -1; }
	return fd;
}

// fopen/basic_filebuf route through _sopen / _wsopen; funnel them to _open.
int __cdecl _sopen(const char* name, int flags, int /*shflag*/, ...)
{
	return _open(name, flags);
}

int __cdecl _wsopen(const wchar_t* wname, int flags, int /*shflag*/, ...)
{
	char name[MAX_PATH];
	int n = WideCharToMultiByte(CP_ACP, 0, wname, -1, name, sizeof(name), NULL, NULL);
	if (n <= 0) { errno = ENOENT; return -1; }
	return _open(name, flags);
}

int __cdecl _sopen_s(int* pfd, const char* name, int flags, int, int)
{
	int fd = _open(name, flags);
	if (pfd) *pfd = fd;
	return fd < 0 ? errno : 0;
}

int __cdecl _close(int fd)
{
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
	CloseHandle(h);
	g_fdtab[fd] = INVALID_HANDLE_VALUE;
	return 0;
}

int __cdecl _read(int fd, void* buf, unsigned int count)
{
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
	DWORD got = 0;
	if (!ReadFile(h, buf, count, &got, NULL)) return -1;
	return (int)got;
}

int __cdecl _write(int fd, const void* buf, unsigned int count)
{
	if (fd == 1 || fd == 2) // stdout/stderr -> debug channel
	{
		char tmp[512];
		unsigned n = count < sizeof(tmp) - 1 ? count : sizeof(tmp) - 1;
		memcpy(tmp, buf, n); tmp[n] = 0;
		OutputDebugStringA(tmp);
		return (int)count;
	}
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
	DWORD put = 0;
	if (!WriteFile(h, buf, count, &put, NULL)) return -1;
	return (int)put;
}

__int64 __cdecl _lseeki64(int fd, __int64 offset, int origin)
{
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
	LARGE_INTEGER li; li.QuadPart = offset;
	DWORD method = (origin == SEEK_CUR) ? FILE_CURRENT
	             : (origin == SEEK_END) ? FILE_END : FILE_BEGIN;
	li.LowPart = SetFilePointer(h, li.LowPart, &li.HighPart, method);
	if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
		return -1;
	return li.QuadPart;
}

long __cdecl _lseek(int fd, long offset, int origin)
{
	return (long)_lseeki64(fd, offset, origin);
}

intptr_t __cdecl _get_osfhandle(int fd)
{
	HANDLE h = fd_get(fd);
	return (intptr_t)(h == INVALID_HANDLE_VALUE ? (HANDLE)-1 : h);
}

int __cdecl _open_osfhandle(intptr_t osfhandle, int /*flags*/)
{
	int fd = fd_alloc((HANDLE)osfhandle);
	if (fd < 0) { errno = EMFILE; return -1; }
	return fd;
}

int __cdecl _commit(int fd)
{
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
	return FlushFileBuffers(h) ? 0 : -1;
}

int __cdecl _setmode(int /*fd*/, int /*mode*/) { return 0x8000; } // pretend binary
int __cdecl _isatty(int /*fd*/) { return 0; }
int __cdecl _fileno_dummy(void) { return 0; }

// Size query used by fstat / stream tellg. Just fill the size field.
__int64 __cdecl _filelengthi64(int fd)
{
	HANDLE h = fd_get(fd);
	if (h == INVALID_HANDLE_VALUE) return -1;
	LARGE_INTEGER sz;
	if (!GetFileSizeEx(h, &sz)) return -1;
	return sz.QuadPart;
}

} // extern "C"
