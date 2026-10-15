// 2 MEM2 allocators, one for general purpose, one for covers
// Aligned and padded to 32 bytes, as required by many functions

#ifndef __MEM2_H_
#define __MEM2_H_

#ifdef __cplusplus
extern "C" {
#endif

#define isMEM2Buffer(p) (((u32) p & 0x10000000) != 0)

void *MEM1_alloc(unsigned int s);
void *MEM1_memalign(unsigned int a, unsigned int s);
void *MEM1_realloc(void *p, unsigned int s);
void MEM1_free(void *p);

void MEM2_init(void);
void MEM2_cleanup(void);
void MEM2_takeBigOnes(bool b);
void *MEM2_alloc(unsigned int s);
void *MEM2_realloc(void *p, unsigned int s);
void MEM2_free(void *p);
unsigned int MEM2_usableSize(void *p);
unsigned int MEM2_freesize();
unsigned int MEM2_largestfree();
void MEM_Report(const char *where);

//! The MEM1 texture pool (mem2.cpp): MEM1 that only boot paths write, and only after
//! AppCleanUp(). TEX_init() opens it once start-up is done; GuiTexturePoolClose()
//! closes it for good at the top of AppCleanUp(). free(), realloc() and
//! malloc_usable_size() route a pool pointer by address, also after the close.
void TEX_init(void);
void TEX_close(void);
//! 32-byte aligned pixels for GX: the pool, else memalign(32, s). Texture data only:
//! never a buffer IOS, a device or a boot path uses.
void *TEX_alloc(unsigned int s);
//! Moves a texture buffer of s bytes into the pool when it fits there and frees the
//! old one. Returns the buffer to use from now on.
void *TEX_adopt(void *p, unsigned int s);

#ifdef __cplusplus
}
#endif

#endif // !defined(__MEM2_H_)
