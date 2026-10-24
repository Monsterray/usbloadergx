/****************************************************************************
 * AutoInput: scripted controller input for unattended test runs.
 *
 * Built only with -DAUTOINPUT (scripts/diag.sh autoinput). A release build gets
 * an empty inline function and no code.
 ***************************************************************************/
#ifndef AUTOINPUT_H_
#define AUTOINPUT_H_

enum
{
	AUTOINPUT_EXIT_NONE = 0,
	AUTOINPUT_EXIT_HBC,
	AUTOINPUT_EXIT_MENU,
	AUTOINPUT_EXIT_LOADER,
	AUTOINPUT_EXIT_PROMPT,
};

#ifdef AUTOINPUT
//! Merge the events due now from sd:/autoinput.txt into userInput[0].
//! Call once per UpdatePads(), after the real pads have been read.
void AutoInput_Apply(void);
//! An "exit" event that came due, or AUTOINPUT_EXIT_NONE; taking it clears it.
//! Call from the main thread.
int AutoInput_TakeExit(void);
#else
static inline void AutoInput_Apply(void) {}
static inline int AutoInput_TakeExit(void) { return AUTOINPUT_EXIT_NONE; }
#endif

#endif
