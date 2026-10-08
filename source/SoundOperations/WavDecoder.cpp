/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
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
#include <string.h>
#include "WavDecoder.hpp"
#include "utils/uncompress.h"

WavDecoder::WavDecoder(const char * filepath)
	: SoundDecoder(filepath)
{
	SoundType = SOUND_WAV;
	SampleRate = 48000;
	Format = VOICE_STEREO_16BIT;

	if(!file_fd)
		return;

	OpenFile();
}

WavDecoder::WavDecoder(const u8 * snd, int len)
	: SoundDecoder(snd, len)
{
	SoundType = SOUND_WAV;
	SampleRate = 48000;
	Format = VOICE_STEREO_16BIT;

	if(!file_fd)
		return;

	OpenFile();
}

WavDecoder::~WavDecoder()
{
}

void WavDecoder::OpenFile()
{
	SWaveHdr Header;
	SWaveFmtChunk FmtChunk;
	memset(&Header, 0, sizeof(SWaveHdr));
	memset(&FmtChunk, 0, sizeof(SWaveFmtChunk));

	file_fd->read((u8 *) &Header, sizeof(SWaveHdr));
	file_fd->read((u8 *) &FmtChunk, sizeof(SWaveFmtChunk));

	if (Header.magicRIFF != 'RIFF')
	{
		CloseFile();
		return;
	}
	else if(Header.magicWAVE != 'WAVE')
	{
		CloseFile();
		return;
	}
	else if(FmtChunk.magicFMT != 'fmt ')
	{
		CloseFile();
		return;
	}

	//! Every chunk size below comes out of the file. Walk forward only, and stop
	//! at the end of the file, or a wrapped size makes this loop run forever.
	u32 FileSize = file_fd->size();

	DataOffset = sizeof(SWaveHdr)+le32(FmtChunk.size)+8;
	SWaveChunk DataChunk;
	memset(&DataChunk, 0, sizeof(SWaveChunk));

	while(1)
	{
		if(DataOffset + sizeof(SWaveChunk) > FileSize)
		{
			CloseFile();
			return;
		}

		file_fd->seek(DataOffset, SEEK_SET);
		if(file_fd->read((u8 *) &DataChunk, sizeof(SWaveChunk)) <= 0)
		{
			CloseFile();
			return;
		}

		if(DataChunk.magicDATA == 'data')
			break;

		u32 NextOffset = DataOffset + 8 + le32(DataChunk.size);
		if(NextOffset <= DataOffset)
		{
			CloseFile();
			return;
		}

		DataOffset = NextOffset;
	}

	DataOffset += 8;
	DataSize = le32(DataChunk.size);
	if(DataSize > FileSize - DataOffset)
		DataSize = FileSize - DataOffset;
	Is16Bit = (le16(FmtChunk.bps) == 16);

	//! SampleRate is a u16 and reaches ASND, so a rate the file made up must not
	//! reach it as 0 or as a truncated value.
	u32 freq = le32(FmtChunk.freq);
	SampleRate = (freq >= 8000 && freq <= 48000) ? freq : 48000;

	if (le16(FmtChunk.channels) == 1 && le16(FmtChunk.bps) == 8 && le16(FmtChunk.alignment) <= 1)
		Format = VOICE_MONO_8BIT;
	else if (le16(FmtChunk.channels) == 1 && le16(FmtChunk.bps) == 16 && le16(FmtChunk.alignment) <= 2)
		Format = VOICE_MONO_16BIT;
	else if (le16(FmtChunk.channels) == 2 && le16(FmtChunk.bps) == 8 && le16(FmtChunk.alignment) <= 2)
		Format = VOICE_STEREO_8BIT;
	else if (le16(FmtChunk.channels) == 2 && le16(FmtChunk.bps) == 16 && le16(FmtChunk.alignment) <= 4)
		Format = VOICE_STEREO_16BIT;

	Decode();
}

void WavDecoder::CloseFile()
{
	if(file_fd)
		delete file_fd;

	file_fd = NULL;
}

int WavDecoder::Read(u8 * buffer, int buffer_size, int pos)
{
	if(!file_fd)
		return -1;

	if(CurPos >= (int) DataSize)
		return 0;

	file_fd->seek(DataOffset+CurPos, SEEK_SET);

	if(buffer_size > (int) DataSize-CurPos)
		buffer_size = DataSize-CurPos;

	int read = file_fd->read(buffer, buffer_size);
	if(read > 0)
	{
		if (Is16Bit)
		{
			read &= ~0x0001;

			for (u32 i = 0; i < (u32) (read / sizeof (u16)); ++i)
				((u16 *) buffer)[i] = le16(((u16 *) buffer)[i]);
		}
		CurPos += read;
	}

	return read;
}
