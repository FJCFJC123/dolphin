// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "x64Emitter.h"
#include "x64ABI.h"
#include "CPUDetect.h"
#include "x64SSE1Fallback.h"

namespace Gen
{

// TODO(ector): Add EAX special casing, for ever so slightly smaller code.
struct NormalOpDef
{
	u8 toRm8, toRm32, fromRm8, fromRm32, imm8, imm32, simm8, ext;
};

static const NormalOpDef nops[11] = 
{
	{0x00, 0x01, 0x02, 0x03, 0x80, 0x81, 0x83, 0}, //ADD
	{0x10, 0x11, 0x12, 0x13, 0x80, 0x81, 0x83, 2}, //ADC

	{0x28, 0x29, 0x2A, 0x2B, 0x80, 0x81, 0x83, 5}, //SUB
	{0x18, 0x19, 0x1A, 0x1B, 0x80, 0x81, 0x83, 3}, //SBB

	{0x20, 0x21, 0x22, 0x23, 0x80, 0x81, 0x83, 4}, //AND
	{0x08, 0x09, 0x0A, 0x0B, 0x80, 0x81, 0x83, 1}, //OR

	{0x30, 0x31, 0x32, 0x33, 0x80, 0x81, 0x83, 6}, //XOR
	{0x88, 0x89, 0x8A, 0x8B, 0xC6, 0xC7, 0xCC, 0}, //MOV

	{0x84, 0x85, 0x84, 0x85, 0xF6, 0xF7, 0xCC, 0}, //TEST (to == from)
	{0x38, 0x39, 0x3A, 0x3B, 0x80, 0x81, 0x83, 7}, //CMP

	{0x86, 0x87, 0x86, 0x87, 0xCC, 0xCC, 0xCC, 7}, //XCHG
};

enum NormalSSEOps
{
	sseCMP         = 0xC2,
	sseADD         = 0x58, //ADD
	sseSUB         = 0x5C, //SUB
	sseAND         = 0x54, //AND
	sseANDN        = 0x55, //ANDN
	sseOR          = 0x56,
	sseXOR         = 0x57,
	sseMUL         = 0x59, //MUL
	sseDIV         = 0x5E, //DIV
	sseMIN         = 0x5D, //MIN
	sseMAX         = 0x5F, //MAX
	sseCOMIS       = 0x2F, //COMIS
	sseUCOMIS      = 0x2E, //UCOMIS
	sseSQRT        = 0x51, //SQRT
	sseRSQRT       = 0x52, //RSQRT (NO DOUBLE PRECISION!!!)
	sseMOVAPfromRM = 0x28, //MOVAP from RM
	sseMOVAPtoRM   = 0x29, //MOVAP to RM
	sseMOVUPfromRM = 0x10, //MOVUP from RM
	sseMOVUPtoRM   = 0x11, //MOVUP to RM
	sseMASKMOVDQU  = 0xF7,
	sseLDDQU       = 0xF0,
	sseSHUF        = 0xC6,
	sseMOVNTDQ     = 0xE7,
	sseMOVNTP      = 0x2B,
};


void XEmitter::SetCodePtr(u8 *ptr)
{
	code = ptr;
}

const u8 *XEmitter::GetCodePtr() const
{
	return code;
}

u8 *XEmitter::GetWritableCodePtr()
{
	return code;
}

void XEmitter::ReserveCodeSpace(int bytes)
{
	for (int i = 0; i < bytes; i++)
		*code++ = 0xCC;
}

const u8 *XEmitter::AlignCode4()
{
	int c = int((u64)code & 3);
	if (c)
		ReserveCodeSpace(4-c);
	return code;
}

const u8 *XEmitter::AlignCode16()
{
	int c = int((u64)code & 15);
	if (c)
		ReserveCodeSpace(16-c);
	return code;
}

const u8 *XEmitter::AlignCodePage()
{
	int c = int((u64)code & 4095);
	if (c)
		ReserveCodeSpace(4096-c);
	return code;
}

void XEmitter::WriteModRM(int mod, int rm, int reg)
{
	Write8((u8)((mod << 6) | ((rm & 7) << 3) | (reg & 7)));
}

void XEmitter::WriteSIB(int scale, int index, int base)
{
	Write8((u8)((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

void OpArg::WriteRex(XEmitter *emit, int opBits, int bits, int customOp) const
{
	if (customOp == -1)       customOp = operandReg;
#ifdef _M_X64
	u8 op = 0x40;
	if (opBits == 64)         op |= 8;
	if (customOp & 8)         op |= 4;
	if (indexReg & 8)         op |= 2;
	if (offsetOrBaseReg & 8)  op |= 1; //TODO investigate if this is dangerous
	if (op != 0x40 ||
		(bits == 8 && (offsetOrBaseReg & 0x10c) == 4) ||
		(opBits == 8 && (customOp & 0x10c) == 4)) {
		emit->Write8(op);
		_dbg_assert_(DYNA_REC, (offsetOrBaseReg & 0x100) == 0 || bits != 8);
		_dbg_assert_(DYNA_REC, (customOp & 0x100) == 0 || opBits != 8);
	} else {
		_dbg_assert_(DYNA_REC, (offsetOrBaseReg & 0x10c) == 0 ||
				(offsetOrBaseReg & 0x10c) == 0x104 ||
				bits != 8);
		_dbg_assert_(DYNA_REC, (customOp & 0x10c) == 0 ||
				(customOp & 0x10c) == 0x104 ||
				opBits != 8);
	}

#else
	_dbg_assert_(DYNA_REC, opBits != 64);
	_dbg_assert_(DYNA_REC, (customOp & 8) == 0 || customOp == -1);
	_dbg_assert_(DYNA_REC, (indexReg & 8) == 0);
	_dbg_assert_(DYNA_REC, (offsetOrBaseReg & 8) == 0);
	_dbg_assert_(DYNA_REC, opBits != 8 || (customOp & 0x10c) != 4 || customOp == -1);
	_dbg_assert_(DYNA_REC, bits != 8 || (offsetOrBaseReg & 0x10c) != 4);
#endif
}

void OpArg::WriteRest(XEmitter *emit, int extraBytes, X64Reg _operandReg,
	bool warn_64bit_offset) const
{
	if (_operandReg == 0xff)
		_operandReg = (X64Reg)this->operandReg;
	int mod = 0;
	int ireg = indexReg;
	bool SIB = false;
	int _offsetOrBaseReg = this->offsetOrBaseReg;

	if (scale == SCALE_RIP) //Also, on 32-bit, just an immediate address
	{
		// Oh, RIP addressing.
		_offsetOrBaseReg = 5;
		emit->WriteModRM(0, _operandReg&7, 5);
		//TODO : add some checks
#ifdef _M_X64
		u64 ripAddr = (u64)emit->GetCodePtr() + 4 + extraBytes;
		s64 distance = (s64)offset - (s64)ripAddr;
		_assert_msg_(DYNA_REC, (distance < 0x80000000LL
					&& distance >=  -0x80000000LL) ||
			     !warn_64bit_offset,
			     "WriteRest: op out of range (0x%llx uses 0x%llx)",
			     ripAddr, offset);
		s32 offs = (s32)distance;
		emit->Write32((u32)offs);
#else
		emit->Write32((u32)offset);
#endif
		return;
	}

	if (scale == 0)
	{
		// Oh, no memory, Just a reg.
		mod = 3; //11
	}
	else if (scale >= 1)
	{
		//Ah good, no scaling.
		if (scale == SCALE_ATREG && !((_offsetOrBaseReg & 7) == 4 || (_offsetOrBaseReg & 7) == 5))
		{
			//Okay, we're good. No SIB necessary.
			int ioff = (int)offset;
			if (ioff == 0)
			{
				mod = 0;
			}
			else if (ioff<-128 || ioff>127)
			{
				mod = 2; //32-bit displacement
			}
			else
			{
				mod = 1; //8-bit displacement
			}
		}
		else if (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8)
		{
			SIB = true;
			mod = 0;
			_offsetOrBaseReg = 5;
		}
		else //if (scale != SCALE_ATREG)
		{
			if ((_offsetOrBaseReg & 7) == 4) //this would occupy the SIB encoding :(
			{
				//So we have to fake it with SIB encoding :(
				SIB = true;
			}

			if (scale >= SCALE_1 && scale < SCALE_ATREG)
			{
				SIB = true;
			}

			if (scale == SCALE_ATREG && ((_offsetOrBaseReg & 7) == 4)) 
			{
				SIB = true;
				ireg = _offsetOrBaseReg;
			}

			//Okay, we're fine. Just disp encoding.
			//We need displacement. Which size?
			int ioff = (int)(s64)offset;
			if (ioff < -128 || ioff > 127)
			{
				mod = 2; //32-bit displacement
			}
			else
			{
				mod = 1; //8-bit displacement
			}
		}
	}

	// Okay. Time to do the actual writing
	// ModRM byte:
	int oreg = _offsetOrBaseReg;
	if (SIB)
		oreg = 4;
	
	// TODO(ector): WTF is this if about? I don't remember writing it :-)
	//if (RIP)
	//    oreg = 5;

	emit->WriteModRM(mod, _operandReg&7, oreg&7);

	if (SIB)
	{
		//SIB byte
		int ss;
		switch (scale)
		{
		case SCALE_NONE: _offsetOrBaseReg = 4; ss = 0; break; //RSP 
		case SCALE_1: ss = 0; break;
		case SCALE_2: ss = 1; break;
		case SCALE_4: ss = 2; break;
		case SCALE_8: ss = 3; break;
		case SCALE_NOBASE_2: ss = 1; break;
		case SCALE_NOBASE_4: ss = 2; break;
		case SCALE_NOBASE_8: ss = 3; break;
		case SCALE_ATREG: ss = 0; break;
		default: _assert_msg_(DYNA_REC, 0, "Invalid scale for SIB byte"); ss = 0; break;
		}
		emit->Write8((u8)((ss << 6) | ((ireg&7)<<3) | (_offsetOrBaseReg&7)));
	}

	if (mod == 1) //8-bit disp
	{
		emit->Write8((u8)(s8)(s32)offset);
	}
	else if (mod == 2 || (scale >= SCALE_NOBASE_2 && scale <= SCALE_NOBASE_8)) //32-bit disp
	{
		emit->Write32((u32)offset);
	}
}


// W = operand extended width (1 if 64-bit)
// R = register# upper bit
// X = scale amnt upper bit
// B = base register# upper bit
void XEmitter::Rex(int w, int r, int x, int b)
{
	w = w ? 1 : 0;		
	r = r ? 1 : 0;
	x = x ? 1 : 0;
	b = b ? 1 : 0;
	u8 rx = (u8)(0x40 | (w << 3) | (r << 2) | (x << 1) | (b));
	if (rx != 0x40)
		Write8(rx);
}

void XEmitter::JMP(const u8 *addr, bool force5Bytes)
{
	u64 fn = (u64)addr;
	if (!force5Bytes)
	{
		s64 distance = (s64)(fn - ((u64)code + 2));
		_assert_msg_(DYNA_REC, distance >= -0x80 && distance < 0x80,
			     "Jump target too far away, needs force5Bytes = true");
		//8 bits will do
		Write8(0xEB);
		Write8((u8)(s8)distance);
	}
	else
	{
		s64 distance = (s64)(fn - ((u64)code + 5));

		_assert_msg_(DYNA_REC, distance >= -0x80000000LL
			     && distance < 0x80000000LL,
			     "Jump target too far away, needs indirect register");
		Write8(0xE9);
		Write32((u32)(s32)distance);
	}
}

void XEmitter::JMPptr(const OpArg &arg2)
{
	OpArg arg = arg2;
	if (arg.IsImm()) _assert_msg_(DYNA_REC, 0, "JMPptr - Imm argument");
	arg.operandReg = 4;
	arg.WriteRex(this, 0, 0);
	Write8(0xFF);
	arg.WriteRest(this);
}

//Can be used to trap other processors, before overwriting their code
// not used in dolphin
void XEmitter::JMPself()
{
	Write8(0xEB);
	Write8(0xFE);
}

void XEmitter::CALLptr(OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(DYNA_REC, 0, "CALLptr - Imm argument");
	arg.operandReg = 2;
	arg.WriteRex(this, 0, 0);
	Write8(0xFF);
	arg.WriteRest(this);
}

void XEmitter::CALL(const void *fnptr)
{
	u64 distance = u64(fnptr) - (u64(code) + 5);
	_assert_msg_(DYNA_REC, distance < 0x0000000080000000ULL
		     || distance >=  0xFFFFFFFF80000000ULL,
		     "CALL out of range (%p calls %p)", code, fnptr);
	Write8(0xE8);
	Write32(u32(distance));
}

FixupBranch XEmitter::J(bool force5bytes)
{
	FixupBranch branch;
	branch.type = force5bytes ? 1 : 0;
	branch.ptr = code + (force5bytes ? 5 : 2);
	if (!force5bytes)
	{
		//8 bits will do
		Write8(0xEB);
		Write8(0);
	}
	else
	{
		Write8(0xE9);
		Write32(0);
	}
	return branch;
}

FixupBranch XEmitter::J_CC(CCFlags conditionCode, bool force5bytes)
{
	FixupBranch branch;
	branch.type = force5bytes ? 1 : 0;
	branch.ptr = code + (force5bytes ? 6 : 2);
	if (!force5bytes)
	{
		//8 bits will do
		Write8(0x70 + conditionCode);
		Write8(0);
	}
	else
	{
		Write8(0x0F);
		Write8(0x80 + conditionCode);
		Write32(0);
	}
	return branch;
}

void XEmitter::J_CC(CCFlags conditionCode, const u8 * addr, bool force5Bytes)
{
	u64 fn = (u64)addr;
	if (!force5Bytes)
	{
		s64 distance = (s64)(fn - ((u64)code + 2));
		_assert_msg_(DYNA_REC, distance >= -0x80 && distance < 0x80, "Jump target too far away, needs force5Bytes = true");
		//8 bits will do
		Write8(0x70 + conditionCode);
		Write8((u8)(s8)distance);
	}
	else
	{
		s64 distance = (s64)(fn - ((u64)code + 6));
		_assert_msg_(DYNA_REC, distance >= -0x80000000LL
			     && distance < 0x80000000LL,
			     "Jump target too far away, needs indirect register");
		Write8(0x0F);
		Write8(0x80 + conditionCode);
		Write32((u32)(s32)distance);
	}
}

void XEmitter::SetJumpTarget(const FixupBranch &branch)
{
	if (branch.type == 0)
	{
		s64 distance = (s64)(code - branch.ptr);
		_assert_msg_(DYNA_REC, distance >= -0x80 && distance < 0x80, "Jump target too far away, needs force5Bytes = true");
		branch.ptr[-1] = (u8)(s8)distance;
	}
	else if (branch.type == 1)
	{
		s64 distance = (s64)(code - branch.ptr);
		_assert_msg_(DYNA_REC, distance >= -0x80000000LL && distance < 0x80000000LL, "Jump target too far away, needs indirect register");
		((s32*)branch.ptr)[-1] = (s32)distance;
	}
}

// INC/DEC considered harmful on newer CPUs due to partial flag set.
// Use ADD, SUB instead.

/*
void XEmitter::INC(int bits, OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(DYNA_REC, 0, "INC - Imm argument");
	arg.operandReg = 0;
	if (bits == 16) {Write8(0x66);}
	arg.WriteRex(this, bits, bits);
	Write8(bits == 8 ? 0xFE : 0xFF);
	arg.WriteRest(this);
}
void XEmitter::DEC(int bits, OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(DYNA_REC, 0, "DEC - Imm argument");
	arg.operandReg = 1;
	if (bits == 16) {Write8(0x66);}
	arg.WriteRex(this, bits, bits);
	Write8(bits == 8 ? 0xFE : 0xFF);
	arg.WriteRest(this);
}
*/

//Single byte opcodes
//There is no PUSHAD/POPAD in 64-bit mode.
void XEmitter::INT3() {Write8(0xCC);}
void XEmitter::RET()  {Write8(0xC3);}
void XEmitter::RET_FAST()  {Write8(0xF3); Write8(0xC3);} //two-byte return (rep ret) - recommended by AMD optimization manual for the case of jumping to a ret

void XEmitter::NOP(int count)
{
	// TODO: look up the fastest nop sleds for various sizes
	int i;
	switch (count) {
	case 1:
		Write8(0x90);
		break;
	case 2:
		Write8(0x66);
		Write8(0x90);
		break;
	default:
		for (i = 0; i < count; i++) {
			Write8(0x90);
		}
		break;
	}
} 

void XEmitter::PAUSE() {Write8(0xF3); NOP();} //use in tight spinloops for energy saving on some cpu
void XEmitter::CLC()  {Write8(0xF8);} //clear carry
void XEmitter::CMC()  {Write8(0xF5);} //flip carry
void XEmitter::STC()  {Write8(0xF9);} //set carry

//TODO: xchg ah, al ???
void XEmitter::XCHG_AHAL()
{
	Write8(0x86);
	Write8(0xe0);
	// alt. 86 c4
}

//These two can not be executed on early Intel 64-bit CPU:s, only on AMD!
void XEmitter::LAHF() {Write8(0x9F);}
void XEmitter::SAHF() {Write8(0x9E);}

void XEmitter::PUSHF() {Write8(0x9C);}
void XEmitter::POPF()  {Write8(0x9D);}

void XEmitter::LFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xE8);}
void XEmitter::MFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xF0);}
void XEmitter::SFENCE() {Write8(0x0F); Write8(0xAE); Write8(0xF8);}

void XEmitter::WriteSimple1Byte(int bits, u8 byte, X64Reg reg)
{
	if (bits == 16) {Write8(0x66);}
	Rex(bits == 64, 0, 0, (int)reg >> 3);
	Write8(byte + ((int)reg & 7));
}

void XEmitter::WriteSimple2Byte(int bits, u8 byte1, u8 byte2, X64Reg reg)
{
	if (bits == 16) {Write8(0x66);}
	Rex(bits==64, 0, 0, (int)reg >> 3);
	Write8(byte1);
	Write8(byte2 + ((int)reg & 7));
}

void XEmitter::CWD(int bits)
{
	if (bits == 16) {Write8(0x66);}
	Rex(bits == 64, 0, 0, 0);
	Write8(0x99);
}

void XEmitter::CBW(int bits)
{
	if (bits == 8) {Write8(0x66);}
	Rex(bits == 32, 0, 0, 0);
	Write8(0x98);
}

//Simple opcodes


//push/pop do not need wide to be 64-bit
void XEmitter::PUSH(X64Reg reg) {WriteSimple1Byte(32, 0x50, reg);}
void XEmitter::POP(X64Reg reg)  {WriteSimple1Byte(32, 0x58, reg);}

void XEmitter::PUSH(int bits, const OpArg &reg) 
{ 
	if (reg.IsSimpleReg())
		PUSH(reg.GetSimpleReg());
	else if (reg.IsImm())
	{
		switch (reg.GetImmBits())
		{
		case 8:
			Write8(0x6A);
			Write8((u8)(s8)reg.offset);
			break;
		case 16:
			Write8(0x66);
			Write8(0x68);
			Write16((u16)(s16)(s32)reg.offset);
			break;
		case 32:
			Write8(0x68);
			Write32((u32)reg.offset);
			break;
		default:
			_assert_msg_(DYNA_REC, 0, "PUSH - Bad imm bits");
			break;
		}
	}
	else
	{
		if (bits == 16)
			Write8(0x66);
		reg.WriteRex(this, bits, bits);
		Write8(0xFF);
		reg.WriteRest(this, 0, (X64Reg)6);
	}
}

void XEmitter::POP(int /*bits*/, const OpArg &reg)
{ 
	if (reg.IsSimpleReg())
		POP(reg.GetSimpleReg());
	else
		INT3();
}

void XEmitter::BSWAP(int bits, X64Reg reg)
{
	if (bits >= 32)
	{
		WriteSimple2Byte(bits, 0x0F, 0xC8, reg);
	}
	else if (bits == 16)
	{
		ROL(16, R(reg), Imm8(8));
	}
	else if (bits == 8)
	{
		// Do nothing - can't bswap a single byte...
	}
	else
	{
		_assert_msg_(DYNA_REC, 0, "BSWAP - Wrong number of bits");
	}
}

// Undefined opcode - reserved
// If we ever need a way to always cause a non-breakpoint hard exception...
void XEmitter::UD2()
{
	Write8(0x0F);
	Write8(0x0B);
}

void XEmitter::PREFETCH(PrefetchLevel level, OpArg arg)
{
	if (arg.IsImm()) _assert_msg_(DYNA_REC, 0, "PREFETCH - Imm argument");;
	arg.operandReg = (u8)level;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(0x18);
	arg.WriteRest(this);
}

void XEmitter::SETcc(CCFlags flag, OpArg dest)
{
	if (dest.IsImm()) _assert_msg_(DYNA_REC, 0, "SETcc - Imm argument");
	dest.operandReg = 0;
	dest.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(0x90 + (u8)flag);
	dest.WriteRest(this);
}

void XEmitter::CMOVcc(int bits, X64Reg dest, OpArg src, CCFlags flag)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "CMOVcc - Imm argument");
	src.operandReg = dest;
	src.WriteRex(this, bits, bits);
	Write8(0x0F);
	Write8(0x40 + (u8)flag);
	src.WriteRest(this);
}

void XEmitter::WriteMulDivType(int bits, OpArg src, int ext)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "WriteMulDivType - Imm argument");
	src.operandReg = ext;
	if (bits == 16) Write8(0x66);
	src.WriteRex(this, bits, bits);
	if (bits == 8)
	{
		Write8(0xF6);
	}
	else
	{
		Write8(0xF7);
	}
	src.WriteRest(this);
}

void XEmitter::MUL(int bits, OpArg src)  {WriteMulDivType(bits, src, 4);}
void XEmitter::DIV(int bits, OpArg src)  {WriteMulDivType(bits, src, 6);}
void XEmitter::IMUL(int bits, OpArg src) {WriteMulDivType(bits, src, 5);}
void XEmitter::IDIV(int bits, OpArg src) {WriteMulDivType(bits, src, 7);}
void XEmitter::NEG(int bits, OpArg src)  {WriteMulDivType(bits, src, 3);}
void XEmitter::NOT(int bits, OpArg src)  {WriteMulDivType(bits, src, 2);}

void XEmitter::WriteBitSearchType(int bits, X64Reg dest, OpArg src, u8 byte2)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "WriteBitSearchType - Imm argument");
	src.operandReg = (u8)dest;
	if (bits == 16) Write8(0x66);
	src.WriteRex(this, bits, bits);
	Write8(0x0F);
	Write8(byte2);
	src.WriteRest(this);
}

void XEmitter::MOVNTI(int bits, OpArg dest, X64Reg src)
{
	if (bits <= 16) _assert_msg_(DYNA_REC, 0, "MOVNTI - bits<=16");
	WriteBitSearchType(bits, src, dest, 0xC3);
}

void XEmitter::BSF(int bits, X64Reg dest, OpArg src) {WriteBitSearchType(bits,dest,src,0xBC);} //bottom bit to top bit
void XEmitter::BSR(int bits, X64Reg dest, OpArg src) {WriteBitSearchType(bits,dest,src,0xBD);} //top bit to bottom bit

void XEmitter::MOVSX(int dbits, int sbits, X64Reg dest, OpArg src)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "MOVSX - Imm argument");
	if (dbits == sbits) {
		MOV(dbits, R(dest), src);
		return;
	}
	src.operandReg = (u8)dest;
	if (dbits == 16) Write8(0x66);
	src.WriteRex(this, dbits, sbits);
	if (sbits == 8)
	{
		Write8(0x0F);
		Write8(0xBE);
	}
	else if (sbits == 16)
	{
		Write8(0x0F);
		Write8(0xBF);
	}
	else if (sbits == 32 && dbits == 64)
	{
		Write8(0x63);
	}
	else
	{
		Crash();
	}
	src.WriteRest(this);
}

void XEmitter::MOVZX(int dbits, int sbits, X64Reg dest, OpArg src)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "MOVZX - Imm argument");
	if (dbits == sbits) {
		MOV(dbits, R(dest), src);
		return;
	}
	src.operandReg = (u8)dest;
	if (dbits == 16) Write8(0x66);
	//the 32bit result is automatically zero extended to 64bit
	src.WriteRex(this, dbits == 64 ? 32 : dbits, sbits);
	if (sbits == 8)
	{
		Write8(0x0F);
		Write8(0xB6);
	}
	else if (sbits == 16)
	{
		Write8(0x0F);
		Write8(0xB7);
	}
	else if (sbits == 32 && dbits == 64)
	{
		Write8(0x8B);
	}
	else
	{
		Crash();
	}
	src.WriteRest(this);
}


void XEmitter::LEA(int bits, X64Reg dest, OpArg src)
{
	if (src.IsImm()) _assert_msg_(DYNA_REC, 0, "LEA - Imm argument");
	src.operandReg = (u8)dest;
	if (bits == 16) Write8(0x66); //TODO: performance warning
	src.WriteRex(this, bits, bits);
	Write8(0x8D);
	src.WriteRest(this, 0, (X64Reg)0xFF, bits == 64);
}

//shift can be either imm8 or cl
void XEmitter::WriteShift(int bits, OpArg dest, OpArg &shift, int ext)
{
	bool writeImm = false;
	if (dest.IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "WriteShift - can't shift imms");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(DYNA_REC, 0, "WriteShift - illegal argument"); 
	}
	dest.operandReg = ext;
	if (bits == 16) Write8(0x66);
	dest.WriteRex(this, bits, bits, 0);
	if (shift.GetImmBits() == 8)
	{
		//ok an imm
		u8 imm = (u8)shift.offset;
		if (imm == 1)
		{
			Write8(bits == 8 ? 0xD0 : 0xD1);
		}
		else
		{
			writeImm = true;
			Write8(bits == 8 ? 0xC0 : 0xC1);
		}
	}
	else
	{
		Write8(bits == 8 ? 0xD2 : 0xD3);
	}
	dest.WriteRest(this, writeImm ? 1 : 0);
	if (writeImm)
		Write8((u8)shift.offset);
}

// large rotates and shift are slower on intel than amd
// intel likes to rotate by 1, and the op is smaller too
void XEmitter::ROL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 0);}
void XEmitter::ROR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 1);}
void XEmitter::RCL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 2);}
void XEmitter::RCR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 3);}
void XEmitter::SHL(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 4);}
void XEmitter::SHR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 5);}
void XEmitter::SAR(int bits, OpArg dest, OpArg shift) {WriteShift(bits, dest, shift, 7);}

// index can be either imm8 or register, don't use memory destination because it's slow
void XEmitter::WriteBitTest(int bits, OpArg &dest, OpArg &index, int ext)
{
	if (dest.IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "WriteBitTest - can't test imms");
	}
	if ((index.IsImm() && index.GetImmBits() != 8))
	{
		_assert_msg_(DYNA_REC, 0, "WriteBitTest - illegal argument"); 
	}
	if (bits == 16) Write8(0x66);
	if (index.IsImm())
	{
		dest.WriteRex(this, bits, bits);
		Write8(0x0F); Write8(0xBA);
		dest.WriteRest(this, 1, (X64Reg)ext);
		Write8((u8)index.offset);
	}
	else
	{
		X64Reg operand = index.GetSimpleReg();
		dest.WriteRex(this, bits, bits, operand);
		Write8(0x0F); Write8(0x83 + 8*ext);
		dest.WriteRest(this, 1, operand);
	}
}

void XEmitter::BT(int bits, OpArg dest, OpArg index)  {WriteBitTest(bits, dest, index, 4);}
void XEmitter::BTS(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 5);}
void XEmitter::BTR(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 6);}
void XEmitter::BTC(int bits, OpArg dest, OpArg index) {WriteBitTest(bits, dest, index, 7);}

//shift can be either imm8 or cl
void XEmitter::SHRD(int bits, OpArg dest, OpArg src, OpArg shift)
{
	if (dest.IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "SHRD - can't use imms as destination");
	}
	if (!src.IsSimpleReg())
	{
		_assert_msg_(DYNA_REC, 0, "SHRD - must use simple register as source");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(DYNA_REC, 0, "SHRD - illegal shift"); 
	}
	if (bits == 16) Write8(0x66);
	X64Reg operand = src.GetSimpleReg();
	dest.WriteRex(this, bits, bits, operand);
	if (shift.GetImmBits() == 8)
	{
		Write8(0x0F); Write8(0xAC);
		dest.WriteRest(this, 1, operand);
		Write8((u8)shift.offset);
	}
	else
	{
		Write8(0x0F); Write8(0xAD);
		dest.WriteRest(this, 0, operand);
	}
}

void XEmitter::SHLD(int bits, OpArg dest, OpArg src, OpArg shift)
{
	if (dest.IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "SHLD - can't use imms as destination");
	}
	if (!src.IsSimpleReg())
	{
		_assert_msg_(DYNA_REC, 0, "SHLD - must use simple register as source");
	}
	if ((shift.IsSimpleReg() && shift.GetSimpleReg() != ECX) || (shift.IsImm() && shift.GetImmBits() != 8))
	{
		_assert_msg_(DYNA_REC, 0, "SHLD - illegal shift"); 
	}
	if (bits == 16) Write8(0x66);
	X64Reg operand = src.GetSimpleReg();
	dest.WriteRex(this, bits, bits, operand);
	if (shift.GetImmBits() == 8)
	{
		Write8(0x0F); Write8(0xA4);
		dest.WriteRest(this, 1, operand);
		Write8((u8)shift.offset);
	}
	else
	{
		Write8(0x0F); Write8(0xA5);
		dest.WriteRest(this, 0, operand);
	}
}

void OpArg::WriteSingleByteOp(XEmitter *emit, u8 op, X64Reg _operandReg, int bits)
{
	if (bits == 16)
		emit->Write8(0x66);

	this->operandReg = (u8)_operandReg;
	WriteRex(emit, bits, bits);
	emit->Write8(op);
	WriteRest(emit);
}

//operand can either be immediate or register
void OpArg::WriteNormalOp(XEmitter *emit, bool toRM, NormalOp op, const OpArg &operand, int bits) const
{
	X64Reg _operandReg = (X64Reg)this->operandReg;
	if (IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Imm argument, wrong order");
	}

	if (bits == 16)
		emit->Write8(0x66);

	int immToWrite = 0;

	if (operand.IsImm())
	{
		_operandReg = (X64Reg)0;
		WriteRex(emit, bits, bits);

		if (!toRM)
		{
			_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Writing to Imm (!toRM)");
		}

		if (operand.scale == SCALE_IMM8 && bits == 8) 
		{
			emit->Write8(nops[op].imm8);
			immToWrite = 8;
		}
		else if ((operand.scale == SCALE_IMM16 && bits == 16) ||
				 (operand.scale == SCALE_IMM32 && bits == 32) || 
				 (operand.scale == SCALE_IMM32 && bits == 64))
		{
			emit->Write8(nops[op].imm32);
			immToWrite = bits == 16 ? 16 : 32;
		}
		else if ((operand.scale == SCALE_IMM8 && bits == 16) ||
				 (operand.scale == SCALE_IMM8 && bits == 32) ||
				 (operand.scale == SCALE_IMM8 && bits == 64))
		{
			emit->Write8(nops[op].simm8);
			immToWrite = 8;
		}
		else if (operand.scale == SCALE_IMM64 && bits == 64)
		{
			if (op == nrmMOV)
			{
				emit->Write8(0xB8 + (offsetOrBaseReg & 7));
				emit->Write64((u64)operand.offset);
				return;
			}
			_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Only MOV can take 64-bit imm");
		}
		else
		{
			_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Unhandled case");
		}
		_operandReg = (X64Reg)nops[op].ext; //pass extension in REG of ModRM
	}
	else
	{
		_operandReg = (X64Reg)operand.offsetOrBaseReg;
		WriteRex(emit, bits, bits, _operandReg);
		// mem/reg or reg/reg op
		if (toRM)
		{
			emit->Write8(bits == 8 ? nops[op].toRm8 : nops[op].toRm32);
			// _assert_msg_(DYNA_REC, code[-1] != 0xCC, "ARGH4");
		}
		else
		{
			emit->Write8(bits == 8 ? nops[op].fromRm8 : nops[op].fromRm32);
			// _assert_msg_(DYNA_REC, code[-1] != 0xCC, "ARGH5");
		}
	}
	WriteRest(emit, immToWrite>>3, _operandReg);
	switch (immToWrite)
	{
	case 0:
		break;
	case 8:
		emit->Write8((u8)operand.offset);
		break;
	case 16:
		emit->Write16((u16)operand.offset);
		break;
	case 32:
		emit->Write32((u32)operand.offset);
		break;
	default:
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - Unhandled case");
	}
}

void XEmitter::WriteNormalOp(XEmitter *emit, int bits, NormalOp op, const OpArg &a1, const OpArg &a2)
{
	if (a1.IsImm())
	{
		//Booh! Can't write to an imm
		_assert_msg_(DYNA_REC, 0, "WriteNormalOp - a1 cannot be imm");
		return;
	}
	if (a2.IsImm())
	{
		a1.WriteNormalOp(emit, true, op, a2, bits);
	}
	else
	{
		if (a1.IsSimpleReg())
		{
			a2.WriteNormalOp(emit, false, op, a1, bits);
		}
		else
		{
			a1.WriteNormalOp(emit, true, op, a2, bits);
		}
	}
}

void XEmitter::ADD (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmADD, a1, a2);}
void XEmitter::ADC (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmADC, a1, a2);}
void XEmitter::SUB (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmSUB, a1, a2);}
void XEmitter::SBB (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmSBB, a1, a2);}
void XEmitter::AND (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmAND, a1, a2);}
void XEmitter::OR  (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmOR , a1, a2);}
void XEmitter::XOR (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmXOR, a1, a2);}
void XEmitter::MOV (int bits, const OpArg &a1, const OpArg &a2) 
{
#ifdef _DEBUG
	_assert_msg_(DYNA_REC, !a1.IsSimpleReg() || !a2.IsSimpleReg() || a1.GetSimpleReg() != a2.GetSimpleReg(), "Redundant MOV @ %p - bug in JIT?", 
				 code); 
#endif
	WriteNormalOp(this, bits, nrmMOV, a1, a2);
}
void XEmitter::TEST(int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmTEST, a1, a2);}
void XEmitter::CMP (int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmCMP, a1, a2);}
void XEmitter::XCHG(int bits, const OpArg &a1, const OpArg &a2) {WriteNormalOp(this, bits, nrmXCHG, a1, a2);}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a1, OpArg a2)
{
	if (bits == 8) {
		_assert_msg_(DYNA_REC, 0, "IMUL - illegal bit size!");
		return;
	}
	if (a1.IsImm()) {
		_assert_msg_(DYNA_REC, 0, "IMUL - second arg cannot be imm!");
		return;
	}
	if (!a2.IsImm())
	{
		_assert_msg_(DYNA_REC, 0, "IMUL - third arg must be imm!");
		return;
	}

	if (bits == 16)
		Write8(0x66);
	a1.WriteRex(this, bits, bits, regOp);

	if (a2.GetImmBits() == 8) {
		Write8(0x6B);
		a1.WriteRest(this, 1, regOp);
		Write8((u8)a2.offset);
	} else {
		Write8(0x69);
		if (a2.GetImmBits() == 16 && bits == 16) {
			a1.WriteRest(this, 2, regOp);
			Write16((u16)a2.offset);
		} else if (a2.GetImmBits() == 32 &&
			(bits == 32 || bits == 64)) {
				a1.WriteRest(this, 4, regOp);
				Write32((u32)a2.offset);
		} else {
			_assert_msg_(DYNA_REC, 0, "IMUL - unhandled case!");
		}
	}
}

void XEmitter::IMUL(int bits, X64Reg regOp, OpArg a)
{
	if (bits == 8) {
		_assert_msg_(DYNA_REC, 0, "IMUL - illegal bit size!");
		return;
	}
	if (a.IsImm())
	{
		IMUL(bits, regOp, R(regOp), a) ;
		return;
	}

	if (bits == 16)
		Write8(0x66);
	a.WriteRex(this, bits, bits, regOp);
	Write8(0x0F);
	Write8(0xAF);
	a.WriteRest(this, 0, regOp);
}


// OG Xbox port: true when we must not emit any SSE2 (Coppermine target, or
// the PC harness simulating it via the `sse1` flag).
static inline bool UseSSE1()
{
#ifdef _M_X64
	return false;
#else
	return !cpu_info.bSSE2;
#endif
}

void XEmitter::WriteSSEOp(int size, u8 sseOp, bool packed, X64Reg regOp, OpArg arg, int extrabytes)
{
	// OG Xbox port: single choke point — every SSE emission passes through
	// here. In SSE1 mode, any SSE2-encoded op that was not intercepted by a
	// fallback rewrite fails loudly at block-compile time instead of
	// crashing the Coppermine with an illegal instruction.
	// size==64 covers all 66- and F2-prefixed forms; 5A/5B/E6 are the
	// unprefixed/F3 conversion opcodes that only exist in SSE2.
	if (UseSSE1() && (size == 64 || sseOp == 0x5A || sseOp == 0x5B || sseOp == 0xE6))
		PanicAlert("SSE1 fallback: unhandled SSE2 emission (op=%02x packed=%d size=%d)",
		           sseOp, (int)packed, size);

	if (size == 64 && packed)
		Write8(0x66); //this time, override goes upwards
	if (!packed)
		Write8(size == 64 ? 0xF2 : 0xF3);
	arg.operandReg = regOp;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(sseOp);
	arg.WriteRest(this, extrabytes);
}

// ---------------------------------------------------------------------------
// OG Xbox port: SSE1 fallback layer.
// SSE1 building blocks, x87 memory ops, and the emit-time rewrite helpers.
// Contract: bit-copies stay bit-exact (never routed through x87); double
// arithmetic goes through x87 (53-bit precision CW assumed, the Windows/
// Dolphin default); rare ops call the C thunk in x64SSE1Thunks.cpp.
// ---------------------------------------------------------------------------

void XEmitter::MOVLPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, 0x12, true, regOp, arg);}
void XEmitter::MOVLPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(32, 0x13, true, regOp, arg);}
void XEmitter::MOVHPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, 0x16, true, regOp, arg);}
void XEmitter::MOVHPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(32, 0x17, true, regOp, arg);}
void XEmitter::MOVLHPS(X64Reg dest, X64Reg src) {WriteSSEOp(32, 0x16, true, dest, R(src));}
void XEmitter::MOVHLPS(X64Reg dest, X64Reg src) {WriteSSEOp(32, 0x12, true, dest, R(src));}

void XEmitter::WriteX87Mem(u8 opcode, OpArg arg, int ext)
{
	arg.operandReg = ext;
	arg.WriteRex(this, 0, 0);
	Write8(opcode);
	arg.WriteRest(this);
}

void XEmitter::FLD32(OpArg arg)             {WriteX87Mem(0xD9, arg, 0);}
void XEmitter::FLD64(OpArg arg)             {WriteX87Mem(0xDD, arg, 0);}
void XEmitter::FSTP32(OpArg arg)            {WriteX87Mem(0xD9, arg, 3);}
void XEmitter::FSTP64(OpArg arg)            {WriteX87Mem(0xDD, arg, 3);}
void XEmitter::FARITH64(int ext, OpArg arg) {WriteX87Mem(0xDC, arg, ext);}
void XEmitter::FSQRT()       {Write8(0xD9); Write8(0xFA);}
void XEmitter::FUCOMIP_ST1() {Write8(0xDF); Write8(0xE9);}
void XEmitter::FSTP_ST0()    {Write8(0xDD); Write8(0xD8);}
void XEmitter::PUSHAD()      {Write8(0x60);}
void XEmitter::POPAD()       {Write8(0x61);}

// Make a 64-bit source operand readable by x87: registers spill their low
// qword to scratch (bit-exact SSE1 move), memory is used directly.
OpArg XEmitter::SSE1SpillSrc64(OpArg src)
{
	if (src.IsSimpleReg())
	{
		MOVLPS(M(SSE1Fallback::g_scratch + 16), src.GetSimpleReg());
		return M(SSE1Fallback::g_scratch + 16);
	}
	return src;
}

// dst.q0 = dst.q0 <op> src.q0 via x87; dst[127:64] preserved.
void XEmitter::SSE1ScalarArith(int ext, X64Reg dst, OpArg src)
{
	OpArg s = SSE1SpillSrc64(src);
	MOVLPS(M(SSE1Fallback::g_scratch), dst);
	FLD64(M(SSE1Fallback::g_scratch));
	FARITH64(ext, s);
	FSTP64(M(SSE1Fallback::g_scratch));
	MOVLPS(dst, M(SSE1Fallback::g_scratch));
}

// Both qwords of dst <op>= the corresponding qword of src, via x87.
void XEmitter::SSE1PackedArith(int ext, X64Reg dst, OpArg src)
{
	OpArg s0, s1;
	if (src.IsSimpleReg())
	{
		MOVAPS(M(SSE1Fallback::g_scratch + 16), src.GetSimpleReg());
		s0 = M(SSE1Fallback::g_scratch + 16);
		s1 = M(SSE1Fallback::g_scratch + 24);
	}
	else
	{
		s0 = src;
		s1 = src;
		s1.offset += 8;
	}
	MOVAPS(M(SSE1Fallback::g_scratch), dst);
	FLD64(M(SSE1Fallback::g_scratch));
	FARITH64(ext, s0);
	FSTP64(M(SSE1Fallback::g_scratch));
	FLD64(M(SSE1Fallback::g_scratch + 8));
	FARITH64(ext, s1);
	FSTP64(M(SSE1Fallback::g_scratch + 8));
	MOVAPS(dst, M(SSE1Fallback::g_scratch));
}

// Generic path for rare ops: spill operands to scratch, call the C thunk.
// EFLAGS are clobbered (the replaced SSE ops don't set flags; the JIT never
// carries live flags across them).
void XEmitter::SSE1Thunk(int op, u8 imm, X64Reg dst, const OpArg *src, int srcBytes)
{
	MOVAPS(M(SSE1Fallback::g_scratch), dst);
	if (src && src->IsSimpleReg())
		MOVAPS(M(SSE1Fallback::g_scratch + 16), src->GetSimpleReg());
	PUSHAD();
	if (src && !src->IsSimpleReg())
	{
		for (int k = 0; k < srcBytes; k += 4)
		{
			OpArg part = *src;
			part.offset += k;
			MOV(32, R(EAX), part);
			MOV(32, M(SSE1Fallback::g_scratch + 16 + k), R(EAX));
		}
	}
	PUSH(32, Imm32(imm));
	PUSH(32, Imm32((u32)op));
	CALL((const void*)&SSE1Fallback_Thunk);
	ADD(32, R(ESP), Imm8(8));
	POPAD();
	MOVAPS(dst, M(SSE1Fallback::g_scratch));
}

void XEmitter::MOVD_xmm(X64Reg dest, const OpArg &arg)
{
	if (UseSSE1())
	{
		if (arg.IsSimpleReg())
		{
			// GPR -> xmm[31:0], upper 96 bits zeroed
			MOV(32, M(SSE1Fallback::g_scratch + 16), arg);
			MOVSS(dest, M(SSE1Fallback::g_scratch + 16)); // mem-form MOVSS zeroes upper
		}
		else
		{
			MOVSS(dest, arg);
		}
		return;
	}
	WriteSSEOp(64, 0x6E, true, dest, arg, 0);
}
void XEmitter::MOVD_xmm(const OpArg &arg, X64Reg src)
{
	if (UseSSE1())
	{
		if (arg.IsSimpleReg())
		{
			MOVSS(M(SSE1Fallback::g_scratch + 16), src);
			MOV(32, arg, M(SSE1Fallback::g_scratch + 16));
		}
		else
		{
			MOVSS(arg, src);
		}
		return;
	}
	WriteSSEOp(64, 0x7E, true, src, arg, 0);
}

void XEmitter::MOVQ_xmm(X64Reg dest, OpArg arg) {
	if (UseSSE1())
	{
		// MOVQ load zeroes the upper 64 bits. Spill src BEFORE the XORPS —
		// MOVQ_xmm(r, R(r)) ("clear my upper half") is a real JIT idiom.
		if (arg.IsSimpleReg())
		{
			MOVLPS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			XORPS(dest, R(dest));
			MOVLPS(dest, M(SSE1Fallback::g_scratch + 16));
		}
		else
		{
			XORPS(dest, R(dest));
			MOVLPS(dest, arg);
		}
		return;
	}
#ifdef _M_X64
		// Alternate encoding
		// This does not display correctly in MSVC's debugger, it thinks it's a MOVD
		arg.operandReg = dest;
		Write8(0x66);
		arg.WriteRex(this, 64, 0);
		Write8(0x0f);
		Write8(0x6E);
		arg.WriteRest(this, 0);
#else
		arg.operandReg = dest;
		Write8(0xF3);
		Write8(0x0f);
		Write8(0x7E);
		arg.WriteRest(this, 0);
#endif
}

void XEmitter::MOVQ_xmm(OpArg arg, X64Reg src) {
	if (arg.IsSimpleReg())
		PanicAlert("Emitter: MOVQ_xmm doesn't support single registers as destination");
	if (UseSSE1())
	{
		MOVLPS(arg, src);
		return;
	}
	if (src > 7)
	{
		// Alternate encoding
		// This does not display correctly in MSVC's debugger, it thinks it's a MOVD
		arg.operandReg = src;
		Write8(0x66);
		arg.WriteRex(this, 64, 0);
		Write8(0x0f);
		Write8(0x7E);
		arg.WriteRest(this, 0);
	} else {
		arg.operandReg = src;
		arg.WriteRex(this, 0, 0);
		Write8(0x66);
		Write8(0x0f);
		Write8(0xD6);
		arg.WriteRest(this, 0);
	}
}

void XEmitter::WriteMXCSR(OpArg arg, int ext)
{
	if (arg.IsImm() || arg.IsSimpleReg()) 
		_assert_msg_(DYNA_REC, 0, "MXCSR - invalid operand");

	arg.operandReg = ext;
	arg.WriteRex(this, 0, 0);
	Write8(0x0F);
	Write8(0xAE);
	arg.WriteRest(this);
}

void XEmitter::STMXCSR(OpArg memloc) {WriteMXCSR(memloc, 3);}
void XEmitter::LDMXCSR(OpArg memloc) {WriteMXCSR(memloc, 2);}

void XEmitter::MOVNTDQ(OpArg arg, X64Reg regOp)   {if (UseSSE1()) {WriteSSEOp(32, sseMOVNTP, true, regOp, arg); return;} WriteSSEOp(64, sseMOVNTDQ, true, regOp, arg);} // MOVNTPS is an equivalent 16-byte NT store
void XEmitter::MOVNTPS(OpArg arg, X64Reg regOp)   {WriteSSEOp(32, sseMOVNTP, true, regOp, arg);}
void XEmitter::MOVNTPD(OpArg arg, X64Reg regOp)   {WriteSSEOp(UseSSE1() ? 32 : 64, sseMOVNTP, true, regOp, arg);}

void XEmitter::ADDSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseADD, false, regOp, arg);}
void XEmitter::ADDSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1ScalarArith(0, regOp, arg); return;} WriteSSEOp(64, sseADD, false, regOp, arg);}
void XEmitter::SUBSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseSUB, false, regOp, arg);}
void XEmitter::SUBSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1ScalarArith(4, regOp, arg); return;} WriteSSEOp(64, sseSUB, false, regOp, arg);}
void XEmitter::CMPSS(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(32, sseCMP, false, regOp, arg,1); Write8(compare);}
void XEmitter::CMPSD(X64Reg regOp, OpArg arg, u8 compare)   {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_CMPSD, compare, regOp, &arg, 8); return;} WriteSSEOp(64, sseCMP, false, regOp, arg,1); Write8(compare);}
void XEmitter::MULSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMUL, false, regOp, arg);}
void XEmitter::MULSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1ScalarArith(1, regOp, arg); return;} WriteSSEOp(64, sseMUL, false, regOp, arg);}
void XEmitter::DIVSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseDIV, false, regOp, arg);}
void XEmitter::DIVSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1ScalarArith(6, regOp, arg); return;} WriteSSEOp(64, sseDIV, false, regOp, arg);}
void XEmitter::MINSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMIN, false, regOp, arg);}
void XEmitter::MINSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_MINSD, 0, regOp, &arg, 8); return;} WriteSSEOp(64, sseMIN, false, regOp, arg);}
void XEmitter::MAXSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMAX, false, regOp, arg);}
void XEmitter::MAXSD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_MAXSD, 0, regOp, &arg, 8); return;} WriteSSEOp(64, sseMAX, false, regOp, arg);}
void XEmitter::SQRTSS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseSQRT, false, regOp, arg);}
void XEmitter::SQRTSD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s = SSE1SpillSrc64(arg);
		FLD64(s);
		FSQRT();
		FSTP64(M(SSE1Fallback::g_scratch));
		MOVLPS(regOp, M(SSE1Fallback::g_scratch));
		return;
	}
	WriteSSEOp(64, sseSQRT, false, regOp, arg);
}
void XEmitter::RSQRTSS(X64Reg regOp, OpArg arg) {WriteSSEOp(32, sseRSQRT, false, regOp, arg);}

void XEmitter::ADDPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseADD, true, regOp, arg);}
void XEmitter::ADDPD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1PackedArith(0, regOp, arg); return;} WriteSSEOp(64, sseADD, true, regOp, arg);}
void XEmitter::SUBPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseSUB, true, regOp, arg);}
void XEmitter::SUBPD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1PackedArith(4, regOp, arg); return;} WriteSSEOp(64, sseSUB, true, regOp, arg);}
void XEmitter::CMPPS(X64Reg regOp, OpArg arg, u8 compare)   {WriteSSEOp(32, sseCMP, true, regOp, arg,1); Write8(compare);}
void XEmitter::CMPPD(X64Reg regOp, OpArg arg, u8 compare)   {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_CMPPD, compare, regOp, &arg, 16); return;} WriteSSEOp(64, sseCMP, true, regOp, arg,1); Write8(compare);}
void XEmitter::ANDPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseAND, true, regOp, arg);}
void XEmitter::ANDPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(UseSSE1() ? 32 : 64, sseAND, true, regOp, arg);}
void XEmitter::ANDNPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseANDN, true, regOp, arg);}
void XEmitter::ANDNPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(UseSSE1() ? 32 : 64, sseANDN, true, regOp, arg);}
void XEmitter::ORPS(X64Reg regOp, OpArg arg)    {WriteSSEOp(32, sseOR, true, regOp, arg);}
void XEmitter::ORPD(X64Reg regOp, OpArg arg)    {WriteSSEOp(UseSSE1() ? 32 : 64, sseOR, true, regOp, arg);}
void XEmitter::XORPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseXOR, true, regOp, arg);}
void XEmitter::XORPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(UseSSE1() ? 32 : 64, sseXOR, true, regOp, arg);}
void XEmitter::MULPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMUL, true, regOp, arg);}
void XEmitter::MULPD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1PackedArith(1, regOp, arg); return;} WriteSSEOp(64, sseMUL, true, regOp, arg);}
void XEmitter::DIVPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseDIV, true, regOp, arg);}
void XEmitter::DIVPD(X64Reg regOp, OpArg arg)   {if (UseSSE1()) {SSE1PackedArith(6, regOp, arg); return;} WriteSSEOp(64, sseDIV, true, regOp, arg);}
void XEmitter::MINPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMIN, true, regOp, arg);}
void XEmitter::MINPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(64, sseMIN, true, regOp, arg);}
void XEmitter::MAXPS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMAX, true, regOp, arg);}
void XEmitter::MAXPD(X64Reg regOp, OpArg arg)   {WriteSSEOp(64, sseMAX, true, regOp, arg);}
void XEmitter::SQRTPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseSQRT, true, regOp, arg);}
void XEmitter::SQRTPD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s0, s1;
		if (arg.IsSimpleReg())
		{
			MOVAPS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			s0 = M(SSE1Fallback::g_scratch + 16);
			s1 = M(SSE1Fallback::g_scratch + 24);
		}
		else
		{
			s0 = arg;
			s1 = arg;
			s1.offset += 8;
		}
		FLD64(s0); FSQRT(); FSTP64(M(SSE1Fallback::g_scratch));
		FLD64(s1); FSQRT(); FSTP64(M(SSE1Fallback::g_scratch + 8));
		MOVAPS(regOp, M(SSE1Fallback::g_scratch));
		return;
	}
	WriteSSEOp(64, sseSQRT, true, regOp, arg);
}
void XEmitter::RSQRTPS(X64Reg regOp, OpArg arg) {WriteSSEOp(32, sseRSQRT, true, regOp, arg);}
void XEmitter::SHUFPS(X64Reg regOp, OpArg arg, u8 shuffle) {WriteSSEOp(32, sseSHUF, true, regOp, arg,1); Write8(shuffle);}
void XEmitter::SHUFPD(X64Reg regOp, OpArg arg, u8 shuffle)
{
	if (UseSSE1())
	{
		// Map the 2-bit qword selector onto the equivalent SHUFPS dword pairs.
		int a = shuffle & 1, b = (shuffle >> 1) & 1;
		u8 ps = (u8)((2 * a) | ((2 * a + 1) << 2) | ((2 * b) << 4) | ((2 * b + 1) << 6));
		SHUFPS(regOp, arg, ps);
		return;
	}
	WriteSSEOp(64, sseSHUF, true, regOp, arg,1); Write8(shuffle);
}

void XEmitter::COMISS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseCOMIS, true, regOp, arg);} //weird that these should be packed
void XEmitter::COMISD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		// x87 FUCOMIP sets ZF/PF/CF exactly like UCOMISD/COMISD
		OpArg s = SSE1SpillSrc64(arg);
		MOVLPS(M(SSE1Fallback::g_scratch), regOp);
		FLD64(s);                          // ST0 = b
		FLD64(M(SSE1Fallback::g_scratch)); // ST0 = a, ST1 = b
		FUCOMIP_ST1();
		FSTP_ST0();
		return;
	}
	WriteSSEOp(64, sseCOMIS, true, regOp, arg);
} //ordered
void XEmitter::UCOMISS(X64Reg regOp, OpArg arg) {WriteSSEOp(32, sseUCOMIS, true, regOp, arg);} //unordered
void XEmitter::UCOMISD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s = SSE1SpillSrc64(arg);
		MOVLPS(M(SSE1Fallback::g_scratch), regOp);
		FLD64(s);
		FLD64(M(SSE1Fallback::g_scratch));
		FUCOMIP_ST1();
		FSTP_ST0();
		return;
	}
	WriteSSEOp(64, sseUCOMIS, true, regOp, arg);
}

void XEmitter::MOVAPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseMOVAPfromRM, true, regOp, arg);}
void XEmitter::MOVAPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(UseSSE1() ? 32 : 64, sseMOVAPfromRM, true, regOp, arg);}
void XEmitter::MOVAPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(32, sseMOVAPtoRM, true, regOp, arg);}
void XEmitter::MOVAPD(OpArg arg, X64Reg regOp)  {WriteSSEOp(UseSSE1() ? 32 : 64, sseMOVAPtoRM, true, regOp, arg);}

void XEmitter::MOVUPS(X64Reg regOp, OpArg arg)  {WriteSSEOp(32, sseMOVUPfromRM, true, regOp, arg);}
void XEmitter::MOVUPD(X64Reg regOp, OpArg arg)  {WriteSSEOp(UseSSE1() ? 32 : 64, sseMOVUPfromRM, true, regOp, arg);}
void XEmitter::MOVUPS(OpArg arg, X64Reg regOp)  {WriteSSEOp(32, sseMOVUPtoRM, true, regOp, arg);}
void XEmitter::MOVUPD(OpArg arg, X64Reg regOp)  {WriteSSEOp(UseSSE1() ? 32 : 64, sseMOVUPtoRM, true, regOp, arg);}

void XEmitter::MOVSS(X64Reg regOp, OpArg arg)   {WriteSSEOp(32, sseMOVUPfromRM, false, regOp, arg);}
void XEmitter::MOVSD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		if (arg.IsSimpleReg())
		{
			// reg-reg MOVSD merges: dst.q0 = src.q0, dst[127:64] preserved
			MOVLPS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			MOVLPS(regOp, M(SSE1Fallback::g_scratch + 16));
		}
		else
		{
			// mem load zeroes the upper bits per SSE2 spec
			XORPS(regOp, R(regOp));
			MOVLPS(regOp, arg);
		}
		return;
	}
	WriteSSEOp(64, sseMOVUPfromRM, false, regOp, arg);
}
void XEmitter::MOVSS(OpArg arg, X64Reg regOp)   {WriteSSEOp(32, sseMOVUPtoRM, false, regOp, arg);}
void XEmitter::MOVSD(OpArg arg, X64Reg regOp)
{
	if (UseSSE1())
	{
		if (arg.IsSimpleReg())
		{
			MOVLPS(M(SSE1Fallback::g_scratch + 16), regOp);
			MOVLPS(arg.GetSimpleReg(), M(SSE1Fallback::g_scratch + 16));
		}
		else
		{
			MOVLPS(arg, regOp);
		}
		return;
	}
	WriteSSEOp(64, sseMOVUPtoRM, false, regOp, arg);
}

void XEmitter::CVTPS2PD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		// Widen the two low singles of src to doubles.
		OpArg s0, s1;
		if (arg.IsSimpleReg())
		{
			MOVLPS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			s0 = M(SSE1Fallback::g_scratch + 16);
			s1 = M(SSE1Fallback::g_scratch + 20);
		}
		else
		{
			s0 = arg;
			s1 = arg;
			s1.offset += 4;
		}
		FLD32(s0); FSTP64(M(SSE1Fallback::g_scratch));
		FLD32(s1); FSTP64(M(SSE1Fallback::g_scratch + 8));
		MOVAPS(regOp, M(SSE1Fallback::g_scratch));
		return;
	}
	WriteSSEOp(32, 0x5A, true, regOp, arg);
}
void XEmitter::CVTPD2PS(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s0, s1;
		if (arg.IsSimpleReg())
		{
			MOVAPS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			s0 = M(SSE1Fallback::g_scratch + 16);
			s1 = M(SSE1Fallback::g_scratch + 24);
		}
		else
		{
			s0 = arg;
			s1 = arg;
			s1.offset += 8;
		}
		FLD64(s0); FSTP32(M(SSE1Fallback::g_scratch));
		FLD64(s1); FSTP32(M(SSE1Fallback::g_scratch + 4));
		XORPS(regOp, R(regOp)); // upper 64 bits zeroed per spec
		MOVLPS(regOp, M(SSE1Fallback::g_scratch));
		return;
	}
	WriteSSEOp(64, 0x5A, true, regOp, arg);
}

void XEmitter::CVTSD2SS(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s = SSE1SpillSrc64(arg);
		MOVLPS(M(SSE1Fallback::g_scratch), regOp); // preserve dst[63:32]
		FLD64(s);
		FSTP32(M(SSE1Fallback::g_scratch));        // rounds to single
		MOVLPS(regOp, M(SSE1Fallback::g_scratch));
		return;
	}
	WriteSSEOp(64, 0x5A, false, regOp, arg);
}
void XEmitter::CVTSS2SD(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		OpArg s;
		if (arg.IsSimpleReg())
		{
			MOVSS(M(SSE1Fallback::g_scratch + 16), arg.GetSimpleReg());
			s = M(SSE1Fallback::g_scratch + 16);
		}
		else
		{
			s = arg;
		}
		FLD32(s);
		FSTP64(M(SSE1Fallback::g_scratch));
		MOVLPS(regOp, M(SSE1Fallback::g_scratch)); // upper 64 bits preserved per spec
		return;
	}
	WriteSSEOp(32, 0x5A, false, regOp, arg);
}
void XEmitter::CVTSD2SI(X64Reg regOp, OpArg arg) {WriteSSEOp(64, 0x2D, false, regOp, arg);}

void XEmitter::CVTDQ2PD(X64Reg regOp, OpArg arg) {WriteSSEOp(32, 0xE6, false, regOp, arg);}
void XEmitter::CVTDQ2PS(X64Reg regOp, OpArg arg)
{
	if (UseSSE1())
	{
		SSE1Thunk(SSE1Fallback::OP_CVTDQ2PS, 0, regOp, &arg, 16);
		return;
	}
	WriteSSEOp(32, 0x5B, true, regOp, arg);
}
void XEmitter::CVTPD2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(64, 0xE6, false, regOp, arg);}
void XEmitter::CVTPS2DQ(X64Reg regOp, OpArg arg) {WriteSSEOp(64, 0x5B, true, regOp, arg);}

void XEmitter::CVTTSS2SI(X64Reg xregdest, OpArg arg) {WriteSSEOp(32, 0x2C, false, xregdest, arg);}
void XEmitter::CVTTPS2DQ(X64Reg xregdest, OpArg arg)
{
	if (UseSSE1())
	{
		SSE1Thunk(SSE1Fallback::OP_CVTTPS2DQ, 0, xregdest, &arg, 16);
		return;
	}
	WriteSSEOp(32, 0x5B, false, xregdest, arg);
}

void XEmitter::MASKMOVDQU(X64Reg dest, X64Reg src)  {WriteSSEOp(64, sseMASKMOVDQU, true, dest, R(src));}

void XEmitter::MOVMSKPS(X64Reg dest, OpArg arg) {WriteSSEOp(32, 0x50, true, dest, arg);}
void XEmitter::MOVMSKPD(X64Reg dest, OpArg arg) {WriteSSEOp(64, 0x50, true, dest, arg);}

void XEmitter::LDDQU(X64Reg dest, OpArg arg)    {WriteSSEOp(64, sseLDDQU, false, dest, arg);} // For integer data only

// THESE TWO ARE UNTESTED.
void XEmitter::UNPCKLPS(X64Reg dest, OpArg arg) {WriteSSEOp(32, 0x14, true, dest, arg);}
void XEmitter::UNPCKHPS(X64Reg dest, OpArg arg) {WriteSSEOp(32, 0x15, true, dest, arg);}

void XEmitter::UNPCKLPD(X64Reg dest, OpArg arg)
{
	if (UseSSE1())
	{
		// dest = {dest.q0, src.q0}
		if (arg.IsSimpleReg())
			MOVLHPS(dest, arg.GetSimpleReg());
		else
			MOVHPS(dest, arg);
		return;
	}
	WriteSSEOp(64, 0x14, true, dest, arg);
}
void XEmitter::UNPCKHPD(X64Reg dest, OpArg arg)
{
	if (UseSSE1())
	{
		// dest = {dest.q1, src.q1}
		SHUFPS(dest, arg, 0xEE);
		return;
	}
	WriteSSEOp(64, 0x15, true, dest, arg);
}

void XEmitter::MOVDDUP(X64Reg regOp, OpArg arg) 
{
	if (cpu_info.bSSE3)
	{
		WriteSSEOp(64, 0x12, false, regOp, arg); //SSE3 movddup
	}
	else
	{
		// Simulate this instruction with SSE2 instructions
		if (!arg.IsSimpleReg(regOp))
			MOVSD(regOp, arg);
		UNPCKLPD(regOp, R(regOp));
	}
}

//There are a few more left

// Also some integer instructions are missing
void XEmitter::PACKSSDW(X64Reg dest, OpArg arg) {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PACKSSDW, 0, dest, &arg, 16); return;} WriteSSEOp(64, 0x6B, true, dest, arg);}
void XEmitter::PACKSSWB(X64Reg dest, OpArg arg) {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PACKSSWB, 0, dest, &arg, 16); return;} WriteSSEOp(64, 0x63, true, dest, arg);}
//void PACKUSDW(X64Reg dest, OpArg arg) {WriteSSEOp(64, 0x66, true, dest, arg);} // WRONG
void XEmitter::PACKUSWB(X64Reg dest, OpArg arg) {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PACKUSWB, 0, dest, &arg, 16); return;} WriteSSEOp(64, 0x67, true, dest, arg);}

void XEmitter::PUNPCKLBW(X64Reg dest, const OpArg &arg) {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PUNPCKLBW, 0, dest, &arg, 16); return;} WriteSSEOp(64, 0x60, true, dest, arg);}
void XEmitter::PUNPCKLWD(X64Reg dest, const OpArg &arg) {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PUNPCKLWD, 0, dest, &arg, 16); return;} WriteSSEOp(64, 0x61, true, dest, arg);}
void XEmitter::PUNPCKLDQ(X64Reg dest, const OpArg &arg) {if (UseSSE1()) {WriteSSEOp(32, 0x14, true, dest, arg); return;} WriteSSEOp(64, 0x62, true, dest, arg);} // UNPCKLPS is bit-identical
//void PUNPCKLQDQ(X64Reg dest, OpArg arg) {WriteSSEOp(64, 0x60, true, dest, arg);}

void XEmitter::PSRLW(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSRLW, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x71, true, (X64Reg)2, R(reg));
	Write8(shift);
}

void XEmitter::PSRLD(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSRLD, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x72, true, (X64Reg)2, R(reg));
	Write8(shift);
}

void XEmitter::PSRLQ(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSRLQ, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x73, true, (X64Reg)2, R(reg));
	Write8(shift);
}

void XEmitter::PSLLW(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSLLW, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x71, true, (X64Reg)6, R(reg));
	Write8(shift);
}

void XEmitter::PSLLD(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSLLD, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x72, true, (X64Reg)6, R(reg));
	Write8(shift);
}

void XEmitter::PSLLQ(X64Reg reg, int shift) {
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSLLQ, (u8)shift, reg, NULL, 0); return;}
	WriteSSEOp(64, 0x73, true, (X64Reg)6, R(reg));
	Write8(shift);
}

// WARNING not REX compatible
void XEmitter::PSRAW(X64Reg reg, int shift) {
	if (reg > 7)
		PanicAlert("The PSRAW-emitter does not support regs above 7");
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSRAW, (u8)shift, reg, NULL, 0); return;}
	Write8(0x66);
	Write8(0x0f);
	Write8(0x71);
	Write8(0xE0 | reg);
	Write8(shift);
}

// WARNING not REX compatible
void XEmitter::PSRAD(X64Reg reg, int shift) {
	if (reg > 7)
		PanicAlert("The PSRAD-emitter does not support regs above 7");
	if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSRAD, (u8)shift, reg, NULL, 0); return;}
	Write8(0x66);
	Write8(0x0f);
	Write8(0x72);
	Write8(0xE0 | reg);
	Write8(shift);
}

void XEmitter::PSHUFB(X64Reg dest, OpArg arg) {
	if (!cpu_info.bSSSE3) {
		PanicAlert("Trying to use PSHUFB on a system that doesn't support it. Bad programmer.");
	}
	Write8(0x66);
	arg.operandReg = dest;
	arg.WriteRex(this, 0, 0);
	Write8(0x0f);
	Write8(0x38);
	Write8(0x00);
	arg.WriteRest(this, 0);
}

// OG Xbox port: the SSE1 float-logic ops are bit-identical replacements.
void XEmitter::PAND(X64Reg dest, OpArg arg)     {if (UseSSE1()) {ANDPS(dest, arg); return;}  WriteSSEOp(64, 0xDB, true, dest, arg);}
void XEmitter::PANDN(X64Reg dest, OpArg arg)    {if (UseSSE1()) {ANDNPS(dest, arg); return;} WriteSSEOp(64, 0xDF, true, dest, arg);}
void XEmitter::PXOR(X64Reg dest, OpArg arg)     {if (UseSSE1()) {XORPS(dest, arg); return;}  WriteSSEOp(64, 0xEF, true, dest, arg);}
void XEmitter::POR(X64Reg dest, OpArg arg)      {if (UseSSE1()) {ORPS(dest, arg); return;}   WriteSSEOp(64, 0xEB, true, dest, arg);}

void XEmitter::PADDB(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xFC, true, dest, arg);}
void XEmitter::PADDW(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xFD, true, dest, arg);}
void XEmitter::PADDD(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xFE, true, dest, arg);}
void XEmitter::PADDQ(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xD4, true, dest, arg);}

void XEmitter::PADDSB(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xEC, true, dest, arg);}
void XEmitter::PADDSW(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xED, true, dest, arg);}
void XEmitter::PADDUSB(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0xDC, true, dest, arg);}
void XEmitter::PADDUSW(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0xDD, true, dest, arg);}

void XEmitter::PSUBB(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xF8, true, dest, arg);}
void XEmitter::PSUBW(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xF9, true, dest, arg);}
void XEmitter::PSUBD(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xFA, true, dest, arg);}
void XEmitter::PSUBQ(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xDB, true, dest, arg);}

void XEmitter::PSUBSB(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xE8, true, dest, arg);}
void XEmitter::PSUBSW(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xE9, true, dest, arg);}
void XEmitter::PSUBUSB(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0xD8, true, dest, arg);}
void XEmitter::PSUBUSW(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0xD9, true, dest, arg);}

void XEmitter::PAVGB(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xE0, true, dest, arg);}
void XEmitter::PAVGW(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xE3, true, dest, arg);}

void XEmitter::PCMPEQB(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x74, true, dest, arg);}
void XEmitter::PCMPEQW(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x75, true, dest, arg);}
void XEmitter::PCMPEQD(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x76, true, dest, arg);}

void XEmitter::PCMPGTB(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x64, true, dest, arg);}
void XEmitter::PCMPGTW(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x65, true, dest, arg);}
void XEmitter::PCMPGTD(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0x66, true, dest, arg);}

void XEmitter::PEXTRW(X64Reg dest, OpArg arg, u8 subreg)    {WriteSSEOp(64, 0xC5, true, dest, arg); Write8(subreg);}
void XEmitter::PINSRW(X64Reg dest, OpArg arg, u8 subreg)    {WriteSSEOp(64, 0xC4, true, dest, arg); Write8(subreg);}

void XEmitter::PMADDWD(X64Reg dest, OpArg arg)  {WriteSSEOp(64, 0xF5, true, dest, arg); }
void XEmitter::PSADBW(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xF6, true, dest, arg);}

void XEmitter::PMAXSW(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xEE, true, dest, arg); }
void XEmitter::PMAXUB(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xDE, true, dest, arg); }
void XEmitter::PMINSW(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xEA, true, dest, arg); }
void XEmitter::PMINUB(X64Reg dest, OpArg arg)   {WriteSSEOp(64, 0xDA, true, dest, arg); }

void XEmitter::PMOVMSKB(X64Reg dest, OpArg arg)    {WriteSSEOp(64, 0xD7, true, dest, arg); }

void XEmitter::PSHUFLW(X64Reg regOp, OpArg arg, u8 shuffle)   {if (UseSSE1()) {SSE1Thunk(SSE1Fallback::OP_PSHUFLW, shuffle, regOp, &arg, 16); return;} WriteSSEOp(64, 0x70, false, regOp, arg, 1); Write8(shuffle);}

// Prefixes

void XEmitter::LOCK()  { Write8(0xF0); }
void XEmitter::REP()   { Write8(0xF3); }
void XEmitter::REPNE() { Write8(0xF2); }

void XEmitter::FWAIT()
{
	Write8(0x9B);
}

void XEmitter::RTDSC() { Write8(0x0F); Write8(0x31); }
	
// helper routines for setting pointers
void XEmitter::CallCdeclFunction3(void* fnptr, u32 arg0, u32 arg1, u32 arg2)
{
	using namespace Gen;
#ifdef _M_X64

#ifdef _MSC_VER
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8),  Imm32(arg2));
	CALL(fnptr);
#else
	MOV(32, R(RDI), Imm32(arg0));
	MOV(32, R(RSI), Imm32(arg1));
	MOV(32, R(RDX), Imm32(arg2));
	CALL(fnptr);
#endif

#else
	ABI_AlignStack(3 * 4);
	PUSH(32, Imm32(arg2));
	PUSH(32, Imm32(arg1));
	PUSH(32, Imm32(arg0));
	CALL(fnptr);
#ifdef _WIN32
	// don't inc stack
#else
	ABI_RestoreStack(3 * 4);
#endif
#endif
}

void XEmitter::CallCdeclFunction4(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
	using namespace Gen;
#ifdef _M_X64

#ifdef _MSC_VER
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	MOV(32, R(R9), Imm32(arg3));
	CALL(fnptr);
#else
	MOV(32, R(RDI), Imm32(arg0));
	MOV(32, R(RSI), Imm32(arg1));
	MOV(32, R(RDX), Imm32(arg2));
	MOV(32, R(RCX), Imm32(arg3));
	CALL(fnptr);
#endif

#else
	ABI_AlignStack(4 * 4);
	PUSH(32, Imm32(arg3));
	PUSH(32, Imm32(arg2));
	PUSH(32, Imm32(arg1));
	PUSH(32, Imm32(arg0));
	CALL(fnptr);
#ifdef _WIN32
	// don't inc stack
#else
	ABI_RestoreStack(4 * 4);
#endif
#endif
}

void XEmitter::CallCdeclFunction5(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	using namespace Gen;
#ifdef _M_X64

#ifdef _MSC_VER
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8),  Imm32(arg2));
	MOV(32, R(R9),  Imm32(arg3));
	MOV(32, MDisp(RSP, 0x20), Imm32(arg4));
	CALL(fnptr);
#else
	MOV(32, R(RDI), Imm32(arg0));
	MOV(32, R(RSI), Imm32(arg1));
	MOV(32, R(RDX), Imm32(arg2));
	MOV(32, R(RCX), Imm32(arg3));
	MOV(32, R(R8),  Imm32(arg4));
	CALL(fnptr);
#endif

#else
	ABI_AlignStack(5 * 4);
	PUSH(32, Imm32(arg4));
	PUSH(32, Imm32(arg3));
	PUSH(32, Imm32(arg2));
	PUSH(32, Imm32(arg1));
	PUSH(32, Imm32(arg0));
	CALL(fnptr);
#ifdef _WIN32
	// don't inc stack
#else
	ABI_RestoreStack(5 * 4);
#endif
#endif
}

void XEmitter::CallCdeclFunction6(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	using namespace Gen;
#ifdef _M_X64

#ifdef _MSC_VER
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	MOV(32, R(R9), Imm32(arg3));
	MOV(32, MDisp(RSP, 0x20), Imm32(arg4));
	MOV(32, MDisp(RSP, 0x28), Imm32(arg5));
	CALL(fnptr);
#else
	MOV(32, R(RDI), Imm32(arg0));
	MOV(32, R(RSI), Imm32(arg1));
	MOV(32, R(RDX), Imm32(arg2));
	MOV(32, R(RCX), Imm32(arg3));
	MOV(32, R(R8), Imm32(arg4));
	MOV(32, R(R9), Imm32(arg5));
	CALL(fnptr);
#endif

#else
	ABI_AlignStack(6 * 4);
	PUSH(32, Imm32(arg5));
	PUSH(32, Imm32(arg4));
	PUSH(32, Imm32(arg3));
	PUSH(32, Imm32(arg2));
	PUSH(32, Imm32(arg1));
	PUSH(32, Imm32(arg0));
	CALL(fnptr);
#ifdef _WIN32
	// don't inc stack
#else
	ABI_RestoreStack(6 * 4);
#endif
#endif
}

#ifdef _M_X64

// See header
void XEmitter::___CallCdeclImport3(void* impptr, u32 arg0, u32 arg1, u32 arg2) {
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	CALLptr(M(impptr));
}
void XEmitter::___CallCdeclImport4(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3) {
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	MOV(32, R(R9), Imm32(arg3));
	CALLptr(M(impptr));
}
void XEmitter::___CallCdeclImport5(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4) {
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	MOV(32, R(R9), Imm32(arg3));
	MOV(32, MDisp(RSP, 0x20), Imm32(arg4));
	CALLptr(M(impptr));
}
void XEmitter::___CallCdeclImport6(void* impptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5) {
	MOV(32, R(RCX), Imm32(arg0));
	MOV(32, R(RDX), Imm32(arg1));
	MOV(32, R(R8), Imm32(arg2));
	MOV(32, R(R9), Imm32(arg3));
	MOV(32, MDisp(RSP, 0x20), Imm32(arg4));
	MOV(32, MDisp(RSP, 0x28), Imm32(arg5));
	CALLptr(M(impptr));
}

#endif

}
