#ifndef _USBSTORAGE2_H_
#define _USBSTORAGE2_H_

#include "ogc/disc_io.h"

#ifdef __cplusplus
extern "C"
{
#endif

	/* Prototypes */
	s32 USBStorage2_Init(u32 port);
	void USBStorage2_Deinit();
	s32 USBStorage2_GetCapacity(u32 port, u32 *size);

	s32 USBStorage2_ReadSectors(u32 port, u32 sector, u32 numSectors, void *buffer);
	s32 USBStorage2_WriteSectors(u32 port, u32 sector, u32 numSectors, const void *buffer);

	s32 USBStorage2_SetPort(u32 port);
	s32 USBStorage2_GetPort();
	//! While on, a write whose sectors already hold those bytes is skipped (PartitionHandle::UnMount)
	void USBStorage2_SkipUnchangedWrites(bool on);
#ifdef USB_BENCH
	//! The cIOS bench build only (utils/UsbBench.cpp): no skip in unmounts, and write counters
	void USBStorage2_BenchNoSkip(bool on);
	//! On by default: every USB write is refused, until -benchwrites=1
	void USBStorage2_BenchReadOnly(bool on);
	u32 USBStorage2_BenchRefused(void);
	void USBStorage2_BenchWrites(u32 *calls, u32 *sectors, u32 *failed, u32 *totalMs, u32 *maxUs);
#endif

#define DEVICE_TYPE_WII_UMS (('W'<<24)|('U'<<16)|('M'<<8)|'S')

	extern const DISC_INTERFACE __io_usbstorage2_port0;
	extern const DISC_INTERFACE __io_usbstorage2_port1;

#ifdef __cplusplus
}
#endif

#endif
