#ifndef _PERFQUERY_H_
#define _PERFQUERY_H_

#include "PerfQueryBase.h"

namespace DX8 {

class PerfQuery : public PerfQueryBase
{
public:
	PerfQuery();
	~PerfQuery();

	void EnableQuery(PerfQueryGroup type);
	void DisableQuery(PerfQueryGroup type);
	void ResetQuery();
	u32 GetQueryResult(PerfQueryType type);
	void FlushResults();
	bool IsFlushed() const;
	void CreateDeviceObjects();
	void DestroyDeviceObjects();

private:
	// D3D8 has no IDirect3DQuery9 occlusion query; NV2A EFB pixel-count queries
	// need a bespoke path (task #15). Kept as an opaque handle, stubbed for now.
	struct ActiveQuery
	{
		void* query;
		PerfQueryGroup query_type;
	};

	void WeakFlush();	
	// Only use when non-empty
	void FlushOne();

	// when testing in SMS: 64 was too small, 128 was ok
	static const int PERF_QUERY_BUFFER_SIZE = 512;

	ActiveQuery m_query_buffer[PERF_QUERY_BUFFER_SIZE];
	int m_query_read_pos;

	// TODO: sloppy
	volatile int m_query_count;
	volatile u32 m_results[PQG_NUM_MEMBERS];
};

} // namespace

#endif // _PERFQUERY_H_
