/****************************************************************************
 * UsbBench: times GX's USB work on a real Wii for the cIOS comparison.
 *
 * Built only with -DUSB_BENCH (scripts/build.sh bench). A release build gets
 * empty inline functions and no code. scripts/usb_bench.py runs it on the
 * bench Wii and fetches its log; the arguments are described in UsbBench.cpp.
 ***************************************************************************/
#ifndef USBBENCH_H_
#define USBBENCH_H_

#ifdef USB_BENCH
//! Before StartUpProcess::Run(): reads the -bench= arguments and, for a
//! wiiload start with no path in argv[0], takes the review build's folder.
void UsbBench_Init(int argc, char *argv[]);
//! In MainMenu() once the GUI runs: the measurements, then the exit to HBC.
void UsbBench_Run(void);
#else
static inline void UsbBench_Init(int, char *[]) {}
static inline void UsbBench_Run(void) {}
#endif

#endif
