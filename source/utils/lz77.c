/*******************************************************************************
 * lz77.c
 *
 * Copyright (c) 2009 The Lemon Man
 * Copyright (c) 2009 Nicksasa
 * Copyright (c) 2009 WiiPower
 *
 * Distributed under the terms of the GNU General Public License (v2)
 * See http://www.gnu.org/licenses/gpl-2.0.txt for more info.
 *
 * Description:
 * -----------
 *
 * The input is a file from a disc, from NAND or from the user's card, so nothing
 * in it is trusted: both decompressors read only inside inputLen, write only
 * inside the output they allocated, and return -1 (leaving *output alone) for a
 * stream that is cut short, copies from before the start of the output or runs
 * past its declared size. tests/host/lz77_test.c checks this under ASan/UBSan.
 *
 ******************************************************************************/

#include <gctypes.h>
#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "lz77.h"
#include "tools.h"

// the most output one input byte can stand for: a 2 byte copy gives 18 bytes
// (0x10, flag byte included: under 9), a 4 byte copy 0x10111 (0x11)
#define LZ77_10_MAX_RATIO 9u
#define LZ77_11_MAX_RATIO 0x4100u

static inline u32 packBytes(u32 a, u32 b, u32 c, u32 d)
{
	return (d << 24) | (c << 16) | (b << 8) | (a);
}

// A declared size is refused when the input could not make that much output (so a
// 4 byte file cannot ask for gigabytes) and when ALIGN32() of it would wrap.
static int sizeIsPlausible(u32 inputLen, u32 size, u32 maxRatio)
{
	return size <= 0xFFFFFFE0u && (u64) size <= (u64) inputLen * maxRatio;
}

s32 __decompressLZ77_11( const u8 *in, u32 inputLen, u8 **output, u32 *outputLen)
{
	int x;

	u8 *out = NULL;

	u32 compressedPos = 0x4;
	u32 decompressedPos = 0x0;
	u32 decompressedSize = 0;

	if (inputLen < 4)
		return -1;

	decompressedSize = packBytes(in[0], in[1], in[2], in[3]) >> 8;

	if (!decompressedSize)
	{
		if (inputLen < 8)
			return -1;

		decompressedSize = packBytes(in[4], in[5], in[6], in[7]);
		compressedPos += 0x4;
	}

	if (!sizeIsPlausible(inputLen, decompressedSize, LZ77_11_MAX_RATIO))
		return -1;

	out = memalign(32, ALIGN32(decompressedSize));
	if (out == NULL)
		return -1;

	while (compressedPos < inputLen && decompressedPos < decompressedSize)
	{
		u8 byteFlag = in[compressedPos];
		compressedPos++;

		for (x = 7; x >= 0; x--)
		{
			// the flag byte can be the last byte, and then no item is left for it
			if (compressedPos >= inputLen)
				goto bad;

			if ((byteFlag & (1 << x)) > 0)
			{
				u8 first = in[compressedPos];
				u32 left = inputLen - compressedPos;

				u32 pos, copyLen;

				if (first < 0x20)
				{
					if (first >= 0x10)
					{
						if (left < 4)
							goto bad;

						u32 second = in[compressedPos + 1];
						u32 third = in[compressedPos + 2];
						u32 fourth = in[compressedPos + 3];

						pos = (u32)(((third & 0xF) << 8) | fourth) + 1;
						copyLen = (u32)((second << 4) | ((first & 0xF) << 12) | (third >> 4)) + 273;

						compressedPos += 4;
					} else
					{
						if (left < 3)
							goto bad;

						u32 second = in[compressedPos + 1];
						u32 third = in[compressedPos + 2];

						pos = (u32)(((second & 0xF) << 8) | third) + 1;
						copyLen = (u32)(((first & 0xF) << 4) | (second >> 4)) + 17;

						compressedPos += 3;
					}
				} else
				{
					if (left < 2)
						goto bad;

					u32 second = in[compressedPos + 1];

					pos = (u32)(((first & 0xF) << 8) | second) + 1;
					copyLen = (u32)(first >> 4) + 1;

					compressedPos += 2;
				}

				// a copy reads only what is already written and writes only inside the output
				if (pos > decompressedPos || copyLen > decompressedSize - decompressedPos)
					goto bad;

				for (u32 y = 0; y < copyLen; y++)
				{
					out[decompressedPos + y] = out[decompressedPos - pos + y];
				}

				decompressedPos += copyLen;
			} else
			{
				out[decompressedPos] = in[compressedPos];

				decompressedPos++;
				compressedPos++;
			}

			if (compressedPos >= inputLen || decompressedPos >= decompressedSize)
				break;
		}
	}

	// the input ended before the declared size was reached
	if (decompressedPos != decompressedSize)
		goto bad;

	*output = out;
	*outputLen = decompressedSize;
	return 0;

bad:
	free(out);
	return -1;
}

s32 __decompressLZ77_10( const u8 *in, u32 inputLen, u8 **output, u32 *outputLen)
{
	int x;

	u8 *out = NULL;

	u32 compressedPos = 0;
	u32 decompressedSize = 0x4;
	u32 decompressedPos = 0;

	if (inputLen < 4)
		return -1;

	decompressedSize = packBytes(in[0], in[1], in[2], in[3]) >> 8;

	if (!sizeIsPlausible(inputLen, decompressedSize, LZ77_10_MAX_RATIO))
		return -1;

	out = memalign(32, ALIGN32(decompressedSize));
	if (out == NULL)
		return -1;

	compressedPos += 0x4;

	while (decompressedPos < decompressedSize)
	{
		// the flag byte of a block, and then no input is left for it
		if (compressedPos >= inputLen)
			goto bad;

		u8 flag = *(u8*)(in + compressedPos);
		compressedPos += 1;

		for (x = 0; x < 8; x++)
		{
			if (flag & 0x80)
			{
				if (inputLen - compressedPos < 2)
					goto bad;

				u8 first = in[compressedPos];
				u8 second = in[compressedPos + 1];

				u32 pos = (u32)((((first << 8) + second) & 0xFFF) + 1);
				u32 copyLen = (u32)(3 + ((first >> 4) & 0xF));

				// a copy reads only what is already written and writes only inside the output
				if (pos > decompressedPos || copyLen > decompressedSize - decompressedPos)
					goto bad;

				for (u32 y = 0; y < copyLen; y++)
				{
					out[decompressedPos + y] = out[decompressedPos - pos + (y % pos)];
				}

				compressedPos += 2;
				decompressedPos += copyLen;
			} else
			{
				if (compressedPos >= inputLen)
					goto bad;

				out[decompressedPos] = in[compressedPos];
				compressedPos += 1;
				decompressedPos += 1;
			}

			flag <<= 1;

			if (decompressedPos >= decompressedSize)
				break;
		}
	}

	*output = out;
	*outputLen = decompressedSize;
	return 0;

bad:
	free(out);
	return -1;
}

int isLZ77compressed( const u8 *buffer)
{
	if ((buffer[0] == LZ77_0x10_FLAG) || (buffer[0] == LZ77_0x11_FLAG))
		return 1;

	return 0;
}

int decompressLZ77content(const u8 *buffer, u32 length, u8 **output, u32 *outputLen)
{
	int ret;

	if (length < 4)
		return -1;

	switch (buffer[0])
	{
		case LZ77_0x10_FLAG:
			ret = __decompressLZ77_10(buffer, length, output, outputLen);
			break;
		case LZ77_0x11_FLAG:
			ret = __decompressLZ77_11(buffer, length, output, outputLen);
			break;
		default:
			ret = -1;
			break;
	}
	return ret;
}
