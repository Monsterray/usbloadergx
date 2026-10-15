#include "mem2.h"
#include "mem2alloc.hpp"

#include <malloc.h>
#include <string.h>
#include <ogc/system.h>
#define HBC_AGENT_LAYOUT_ONLY
#define HBC_NETLOG_LAYOUT_ONLY
#include <hbc_agent.h>
#include <hbc_netlog.h>
#include "gecko.h"

#define MAX_MEM1_ARENA_LO	((void *) (0x81700000-size))
#define MEM2_PRIORITY_SIZE	30720	// 30KB

//! MEM2 GX does not take for its heap:
//! - below 0x90200000: an IOS reload uses it
//! - the hbc agent's netlog copy, crash and last-log records, which `hbc.py log`
//!   and the agent write while GX runs (HBC Reborn 1.10: 0x91800000-0x91801140)
//! - LIBOGC_ARENA2_RESERVE under the top of the SYS arena: libogc takes it after
//!   MEM2_init(), from SYS_GetArena2Hi() down. Measured in Dolphin, 233 KB by the
//!   main menu: the Bluetooth buffers for each WPAD_Init() (85 KB, twice at start-up)
//!   and the 64 KB network heap. The bench Wii's mem2_free of 1.7 MB above the old
//!   48 MiB heap agrees.
#define IOS_RELOAD_AREA			0x90200000
#define HBC_RECORDS_START		HBC_NETLOG_KEEP_ADDR
#define HBC_RECORDS_END			((HBC_LASTLOG_ADDR + sizeof(hbc_lastlog_block) + 31) & ~31)
#define LIBOGC_ARENA2_RESERVE	(768 * 1024)

// Forbid the use of MEM2 through malloc
u32 MALLOC_MEM2 = 0;

//! The heap is two pools, one on each side of the hbc records. An allocation never
//! spans the records; the lower pool is tried first.
static CMEM2Alloc g_mem2lo;
static CMEM2Alloc g_mem2hi;
static void *g_arena2Lo = 0;
static void *g_poolEnd = 0;

static bool g_bigGoesToMem2 = true;

//! EXTRA_CFLAGS=-DDEBUG_MEM logs every allocation of 256 KB or more and its caller
#ifdef DEBUG_MEM
#define TRACE_BIG(kind, size, p) do { if ((size) >= 256 * 1024) \
	gprintf("mem %s %u -> %p from %p\n", kind, (unsigned) (size), p, __builtin_return_address(0)); } while (0)
#else
#define TRACE_BIG(kind, size, p) do { } while (0)
#endif

static void *mem2_allocate(unsigned int s)
{
	void *p = g_mem2lo.allocate(s);
	return p ? p : g_mem2hi.allocate(s);
}

//! The pool a pointer came from, or NULL for MEM2 GX's heap does not own
static CMEM2Alloc *mem2_owner(void *p)
{
	if (g_mem2lo.Owns(p))
		return &g_mem2lo;
	if (g_mem2hi.Owns(p))
		return &g_mem2hi;
	return 0;
}

static void mem2_release(void *p)
{
	CMEM2Alloc *pool = mem2_owner(p);
	if (pool)
		pool->release(p);
	else if (p)
		gprintf("mem: free of %p, which no MEM2 pool owns\n", p);
}

static unsigned int mem2_usable(void *p)
{
	CMEM2Alloc *pool = mem2_owner(p);
	return pool ? pool->usableSize(p) : 0;
}

static void *mem2_reallocate(void *p, unsigned int s)
{
	CMEM2Alloc *pool = mem2_owner(p);
	if (!pool)
		return p ? 0 : mem2_allocate(s);

	void *n = pool->reallocate(p, s);
	if (n)
		return n;

	//! No room in its own pool: move it to the other one
	CMEM2Alloc *other = (pool == &g_mem2lo) ? &g_mem2hi : &g_mem2lo;
	n = other->allocate(s);
	if (!n)
		return 0;
	unsigned int old = pool->usableSize(p);
	memcpy(n, p, old < s ? old : s);
	pool->release(p);
	return n;
}

extern "C"
{

extern __typeof(malloc) __real_malloc;
extern __typeof(calloc) __real_calloc;
extern __typeof(realloc) __real_realloc;
extern __typeof(memalign) __real_memalign;
extern __typeof(free) __real_free;
extern __typeof(malloc_usable_size) __real_malloc_usable_size;


void MEM2_takeBigOnes(bool b)
{
	g_bigGoesToMem2 = b;
}

//! Take all of the SYS MEM2 arena that libogc does not need later
void MEM2_init(void)
{
	u32 lo = ((u32) SYS_GetArena2Lo() + 31) & ~31;
	u32 hi = ((u32) SYS_GetArena2Hi() - LIBOGC_ARENA2_RESERVE) & ~31;
	if (lo < IOS_RELOAD_AREA)
		lo = IOS_RELOAD_AREA;
	if (hi <= lo)
		return;

	g_arena2Lo = SYS_GetArena2Lo();
	g_poolEnd = (void *) hi;
	if (hi <= HBC_RECORDS_START || lo >= HBC_RECORDS_END)
		g_mem2lo.init((void *) lo, (void *) hi);
	else
	{
		g_mem2lo.init((void *) lo, (void *) HBC_RECORDS_START);
		g_mem2hi.init((void *) HBC_RECORDS_END, (void *) hi);
	}
	SYS_SetArena2Lo((void *) hi);
}

void MEM2_cleanup(void)
{
	g_mem2lo.cleanup();
	g_mem2hi.cleanup();
	// Give the arena back if nothing took the MEM2 above it meanwhile
	if (g_arena2Lo && SYS_GetArena2Lo() == g_poolEnd)
		SYS_SetArena2Lo(g_arena2Lo);
	g_arena2Lo = g_poolEnd = 0;
}

void *MEM2_alloc(unsigned int s)
{
	void *r = mem2_allocate(s);
	TRACE_BIG("MEM2_alloc", s, r);
	return r;
}

void MEM2_free(void *p)
{
	mem2_release(p);
}

void *MEM2_realloc(void *p, unsigned int s)
{
	void *r = mem2_reallocate(p, s);
	TRACE_BIG("MEM2_realloc", s, r);
	return r;
}

unsigned int MEM2_usableSize(void *p)
{
	return mem2_usable(p);
}

unsigned int MEM2_freesize()
{
	return g_mem2lo.FreeSize() + g_mem2hi.FreeSize();
}

unsigned int MEM2_largestfree()
{
	unsigned int lo = g_mem2lo.LargestFree();
	unsigned int hi = g_mem2hi.LargestFree();
	return lo > hi ? lo : hi;
}

//! One line with where every bank stands. The hbc agent's mem1_free and mem2_free
//! are the SYS arenas nobody has claimed yet, not what GX can still allocate.
void MEM_Report(const char *where)
{
	struct mallinfo mi = mallinfo();
	void *loBase, *hiBase;
	unsigned int loSize, hiSize;
	g_mem2lo.info(loBase, loSize);
	g_mem2hi.info(hiBase, hiSize);
	gprintf("mem %s: arena1 %p-%p heap %u used %u free %u | pool %p+%u %p+%u free %u largest %u | arena2 %p-%p\n",
			where, SYS_GetArena1Lo(), SYS_GetArena1Hi(), mi.arena, mi.uordblks, mi.fordblks,
			loBase, loSize, hiBase, hiSize, MEM2_freesize(), MEM2_largestfree(),
			SYS_GetArena2Lo(), SYS_GetArena2Hi());
}

void *MEM1_alloc(unsigned int s)
{
	return __real_malloc(s);
}

void *MEM1_memalign(unsigned int a, unsigned int s)
{
	return __real_memalign(a, s);
}

void MEM1_free(void *p)
{
	__real_free(p);
}

void *MEM1_realloc(void *p, unsigned int s)
{
	return __real_realloc(p, s);
}

static void *malloc_impl(size_t size)
{
	void *p;
	if ((SYS_GetArena1Lo() > MAX_MEM1_ARENA_LO) || (g_bigGoesToMem2 && size > MEM2_PRIORITY_SIZE))
	{
		p = mem2_allocate(size);
		if (p != 0) {
			return p;
		}
		return __real_malloc(size);
	}
	p = __real_malloc(size);
	if (p != 0) {
		return p;
	}
	return mem2_allocate(size);
}

void *__wrap_malloc(size_t size)
{
	void *r = malloc_impl(size);
	TRACE_BIG("malloc", size, r);
	return r;
}

static void *calloc_impl(size_t n, size_t size)
{
	//! The product decides the bank, and must not wrap
	if (size && n > (size_t) -1 / size)
		return 0;
	size_t total = n * size;

	void *p;
	if ((SYS_GetArena1Lo() > MAX_MEM1_ARENA_LO) || (g_bigGoesToMem2 && total > MEM2_PRIORITY_SIZE))
	{
		p = mem2_allocate(total);
		if (p != 0)
		{
			memset(p, 0, total);
			return p;
		}
		return __real_calloc(n, size);
	}
	p = __real_calloc(n, size);
	if (p != 0) {
		return p;
	}
	p = mem2_allocate(total);
	if (p != 0) {
		memset(p, 0, total);
	}
	return p;
}

void *__wrap_calloc(size_t n, size_t size)
{
	void *r = calloc_impl(n, size);
	TRACE_BIG("calloc", n * size, r);
	return r;
}

static void *memalign_impl(size_t a, size_t size)
{
	void *p;
	if ((SYS_GetArena1Lo() > MAX_MEM1_ARENA_LO) || (g_bigGoesToMem2 && size > MEM2_PRIORITY_SIZE))
	{
		if (a <= 32 && 32 % a == 0)
		{
			p = mem2_allocate(size);
			if (p != 0) {
				return p;
			}
		}
		return __real_memalign(a, size);
	}
	p = __real_memalign(a, size);
	if (p != 0 || a > 32 || 32 % a != 0) {
		return p;
	}

	return mem2_allocate(size);
}

void *__wrap_memalign(size_t a, size_t size)
{
	void *r = memalign_impl(a, size);
	TRACE_BIG("memalign", size, r);
	return r;
}

void __wrap_free(void *p)
{
	if(!p)
		return;

	if (((u32)p & 0x10000000) != 0)
	{
		mem2_release(p);
	}
	else
	{
		__real_free(p);
	}
}

static void *realloc_impl(void *p, size_t size)
{
	void *n;
	// ptr from mem2
	if (((u32)p & 0x10000000) != 0 || (p == 0 && g_bigGoesToMem2 && size > MEM2_PRIORITY_SIZE))
	{
		n = mem2_reallocate(p, size);
		if (n != 0) {
			return n;
		}
		n = __real_malloc(size);
		if (n == 0) {
			return 0;
		}
		if (p != 0)
		{
			unsigned int old = mem2_usable(p);
			memcpy(n, p, old < size ? old : size);
			mem2_release(p);
		}
		return n;
	}
	// ptr from malloc
	n = __real_realloc(p, size);
	if (n != 0) {
		return n;
	}
	n = mem2_allocate(size);
	if (n == 0) {
		return 0;
	}
	if (p != 0)
	{
		memcpy(n, p, __real_malloc_usable_size(p) < size ? __real_malloc_usable_size(p) : size);
		__real_free(p);
	}
	return n;
}

void *__wrap_realloc(void *p, size_t size)
{
	void *r = realloc_impl(p, size);
	TRACE_BIG("realloc", size, r);
	return r;
}

size_t __wrap_malloc_usable_size(void *p)
{
	if (((u32)p & 0x10000000) != 0)
		return mem2_usable(p);
	return __real_malloc_usable_size(p);
}

} ///extern "C"
