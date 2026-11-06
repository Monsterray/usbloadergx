/* DeviceHandler.hpp for settings_test: only the names FindConfig() loops over.
 * The test gives ConfigPath a ':' that fopen can open, so the loop never runs. */
#ifndef HOST_SHIM_DEVICEHANDLER_HPP
#define HOST_SHIM_DEVICEHANDLER_HPP

enum { SD, MAXDEVICES };
static const char *const DeviceName[MAXDEVICES] = { "sd" };

#endif
