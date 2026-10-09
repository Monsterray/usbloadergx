/****************************************************************************
 * WadInstall.cpp
 *
 * ES wants each blob 32-byte aligned and the certificate chain with every
 * call, so everything is copied out of the WAD into aligned buffers first.
 * Contents go in 16 KB pieces. The order is the one IOS requires:
 * AddTicket, AddTitleStart, AddContentStart/Data/Finish per content in TMD
 * order, AddTitleFinish.
 ***************************************************************************/
#include <ogcsys.h>
#include <malloc.h>
#include <string.h>

#include "WadInstall.h"
#include "WadLayout.h"
#include "prompts/ProgressWindow.h"
#include "gecko.h"

#define CONTENT_CHUNK (16 * 1024)

// A 32-byte aligned copy of a section of the WAD, or NULL. An empty section
// (the CRL usually is) gives NULL too, which ES takes as "none".
static signed_blob *AlignedCopy(const u8 *wad, const WadSection &section)
{
	if (section.size == 0)
		return NULL;
	signed_blob *copy = (signed_blob *)memalign(32, (section.size + 31) & ~31u);
	if (copy)
		memcpy(copy, wad + section.offset, section.size);
	return copy;
}

int WadInstall(const u8 *wad, u32 size, u64 expectedTitle)
{
	WadLayout layout;
	if (!WadLayout_Parse(wad, size, &layout))
		return WAD_INSTALL_BAD_LAYOUT;

	// Never a system title: a bad IOS or System Menu install bricks the console
	if (layout.titleId != expectedTitle || (layout.titleId >> 32) != 0x00010001)
	{
		gprintf("WadInstall: title %016llx, expected %016llx\n", layout.titleId, expectedTitle);
		return WAD_INSTALL_WRONG_TITLE;
	}

	signed_blob *certs = AlignedCopy(wad, layout.certs);
	signed_blob *crl = AlignedCopy(wad, layout.crl);
	signed_blob *tik = AlignedCopy(wad, layout.tik);
	signed_blob *tmd = AlignedCopy(wad, layout.tmd);
	u8 *chunk = (u8 *)memalign(32, CONTENT_CHUNK);
	bool started = false;
	int ret;

	if (!certs || !tik || !tmd || !chunk || (layout.crl.size && !crl))
	{
		ret = WAD_INSTALL_NO_MEMORY;
		goto out;
	}

	ret = ES_AddTicket(tik, layout.tik.size, certs, layout.certs.size, crl, layout.crl.size);
	if (ret < 0)
	{
		gprintf("WadInstall: ES_AddTicket %d\n", ret);
		goto out;
	}

	ret = ES_AddTitleStart(tmd, layout.tmd.size, certs, layout.certs.size, crl, layout.crl.size);
	if (ret < 0)
	{
		gprintf("WadInstall: ES_AddTitleStart %d\n", ret);
		goto out;
	}
	started = true;

	for (u16 i = 0; i < layout.numContents; i++)
	{
		WadContent content;
		if (!WadLayout_Content(wad, &layout, i, &content))
		{
			ret = WAD_INSTALL_BAD_LAYOUT;
			goto out;
		}

		s32 cfd = ES_AddContentStart(layout.titleId, content.cid);
		if (cfd < 0)
		{
			gprintf("WadInstall: ES_AddContentStart %08x: %d\n", content.cid, cfd);
			ret = cfd;
			goto out;
		}

		for (u32 done = 0; done < content.length;)
		{
			u32 len = content.length - done;
			if (len > CONTENT_CHUNK)
				len = CONTENT_CHUNK;
			memcpy(chunk, wad + content.offset + done, len);
			ret = ES_AddContentData(cfd, chunk, len);
			if (ret < 0)
			{
				gprintf("WadInstall: ES_AddContentData %08x: %d\n", content.cid, ret);
				goto out;
			}
			done += len;
			ShowProgress(content.offset - layout.data.offset + done, layout.data.size);
		}

		ret = ES_AddContentFinish(cfd);
		if (ret < 0)
		{
			gprintf("WadInstall: ES_AddContentFinish %08x: %d\n", content.cid, ret);
			goto out;
		}
	}

	ret = ES_AddTitleFinish();
	if (ret < 0)
		gprintf("WadInstall: ES_AddTitleFinish %d\n", ret);
	else
		started = false;

out:
	if (started)
		ES_AddTitleCancel();
	free(chunk);
	free(tmd);
	free(tik);
	free(crl);
	free(certs);
	return ret < 0 ? ret : 0;
}
