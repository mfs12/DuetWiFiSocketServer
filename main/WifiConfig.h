#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H 1

#include <stddef.h>
#include "include/MessageFormats.h"

typedef WirelessConfigurationData WifiConfigData;

class WifiConfig
{
	WifiConfig(void);
	~WifiConfig(void);

	int load(size_t index, WifiConfigData *config);
	int save(size_t index, const WifiConfigData *config);

	int remove(size_t index);
	int removeAll(void);

	int findByName(const char *name);
	int findBySSID(const char *SSID);
};

#endif /* ifndef WIFI_CONFIG_H */
