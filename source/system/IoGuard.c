#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp.h>
#include <ogc/irq.h>
#include <ogc/lwp_watchdog.h>
#include <hbc_agent.h>
#include "IoGuard.h"
#include "gecko.h"

#define SLOTS 4

typedef struct
{
	u64 start; //! 0: free
	int kind;
	u32 sector, count;
} Request;

typedef struct
{
	u32 requests, slow;
	u64 bytes, ticks, longest;
	u32 longestSector;
} Stats;

static const char *const names[IOGUARD_KINDS] = { "USB read", "USB write", "disc read" };
static Request requests[SLOTS];
static Stats stats[IOGUARD_KINDS];
static lwp_t watchThread = LWP_THREAD_NULL;

static void *Watch(void *arg)
{
	(void) arg;
	while (true)
	{
		usleep(1000 * 1000);
		u64 now = gettime();
		for (int i = 0; i < SLOTS; ++i)
		{
			Request r = requests[i];
			if (!r.start || ticks_to_secs(diff_ticks(r.start, now)) < IOGUARD_LIMIT_S)
				continue;
			//! stdout reaches the agent's kept log without IOS; gprintf() may write to SD
			printf("IoGuard: %s of %u sectors at %u has not returned in %u s: leaving GX\n",
				   names[r.kind], r.count, r.sector, IOGUARD_LIMIT_S);
			fflush(stdout);
			//! _now: exit()'s clean-up would unmount USB through the stuck cIOS
			hbc_agent_fatal_now(0x494f4755, "%s at sector %u stuck %u s", names[r.kind], r.sector,
								IOGUARD_LIMIT_S);
		}
	}
	return NULL;
}

void IoGuard_Start(void)
{
	if (watchThread != LWP_THREAD_NULL)
		return;
	//! 64 KiB: the agent's way out may print on the TV from this thread
	LWP_CreateThread(&watchThread, Watch, NULL, NULL, 65536, LWP_PRIO_HIGHEST);
}

int IoGuard_Begin(int kind, u32 sector, u32 count, u32 bytes)
{
	int token = -1;
	u32 level = IRQ_Disable();
	for (int i = 0; i < SLOTS; ++i)
	{
		if (requests[i].start)
			continue;
		requests[i].kind = kind;
		requests[i].sector = sector;
		requests[i].count = count;
		requests[i].start = gettime();
		token = i;
		break;
	}
	stats[kind].bytes += bytes;
	IRQ_Restore(level);
	return token;
}

void IoGuard_End(int token)
{
	if (token < 0 || token >= SLOTS)
		return;

	u32 level = IRQ_Disable();
	Request r = requests[token];
	requests[token].start = 0;
	u64 ticks = diff_ticks(r.start, gettime());
	Stats *s = &stats[r.kind];
	s->requests++;
	s->ticks += ticks;
	if (ticks > s->longest)
	{
		s->longest = ticks;
		s->longestSector = r.sector;
	}
	bool slow = ticks_to_millisecs(ticks) >= IOGUARD_SLOW_MS;
	if (slow)
		s->slow++;
	IRQ_Restore(level);

	if (slow)
		gprintf("IoGuard: slow %s of %u sectors at %u: %u ms\n", names[r.kind], r.count, r.sector,
				(u32) ticks_to_millisecs(ticks));
}

void IoGuard_Report(const char *where)
{
	for (int k = 0; k < IOGUARD_KINDS; ++k)
	{
		Stats s = stats[k];
		if (!s.requests)
			continue;
		u32 ms = ticks_to_millisecs(s.ticks);
		u32 kbps = ms ? (u32) (s.bytes * 1000 / 1024 / ms) : 0;
		gprintf("IoGuard %s: %s %u requests, %llu bytes, %u ms busy (%u KiB/s), longest %u ms at %u, %u over %u ms\n",
				where, names[k], s.requests, s.bytes, ms, kbps, (u32) ticks_to_millisecs(s.longest),
				s.longestSector, s.slow, IOGUARD_SLOW_MS);
	}
}

void IoGuard_ResetStats(void)
{
	u32 level = IRQ_Disable();
	memset(stats, 0, sizeof(stats));
	IRQ_Restore(level);
}
