/* ogcsys.h for settings_test: the few libogc names CSettings.cpp uses, with the
 * video, aspect and language the test pins (NTSC, 4:3, English). */
#ifndef HOST_SHIM_OGCSYS_H
#define HOST_SHIM_OGCSYS_H

#include <string.h>
#include <stddef.h>
#include <gctypes.h>

typedef float f32;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#define CONF_VIDEO_NTSC 0
#define CONF_VIDEO_PAL 1
#define CONF_ASPECT_4_3 0
#define CONF_ASPECT_16_9 1

static inline int CONF_Init(void) { return 0; }
static inline int CONF_GetVideo(void) { return CONF_VIDEO_NTSC; }
static inline int CONF_GetEuRGB60(void) { return 0; }
static inline int CONF_GetAspectRatio(void) { return CONF_ASPECT_4_3; }
static inline int CONF_GetLanguage(void) { return 1; }

/* glibc has strlcpy from 2.38 on; the Wii's newlib always had it. */
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 38)
static inline size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t n = strlen(src);
	if (size)
	{
		size_t c = n >= size ? size - 1 : n;
		memcpy(dst, src, c);
		dst[c] = 0;
	}
	return n;
}
#endif

#endif
