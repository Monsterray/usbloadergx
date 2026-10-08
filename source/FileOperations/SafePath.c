/****************************************************************************
 * SafePath.c
 *
 * Plain C with no libogc in it, so tests/host/ compiles this exact file on
 * the PC and tests the code the Wii runs.
 ***************************************************************************/
#include <string.h>
#include "FileOperations/fileops.h"

bool IsSafeRelativePath(const char *name)
{
	//! A zip entry name and a wiiload filename are both chosen by whoever sent
	//! them. Such a name may not start at the root, name a device, or step out
	//! of the directory it is joined to.
	if (!name || name[0] == '\0' || name[0] == '/' || name[0] == '\\')
		return false;

	if (strchr(name, ':') != NULL)
		return false;

	for (const char *part = name; part != NULL; )
	{
		if (part[0] == '.' && part[1] == '.' &&
			(part[2] == '/' || part[2] == '\\' || part[2] == '\0'))
			return false;

		//! Advance to whichever separator comes first
		const char *slash = strchr(part, '/');
		const char *backslash = strchr(part, '\\');
		if (!slash || (backslash && backslash < slash))
			slash = backslash;

		part = slash ? slash + 1 : NULL;
	}

	return true;
}
