/****************************************************************************
 * AutoInput: scripted controller input for unattended test runs.
 *
 * Built only with -DAUTOINPUT (scripts/diag.sh autoinput). A release build gets
 * an empty inline function and no code.
 ***************************************************************************/
#ifndef AUTOINPUT_H_
#define AUTOINPUT_H_

#ifdef AUTOINPUT
//! Merge the events due now from sd:/autoinput.txt into userInput[0].
//! Call once per UpdatePads(), after the real pads have been read.
void AutoInput_Apply(void);
#else
static inline void AutoInput_Apply(void) {}
#endif

#endif
