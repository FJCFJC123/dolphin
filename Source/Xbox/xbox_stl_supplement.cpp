// OG Xbox port: STL supplement — throw helpers + the few threading primitives
// the Flycast stl_shim doesn't cover but Dolphin references. Same header
// context as xbox_stl_shim.cpp so _Mtx_t/_Thrd_t/_Thrd_result match exactly.
// Threading is stubbed no-op (single-threaded: bCPUThread=false), consistent
// with xbox_stl_shim.cpp.

#include <exception>
#include <stdexcept>
#include <new>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <iomanip>

// STL container/algorithm out-of-line throw helpers (RXDK libcpmt lacks them).
namespace std {
	[[noreturn]] void __cdecl _Xout_of_range(const char* s)     { throw out_of_range(s); }
	[[noreturn]] void __cdecl _Xruntime_error(const char* s)    { throw runtime_error(s); }
	// _Xlength_error lives in xbox_stl_shim.cpp (Flycast set) — not duplicated here.
	[[noreturn]] void __cdecl _Xinvalid_argument(const char* s) { throw invalid_argument(s); }
	[[noreturn]] void __cdecl _Xbad_alloc()                     { throw bad_alloc(); }
}

extern "C" {
	void __cdecl _Thrd_yield(void) noexcept {}
	_Thrd_result __cdecl _Thrd_detach(_Thrd_t) noexcept { return _Thrd_result::_Success; }
	_Thrd_result __cdecl _Mtx_trylock(_Mtx_t) noexcept  { return _Thrd_result::_Success; }
}

// std::setw(streamsize) — declared but not defined by RXDK's libcpmt.
namespace std {
	static void __cdecl _Xbox_setw(ios_base& _Iostr, streamsize _Wide) { _Iostr.width(_Wide); }
	_Smanip<streamsize> __cdecl setw(streamsize _Wide) { return _Smanip<streamsize>(&_Xbox_setw, _Wide); }
}
