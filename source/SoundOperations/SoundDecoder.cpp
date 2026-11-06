/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
 *
 * 3Band resampling thanks to libmad
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
#include <gccore.h>
#include <unistd.h>
#include "SoundDecoder.hpp"
#include "settings/CSettings.h"

static const u32 FixedPointShift = 15;
static const u32 FixedPointScale = 1 << FixedPointShift;

SoundDecoder::SoundDecoder()
{
	file_fd = NULL;
	Init();
}

SoundDecoder::SoundDecoder(const char * filepath)
{
	file_fd = new CFile(filepath, "rb");
	Init();
}

SoundDecoder::SoundDecoder(const u8 * buffer, int size)
{
	file_fd = new CFile(buffer, size);
	Init();
}

SoundDecoder::~SoundDecoder()
{
	ExitRequested = true;
	while(Decoding)
		usleep(100);

	if(file_fd)
		delete file_fd;
	file_fd = NULL;
	if(ResampleBuffer)
		free(ResampleBuffer);
	ResampleBuffer = NULL;
}

void SoundDecoder::Init()
{
	SoundType = SOUND_RAW;
	SoundBlocks = 8;
	SoundBlockSize = 8192;
	ResampleTo48kHz = Settings.ResampleTo48kHz;
	CurPos = 0;
	Loop = false;
	EndOfFile = false;
	Decoding = false;
	ExitRequested = false;
	SoundBuffer.SetBufferBlockSize(SoundBlockSize);
	SoundBuffer.Resize(SoundBlocks);
	ResampleBuffer = NULL;
	ResampleRatio = 0;
	Format = VOICE_STEREO_16BIT;
	SampleRate = 48000;
	whichLoad = 0;
}

int SoundDecoder::Rewind()
{
	CurPos = 0;
	EndOfFile = false;
	if(file_fd)
		file_fd->rewind();

	return 0;
}

int SoundDecoder::Read(u8 * buffer, int buffer_size, int pos)
{
	int ret = file_fd->read(buffer, buffer_size);
	CurPos += ret;

	return ret;
}

void SoundDecoder::EnableUpsample(void)
{
	if(ResampleBuffer)
	{
		free(ResampleBuffer);
		ResampleBuffer = NULL;
	}
	if(IsStereo() && Is16Bit() && SampleRate != 32000 && SampleRate < 48000)
	{
		// A rate that gives a ratio of 0 or a block under 4 bytes would divide by
		// zero in Decode(); leave the stream as it is then.
		u32 ratio = ( FixedPointScale * SampleRate ) / 48000;
		int blockSize = ( SoundBlockSize * ratio ) / FixedPointScale;
		blockSize &= ~0x03;
		if(ratio == 0 || blockSize < 4)
			return;

		u8 *buf = (u8*)memalign(32, SoundBlockSize);
		if(!buf)
			return;

		ResampleBuffer = buf;
		ResampleRatio = ratio;
		SoundBlockSize = blockSize;
		// Set new sample rate
		SampleRate = 48000;
	}
}

void SoundDecoder::Upsample(s16 *src, s16 *dst, u32 nr_src_samples, u32 nr_dst_samples)
{
	int timer = 0;

	for(u32 i = 0, n = 0; i < nr_dst_samples; i += 2)
	{
		if((n+3) < nr_src_samples) {
			// Simple fixed point linear interpolation
			dst[i]   = src[n] +   ( ((src[n+2] - src[n]  ) * timer) >> FixedPointShift );
			dst[i+1] = src[n+1] + ( ((src[n+3] - src[n+1]) * timer) >> FixedPointShift );
		}
		else {
			dst[i]   = src[n];
			dst[i+1] = src[n+1];
		}

		timer += ResampleRatio;

		if(timer >= (int)FixedPointScale) {
			n += 2;
			timer -= FixedPointScale;
		}
	}
}

void SoundDecoder::Decode()
{
	if(!file_fd || ExitRequested || EndOfFile)
		return;

	u16 newWhich = SoundBuffer.Which();
	u16 i = 0;
	for (i = 0; i < SoundBuffer.Size()-2; i++)
	{
		if(!SoundBuffer.IsBufferReady(newWhich))
			break;

		newWhich = (newWhich+1) % SoundBuffer.Size();
	}

	if(i == SoundBuffer.Size()-2)
		return;

	Decoding = true;

	int done  = 0;
	u8 *write_buf = SoundBuffer.GetBuffer(newWhich);
	if(!write_buf)
	{
		ExitRequested = true;
		Decoding = false;
		return;
	}

	if(ResampleTo48kHz && !ResampleBuffer)
		EnableUpsample();

	bool rewound = false;
	while(done < SoundBlockSize)
	{
		int ret = Read(&write_buf[done], SoundBlockSize-done, Tell());

		if(ret <= 0)
		{
			// One rewind per empty read: an empty file must not loop forever
			if(Loop && !rewound)
			{
				Rewind();
				rewound = true;
				continue;
			}
			else
			{
				EndOfFile = true;
				break;
			}
		}

		rewound = false;
		done += ret;
	}

	if(done > 0)
	{
		// Check if we need to resample
		if(ResampleBuffer && ResampleRatio)
		{
			memcpy(ResampleBuffer, write_buf, done);

			int src_samples = done >> 1;
			int dest_samples = ( src_samples * FixedPointScale ) / ResampleRatio;
			dest_samples &= ~0x01;
			Upsample((s16*)ResampleBuffer, (s16*)write_buf, src_samples, dest_samples);
			done = dest_samples << 1;
		}
		//! A tail of under 4 bytes resamples to nothing: do not hand out an empty buffer
		if(done > 0)
		{
			SoundBuffer.SetBufferSize(newWhich, done);
			SoundBuffer.SetBufferReady(newWhich, true);
		}
	}

	// Only keep filling while data arrives: with nothing decoded this would recurse forever
	if(done > 0 && !SoundBuffer.IsBufferReady((newWhich+1) % SoundBuffer.Size()))
		Decode();

	Decoding = false;
}
