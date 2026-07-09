// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common.h"
#include "Thread.h"

#include "../DSPEmulator.h"
#include "../PowerPC/PowerPC.h"
#include "../Host.h"
#include "../Core.h"
#include "CPU.h"
#include "DSP.h"
#include "Movie.h"
#include "Memmap.h"
#include "ProcessorInterface.h"

#include "VideoBackendBase.h"

namespace
{
	static Common::Event m_StepEvent;
	static Common::Event *m_SyncEvent = NULL;
	static std::mutex m_csCpuOccupied;
}

void CCPU::Init(int cpu_core)
{
	PowerPC::Init(cpu_core);
	m_SyncEvent = NULL;
}

void CCPU::Shutdown()
{
	PowerPC::Shutdown();
	m_SyncEvent = NULL;
}

#ifdef _XBOX
extern "C" volatile unsigned g_xbox_cpucount;  // CPU-loop iterations (watchdog reads)
extern "C" volatile unsigned g_xbox_cpustate;  // last PowerPC state seen in the loop
// Live guest PC + key SPRs for the watchdog overlay — to diagnose how execution
// reached a bad PC: SRR0/SRR1 nonzero => arrived via an exception/interrupt
// (SRR0 = where it was interrupted); CTR/LR = indirect-branch targets.
extern "C" unsigned xbox_live_pc()   { return PC; }
extern "C" unsigned xbox_live_lr()   { return PowerPC::ppcState.spr[SPR_LR]; }
extern "C" unsigned xbox_live_ctr()  { return PowerPC::ppcState.spr[SPR_CTR]; }
extern "C" unsigned xbox_live_srr0() { return PowerPC::ppcState.spr[SPR_SRR0]; }
extern "C" unsigned xbox_live_srr1() { return PowerPC::ppcState.spr[SPR_SRR1]; }
extern "C" unsigned xbox_live_msr()  { return PowerPC::ppcState.msr; }
extern "C" unsigned xbox_live_gpr(unsigned n) { return PowerPC::ppcState.gpr[n & 31]; }
// Gekko SPRG0-3 (SPR 272-275) — the exception entry uses these as scratch to
// find the OSContext; if the JIT mishandles them, r4 comes out bad.
extern "C" unsigned xbox_live_sprg(unsigned n) { return PowerPC::ppcState.spr[272 + (n & 3)]; }
// PI interrupt cause/mask + the pending-exceptions bitfield. The 0x8023346C loop
// = handler reads (cause & mask)==0 yet the JIT keeps taking EXCEPTION_EXTERNAL_INT
// (bit 0x200). Watching IC/IM/EXC live shows which bit is stuck asserted.
extern "C" unsigned xbox_live_intcause()   { return ProcessorInterface::GetCause(); }
extern "C" unsigned xbox_live_intmask()    { return ProcessorInterface::GetMask(); }
extern "C" unsigned xbox_live_exceptions() { return PowerPC::ppcState.Exceptions; }
// Big-endian opcode at a guest address (for disassembling the stuck wait loop).
extern "C" unsigned xbox_guest_op(unsigned addr)
{
	unsigned char* p = Memory::GetPointer(addr);
	// Guard the watchdog thread reading guest RAM before Memory::Init maps it:
	// GetPointer can return a near-null (unmasked base) pointer during early
	// boot, which would fault. Only read a plainly-valid host pointer.
	if ((unsigned)p < 0x10000) return 0;
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
#endif

void CCPU::Run()
{
	std::lock_guard<std::mutex> lk(m_csCpuOccupied);
	Host_UpdateDisasmDialog();

	while (true)
	{
reswitch:
		switch (PowerPC::GetState())
		{
		case PowerPC::CPU_RUNNING:
#ifdef _XBOX
			g_xbox_cpustate = PowerPC::CPU_RUNNING;
			g_xbox_cpucount++;   // watchdog: advancing = RunLoop is returning (alive)
#endif
			//1: enter a fast runloop
			PowerPC::RunLoop();
			break;

		case PowerPC::CPU_STEPPING:
			m_csCpuOccupied.unlock();

#ifdef _XBOX
			g_xbox_cpustate = PowerPC::CPU_STEPPING; // watchdog shows STEPPING vs RUNNING
#endif
			//1: wait for step command..
			m_StepEvent.Wait();

			m_csCpuOccupied.lock();
			if (PowerPC::GetState() == PowerPC::CPU_POWERDOWN)
				return;
			if (PowerPC::GetState() != PowerPC::CPU_STEPPING)
				goto reswitch;

			//3: do a step
			PowerPC::SingleStep();

			//4: update disasm dialog
			if (m_SyncEvent)
			{
				m_SyncEvent->Set();
				m_SyncEvent = NULL;
			}
			Host_UpdateDisasmDialog();
			break;

		case PowerPC::CPU_POWERDOWN:
			//1: Exit loop!!
			return; 
		}
	}
}

void CCPU::Stop()
{
	PowerPC::Stop();
	m_StepEvent.Set();
}

bool CCPU::IsStepping()
{
	return PowerPC::GetState() == PowerPC::CPU_STEPPING;
}

void CCPU::Reset()
{

}

void CCPU::StepOpcode(Common::Event *event) 
{
	m_StepEvent.Set();
	if (PowerPC::GetState() == PowerPC::CPU_STEPPING)
	{
		m_SyncEvent = event;
	}
}

void CCPU::EnableStepping(const bool _bStepping)
{	
	if (_bStepping)
	{
		PowerPC::Pause();
		m_StepEvent.Reset();
		g_video_backend->EmuStateChange(EMUSTATE_CHANGE_PAUSE);
		DSP::GetDSPEmulator()->DSP_ClearAudioBuffer(true);
	}
	else
	{
		PowerPC::Start();
		m_StepEvent.Set();
		g_video_backend->EmuStateChange(EMUSTATE_CHANGE_PLAY);
		DSP::GetDSPEmulator()->DSP_ClearAudioBuffer(false);
	}
}

void CCPU::Break() 
{
	EnableStepping(true);
}

bool CCPU::PauseAndLock(bool doLock, bool unpauseOnUnlock)
{
	bool wasUnpaused = !IsStepping();
	if (doLock)
	{
		// we can't use EnableStepping, that would causes deadlocks with both audio and video
		PowerPC::Pause();
		if (!Core::IsCPUThread())
			m_csCpuOccupied.lock();
	}
	else
	{
		if (unpauseOnUnlock)
		{
			PowerPC::Start();
			m_StepEvent.Set();
		}

		if (!Core::IsCPUThread())
			m_csCpuOccupied.unlock();
	}
	return wasUnpaused;
}
