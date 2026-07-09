// NV2A/D3D8 PerfQuery — STUB. D3D8 has no IDirect3DQuery9 occlusion query and the
// GC EFB pixel-count queries (used for the zcomploc/zcomp perf-query feature) need
// a bespoke NV2A path; that is deferred to task #15. Reporting 0 results is safe:
// only games that read the EFB pixel count depend on it, and correctness there is
// not required to un-hang the FIFO or render normally.

#include "RenderBase.h"

#include "D3DBase.h"
#include "PerfQuery.h"

namespace DX8
{

PerfQuery::PerfQuery()
	: m_query_read_pos(0)
	, m_query_count(0)
{
	memset(m_query_buffer, 0, sizeof(m_query_buffer));
	memset((void*)m_results, 0, sizeof(m_results));
}

PerfQuery::~PerfQuery()
{
}

void PerfQuery::CreateDeviceObjects()  {}
void PerfQuery::DestroyDeviceObjects() {}

void PerfQuery::EnableQuery(PerfQueryGroup type)  { (void)type; }
void PerfQuery::DisableQuery(PerfQueryGroup type) { (void)type; }

void PerfQuery::ResetQuery()
{
	m_query_count = 0;
	memset((void*)m_results, 0, sizeof(m_results));
}

u32 PerfQuery::GetQueryResult(PerfQueryType type)
{
	(void)type;
	return 0;
}

void PerfQuery::FlushResults() {}
void PerfQuery::WeakFlush()    {}
void PerfQuery::FlushOne()     {}

bool PerfQuery::IsFlushed() const
{
	return true;
}

}  // namespace DX8
