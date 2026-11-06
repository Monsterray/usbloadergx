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

bool IsDeletableDirPath(const char *path)
{
	//! RemoveDirectory() deletes everything under the path it is given. Some of
	//! those paths come from the config file or the folder browser (the game
	//! header cache, the cover folder), so refuse the ones that can never name
	//! one folder of ours: nothing at all, a device root ("sd:", "sd:/", "/")
	//! and any path with a "." or ".." component.
	if (!path)
		return false;

	const char *rest = strchr(path, ':');
	rest = rest ? rest + 1 : path;

	bool named = false;
	for (const char *p = rest; *p != '\0'; )
	{
		while (*p == '/')
			p++;
		if (*p == '\0')
			break;

		size_t len = 0;
		while (p[len] != '\0' && p[len] != '/')
			len++;

		if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.'))
			return false;

		named = true;
		p += len;
	}

	return named;
}
