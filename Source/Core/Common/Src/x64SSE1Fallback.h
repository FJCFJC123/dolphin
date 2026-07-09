// OG Xbox port: SSE1 fallback support.
// The Coppermine Celeron (OG Xbox CPU) has SSE1/MMX but no SSE2. The JIT
// emitter (x64Emitter.cpp) rewrites SSE2 emissions when cpu_info.bSSE2 is
// false: free SSE1 opcode swaps where bit-identical, inline x87 sequences for
// double arithmetic, and calls into the C thunk below for the rare rest.
// Scratch layout: bytes 0..15 = dst operand, bytes 16..31 = src operand.

#ifndef _X64SSE1FALLBACK_H_
#define _X64SSE1FALLBACK_H_

#include "Common.h"

namespace SSE1Fallback
{

extern GC_ALIGNED16(u8 g_scratch[32]);

// Runtime profiling: how often each thunk op executes (indexed by ThunkOp).
extern u32 g_thunk_count[32];

enum ThunkOp
{
	OP_PSLLW, OP_PSRLW, OP_PSRAW,
	OP_PSLLD, OP_PSRLD, OP_PSRAD,
	OP_PSLLQ, OP_PSRLQ,
	OP_PSHUFLW,
	OP_PACKSSDW, OP_PACKSSWB, OP_PACKUSWB,
	OP_PUNPCKLBW, OP_PUNPCKLWD,
	OP_CVTDQ2PS, OP_CVTTPS2DQ,
	OP_CMPSD, OP_CMPPD,
	OP_MINSD, OP_MAXSD,
};

}  // namespace

// dst(g_scratch[0..15]) = op(dst, src(g_scratch[16..31]), imm)
extern "C" void SSE1Fallback_Thunk(u32 op, u32 imm);

#endif  // _X64SSE1FALLBACK_H_
