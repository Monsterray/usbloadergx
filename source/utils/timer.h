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
#ifndef __TIMER_H
#define __TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <gctypes.h>
#include <ogc/lwp_watchdog.h>

#ifdef __cplusplus
}

class Timer
{
	public:
		Timer() { starttick = gettime(); }
		~Timer() { }
		float elapsed() { return (float) (gettime()-starttick)/(1000.0f*TB_TIMER_CLOCK); }
		u32 elapsed_millisecs() { return (u32) ((gettime()-starttick)/TB_TIMER_CLOCK); }
		void reset() { starttick = gettime(); }
	protected:
		u64 starttick;
};

#endif //__cplusplus

#endif //__TIMER_H
