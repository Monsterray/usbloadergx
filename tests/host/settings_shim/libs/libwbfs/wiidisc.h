/* wiidisc.h for settings_test: SettingsEnums.h includes it for the partition selector
 * CSettings::SetDefault() uses. Same names and values as source/libs/libwbfs/wiidisc.h;
 * the rest of that header needs libogc. */
#ifndef HOST_SHIM_WIIDISC_H
#define HOST_SHIM_WIIDISC_H

typedef enum
{
	UPDATE_PARTITION_TYPE = 0, GAME_PARTITION_TYPE, OTHER_PARTITION_TYPE,
	ALL_PARTITIONS = 0xffffffff - 3,
	REMOVE_UPDATE_PARTITION,
	ONLY_GAME_PARTITION,
} partition_selector_t;

#endif
