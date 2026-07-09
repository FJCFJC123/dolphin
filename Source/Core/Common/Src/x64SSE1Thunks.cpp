// OG Xbox port: C implementations of SSE2 operations the SSE1 JIT fallback
// layer routes through a call (see x64SSE1Fallback.h for the contract).
// Compiled with /arch:IA32 (per-file override in Common.vcxproj) so MSVC
// emits x87-only code: the JIT calls this with live PPC values in the XMM
// registers and only GPRs saved (PUSHAD), so the thunk must never touch
// XMM. Manual copy loops instead of memcpy for the same reason (the CRT
// memcpy is SSE2-optimized).

#include <math.h>
#include <string.h>

#include "x64SSE1Fallback.h"

namespace SSE1Fallback
{
GC_ALIGNED16(u8 g_scratch[32]);
u32 g_thunk_count[32];
}

using SSE1Fallback::g_scratch;

static inline s16 SatS16(s32 v) { return v < -32768 ? -32768 : (v > 32767 ? 32767 : (s16)v); }
static inline s8  SatS8(s32 v)  { return v < -128 ? -128 : (v > 127 ? 127 : (s8)v); }
static inline u8  SatU8(s32 v)  { return v < 0 ? 0 : (v > 255 ? 255 : (u8)v); }

static inline u64 CmpDoubleMask(double a, double b, u32 predicate)
{
	bool unord = (a != a) || (b != b);
	bool r = false;
	switch (predicate & 7)
	{
	case 0: r = !unord && (a == b); break; // EQ
	case 1: r = !unord && (a < b);  break; // LT
	case 2: r = !unord && (a <= b); break; // LE
	case 3: r = unord;              break; // UNORD
	case 4: r = unord || (a != b);  break; // NEQ
	case 5: r = unord || !(a < b);  break; // NLT
	case 6: r = unord || !(a <= b); break; // NLE
	case 7: r = !unord;             break; // ORD
	}
	return r ? 0xFFFFFFFFFFFFFFFFULL : 0;
}

extern "C" void SSE1Fallback_Thunk(u32 op, u32 imm)
{
	if (op < 32)
		SSE1Fallback::g_thunk_count[op]++;
	u8*  d8  = (u8*)g_scratch;         u8*  s8p = (u8*)(g_scratch + 16);
	u16* d16 = (u16*)g_scratch;        u16* sw16 = (u16*)(g_scratch + 16);
	u32* d32 = (u32*)g_scratch;        u32* sw32 = (u32*)(g_scratch + 16);
	u64* d64 = (u64*)g_scratch;        u64* sw64 = (u64*)(g_scratch + 16);
	float*  df = (float*)g_scratch;
	double* dd = (double*)g_scratch;   double* sd = (double*)(g_scratch + 16);
	int i;
	u8 tmp[16];

	switch (op)
	{
	case SSE1Fallback::OP_PSLLW:
		for (i = 0; i < 8; i++) d16[i] = (imm > 15) ? 0 : (u16)(d16[i] << imm);
		break;
	case SSE1Fallback::OP_PSRLW:
		for (i = 0; i < 8; i++) d16[i] = (imm > 15) ? 0 : (u16)(d16[i] >> imm);
		break;
	case SSE1Fallback::OP_PSRAW:
		for (i = 0; i < 8; i++) d16[i] = (u16)((s16)d16[i] >> (imm > 15 ? 15 : imm));
		break;
	case SSE1Fallback::OP_PSLLD:
		for (i = 0; i < 4; i++) d32[i] = (imm > 31) ? 0 : (d32[i] << imm);
		break;
	case SSE1Fallback::OP_PSRLD:
		for (i = 0; i < 4; i++) d32[i] = (imm > 31) ? 0 : (d32[i] >> imm);
		break;
	case SSE1Fallback::OP_PSRAD:
		for (i = 0; i < 4; i++) d32[i] = (u32)((s32)d32[i] >> (imm > 31 ? 31 : imm));
		break;
	case SSE1Fallback::OP_PSLLQ:
		for (i = 0; i < 2; i++) d64[i] = (imm > 63) ? 0 : (d64[i] << imm);
		break;
	case SSE1Fallback::OP_PSRLQ:
		for (i = 0; i < 2; i++) d64[i] = (imm > 63) ? 0 : (d64[i] >> imm);
		break;

	case SSE1Fallback::OP_PSHUFLW:
		// result = shuffle of src low words; src high qword copied through
		for (i = 0; i < 4; i++) ((u16*)tmp)[i] = sw16[(imm >> (2 * i)) & 3];
		for (i = 0; i < 8; i++) tmp[8 + i] = s8p[8 + i];
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;

	case SSE1Fallback::OP_PACKSSDW:
		for (i = 0; i < 4; i++) ((s16*)tmp)[i]     = SatS16((s32)d32[i]);
		for (i = 0; i < 4; i++) ((s16*)tmp)[4 + i] = SatS16((s32)sw32[i]);
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;
	case SSE1Fallback::OP_PACKSSWB:
		for (i = 0; i < 8; i++) ((s8*)tmp)[i]     = SatS8((s16)d16[i]);
		for (i = 0; i < 8; i++) ((s8*)tmp)[8 + i] = SatS8((s16)sw16[i]);
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;
	case SSE1Fallback::OP_PACKUSWB:
		for (i = 0; i < 8; i++) tmp[i]     = SatU8((s16)d16[i]);
		for (i = 0; i < 8; i++) tmp[8 + i] = SatU8((s16)sw16[i]);
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;

	case SSE1Fallback::OP_PUNPCKLBW:
		for (i = 0; i < 8; i++) { tmp[2 * i] = d8[i]; tmp[2 * i + 1] = s8p[i]; }
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;
	case SSE1Fallback::OP_PUNPCKLWD:
		for (i = 0; i < 4; i++)
		{
			((u16*)tmp)[2 * i]     = d16[i];
			((u16*)tmp)[2 * i + 1] = sw16[i];
		}
		for (i = 0; i < 16; i++) d8[i] = tmp[i];
		break;

	case SSE1Fallback::OP_CVTDQ2PS:
		for (i = 0; i < 4; i++) df[i] = (float)(s32)sw32[i];
		break;
	case SSE1Fallback::OP_CVTTPS2DQ:
		for (i = 0; i < 4; i++)
		{
			float f = ((float*)(g_scratch + 16))[i];
			if (f != f || f >= 2147483648.0f || f < -2147483648.0f)
				d32[i] = 0x80000000;
			else
				d32[i] = (u32)(s32)f; // C cast truncates, matching CVTT
		}
		break;

	case SSE1Fallback::OP_CMPSD:
		d64[0] = CmpDoubleMask(dd[0], sd[0], imm);
		break;
	case SSE1Fallback::OP_CMPPD:
		{
			u64 lo = CmpDoubleMask(dd[0], sd[0], imm);
			u64 hi = CmpDoubleMask(dd[1], sd[1], imm);
			d64[0] = lo;
			d64[1] = hi;
		}
		break;

	case SSE1Fallback::OP_MINSD:
		// SSE semantics: if either is NaN (or equal), result = src
		dd[0] = (dd[0] < sd[0]) ? dd[0] : sd[0];
		break;
	case SSE1Fallback::OP_MAXSD:
		dd[0] = (dd[0] > sd[0]) ? dd[0] : sd[0];
		break;
	}
}

