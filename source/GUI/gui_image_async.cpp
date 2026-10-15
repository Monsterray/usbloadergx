/****************************************************************************
 * USB Loader GX
 *
 * gui_imagea_sync.cpp
 ***************************************************************************/
#include <unistd.h>
#include "gui_image_async.h"
#include "memory/mem2.h"
#include "gecko.h"

std::vector<GuiImageAsync *> GuiImageAsync::List;
lwp_t GuiImageAsync::Thread = LWP_THREAD_NULL;
mutex_t GuiImageAsync::ListLock = LWP_THREAD_NULL;
GuiImageAsync * GuiImageAsync::InUse = NULL;
u32 GuiImageAsync::ThreadCount = 0;
bool GuiImageAsync::ThreadSleep = true;
bool GuiImageAsync::CloseThread = false;

static inline void * memdup(const void* src, size_t len)
{
	if(!src) return NULL;

	void *dst = malloc(len);
	if (dst) memcpy(dst, src, len);
	return dst;
}

static GuiImageData * StdImageLoaderCallback(void *arg)
{
	return new GuiImageData((char *) arg);
}

GuiImageAsync::GuiImageAsync(const char *Filename, GuiImageData * PreloadImg) :
	GuiImage(PreloadImg), imgData(NULL), callback(StdImageLoaderCallback), arg(strdup(Filename))
{
	ThreadInit();
	ThreadAddImage(this);
}

GuiImageAsync::GuiImageAsync(ImageLoaderCallback Callback, const void * Arg, int ArgLen, GuiImageData * PreloadImg) :
	GuiImage(PreloadImg), imgData(NULL), callback(Callback), arg(memdup(Arg, ArgLen))
{
	ThreadInit();
	ThreadAddImage(this);
}

GuiImageAsync::~GuiImageAsync()
{
	ThreadRemoveImage(this);
	ThreadExit();
	while(InUse == this) usleep(100);
	if (imgData) delete imgData;
	if (arg) free(arg);
}

void GuiImageAsync::ThreadAddImage(GuiImageAsync *Image)
{
	LWP_MutexLock(ListLock);
	List.push_back(Image);
	LWP_MutexUnlock(ListLock);
	ThreadSleep = false;
	LWP_ResumeThread(Thread);
}

//! Search under ListLock too: the loader thread erases the front entry, and an index
//! found without the lock can name another image by the time it is erased.
void GuiImageAsync::ThreadRemoveImage(GuiImageAsync *Image)
{
	LWP_MutexLock(ListLock);
	for(u32 i = 0; i < List.size(); ++i)
	{
		if(List[i] == Image)
		{
			List.erase(List.begin()+i);
			break;
		}
	}
	LWP_MutexUnlock(ListLock);
}

void GuiImageAsync::ClearQueue()
{
	LWP_MutexLock(ListLock);
	List.clear();
	LWP_MutexUnlock(ListLock);
}

bool GuiImageAsync::WaitIdle(u32 timeoutMs)
{
	for (u32 waited = 0; InUse != NULL; waited++)
	{
		if (waited >= timeoutMs * 10)
			return false;
		usleep(100);
	}
	return true;
}

//! Declared in gui_imagedata.h. The pool takes no new texture after TEX_close(), but
//! the loader thread may be decoding a cover into a block it got just before: the
//! pixels are written until its callback returns, so wait for that. Then nothing of
//! GX writes the pool again. The queue goes too, as nothing will draw it.
void GuiTexturePoolClose(void)
{
	TEX_close();
	GuiImageAsync::ClearQueue();
	if (!GuiImageAsync::WaitIdle(2000))
		gprintf("mem: texture pool closed while a cover was still loading\n");
}

void * GuiImageAsync::GuiImageAsyncThread(void *arg)
{
	while(!CloseThread)
	{
		if(ThreadSleep)
			LWP_SuspendThread(Thread);

		while(!List.empty() && !CloseThread)
		{
			LWP_MutexLock(ListLock);
			//! ClearQueue() can empty the list after the check above
			if(List.empty())
			{
				LWP_MutexUnlock(ListLock);
				continue;
			}
			InUse = List.front();
			List.erase(List.begin());
			LWP_MutexUnlock(ListLock);

			if (!InUse)
				continue;

			InUse->imgData = InUse->callback(InUse->arg);

			if (InUse->imgData && InUse->imgData->GetImage())
			{
				InUse->width = InUse->imgData->GetWidth();
				InUse->height = InUse->imgData->GetHeight();
				InUse->image = InUse->imgData->GetImage();
			}

			InUse = NULL;
		}

		ThreadSleep = true;
	}

	return NULL;
}

u32 GuiImageAsync::ThreadInit()
{
	if (Thread == LWP_THREAD_NULL)
	{
		LWP_MutexInit(&ListLock, false);
		LWP_CreateThread(&Thread, GuiImageAsyncThread, NULL, NULL, 32768, 70);
	}
	return ++ThreadCount;
}

u32 GuiImageAsync::ThreadExit()
{
	//! We don't need to always shutdown and startup the thread, especially
	//! since this is a nested startup/shutdown from the gui thread.
	//! It's fine with being put to suspended only.
	/*
	if (--ThreadCount == 0)
	{
		CloseThread = true;
		LWP_ResumeThread(Thread);
		LWP_JoinThread(Thread, NULL);
		LWP_MutexDestroy(ListLock);
		Thread = LWP_THREAD_NULL;
		ListLock = LWP_MUTEX_NULL;
	}
	*/
	return --ThreadCount;
}

