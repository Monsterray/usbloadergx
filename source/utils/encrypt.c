/***************************************************************************
 * Copyright (C) 2010
 * by dude, Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * for WiiXplorer 2010
 ***************************************************************************/
#include <stddef.h>
#include "encrypt.h"

//! No need for high security crap. It's a simple encrypter/decrypter
//! with a constant sid.
const char * sid = "USBLoaderGX";

//! Writes two hex digits per source character and a terminator: at most
//! (dstsize - 1) / 2 characters of src fit, the rest is dropped.
void EncryptString(const char *src, char *dst, size_t dstsize)
{
	static const char hex[] = "0123456789abcdef";
	size_t i, n = 0;

	if (dstsize == 0)
		return;

	for (i = 0; src[i] != 0 && n + 3 <= dstsize; i++)
	{
		unsigned char c = (unsigned char) src[i] ^ (unsigned char) sid[i%10];
		dst[n++] = hex[c >> 4];
		dst[n++] = hex[c & 15];
	}
	dst[n] = 0;
}

static int HexDigit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

//! src comes from a file on the card. Writes at most dstsize - 1 characters and a
//! terminator, and stops at the first pair that is not two hex digits.
void DecryptString(const char *src, char *dst, size_t dstsize)
{
	size_t i = 0, n = 0;

	if (dstsize == 0)
		return;

	while (n + 1 < dstsize)
	{
		int hi = HexDigit(src[i]);
		int lo = hi < 0 ? -1 : HexDigit(src[i+1]);
		if (hi < 0 || lo < 0)
			break;
		dst[n] = (char) (((hi << 4) | lo) ^ (unsigned char) sid[n%10]);
		n++;
		i += 2;
	}
	dst[n] = 0;
}

