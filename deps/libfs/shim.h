/* libogc 3 dropped libogc2's BOOL and friends; libntfs-wii uses them. */
#ifndef GX_FSLIBS_SHIM_H
#define GX_FSLIBS_SHIM_H
#include <gctypes.h>
#ifndef BOOL
typedef unsigned int BOOL;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef makedev
#define makedev(ma, mi) (((ma) << 8) | (mi))
#endif
#endif
