/* ogc/gx.h for the host: the GXColor type and nothing else. themes/gettheme.c includes
 * it only for the colour a theme string is parsed into. */
#ifndef HOST_SHIM_OGC_GX_H
#define HOST_SHIM_OGC_GX_H

#include <gctypes.h>

typedef struct _gx_color
{
	u8 r, g, b, a;
} GXColor;

#endif
