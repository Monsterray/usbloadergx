#include "GUI/gui.h"
#include "usbloader/disc.h"
#include "FileOperations/fileops.h"
#include "settings/CSettings.h"
#include "themes/CTheme.h"
#include "ImageOperations/TextureConverter.h"
#include "homebrewboot/HomebrewApps.h"

//! A homebrew app's icon.png (128x48 by HBC's rules) on a transparent card the size of
//! the no-cover image, scaled to the card's width: stretched to a box it would distort.
static GuiImageData *HomebrewCover(struct discHdr *header, bool Prefere3D)
{
	std::string path = HomebrewApps::Folder(header) + "icon.png";
	u8 *png = NULL;
	u32 pngSize = 0;
	if (LoadFileToMem(path.c_str(), &png, &pngSize) < 0 || !png)
		return NULL;
	gdImagePtr icon = gdImageCreateFromPngPtr(pngSize, png);
	free(png);
	if (!icon)
		return NULL;

	const int w = Prefere3D ? 176 : 160, h = Prefere3D ? 248 : 224;
	int iw = gdImageSX(icon), ih = gdImageSY(icon);
	int dw = w - 8, dh = iw > 0 ? ih * dw / iw : 0;
	if (dh > h - 8)
	{
		dh = h - 8;
		dw = ih > 0 ? iw * dh / ih : 0;
	}

	GuiImageData *cover = NULL;
	gdImagePtr card = (dw > 0 && dh > 0) ? gdImageCreateTrueColor(w, h) : NULL;
	if (card)
	{
		gdImageAlphaBlending(card, 0);
		gdImageSaveAlpha(card, 1);
		gdImageFilledRectangle(card, 0, 0, w - 1, h - 1, gdTrueColorAlpha(0, 0, 0, gdAlphaTransparent));
		gdImageAlphaBlending(card, 1);
		gdImageCopyResampled(card, icon, (w - dw) / 2, (h - dh) / 2, 0, 0, dw, dh, iw, ih);
		gdImageAlphaBlending(card, 0);

		int outSize = 0;
		void *out = gdImagePngPtr(card, &outSize);
		if (out && outSize > 0)
			cover = new (std::nothrow) GuiImageData((const u8 *)out, outSize, false);
		gdFree(out);
		gdImageDestroy(card);
	}
	gdImageDestroy(icon);

	if (cover && !cover->GetImage())
	{
		delete cover;
		cover = NULL;
	}
	return cover;
}

/****************************************************************************
 * LoadCoverImage
 ***************************************************************************/
GuiImageData *LoadCoverImage(struct discHdr *header, bool Prefere3D, bool noCover)
{
	if (!header)
		return NULL;
	if (header->type == TYPE_GAME_HOMEBREW)
	{
		GuiImageData *cover = HomebrewCover(header, Prefere3D);
		if (cover || !noCover)
			return cover;
		return Resources::GetImageData(Prefere3D ? "nocover.png" : "nocoverFlat.png");
	}
	GuiImageData *Cover = NULL;
	char ID3[4] = {};
	char IDfull[7];
	char Path[255];
	bool flag = Prefere3D;

	memcpy(ID3, (char *)header->id, sizeof(ID3) - 1);
	snprintf(IDfull, sizeof(IDfull), "%.6s", (char *)header->id);

	for (int i = 0; i < 2; ++i)
	{
		char *coverPath = flag ? Settings.covers_path : Settings.covers2d_path;
		flag = !flag;
		//Load full id image
		snprintf(Path, sizeof(Path), "%s%s.png", coverPath, IDfull);

		if (!CheckFile(Path))
		{
			snprintf(Path, sizeof(Path), "%s%s.png", coverPath, ID3);
			if (!CheckFile(Path))
				continue;
		}

		delete Cover;
		Cover = new (std::nothrow) GuiImageData(Path);
		//Load short id image
		if (!Cover || !Cover->GetImage())
		{
			snprintf(Path, sizeof(Path), "%s%s.png", coverPath, ID3);
			delete Cover;
			Cover = new (std::nothrow) GuiImageData(Path);
		}
		if (Cover && Cover->GetImage())
			break;
	}
	//Load no image
	if (noCover && (!Cover || !Cover->GetImage()))
	{
		flag = Prefere3D;
		for (int i = 0; i < 2; ++i)
		{
			delete Cover;
			Cover = Resources::GetImageData(flag ? "nocover.png" : "nocoverFlat.png");
			if (Cover && Cover->GetImage())
				break;
			flag = !flag;
		}
	}
	if (Cover && !Cover->GetImage())
	{
		delete Cover;
		Cover = NULL;
	}
	return Cover;
}
