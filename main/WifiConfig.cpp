#include "WifiConfig.h"

WifiConfig::WifiConfig(void)
{
	// TODO init access to spi flash
}

WifiConfig::~WifiConfig(void)
{
	// TODO cleanup interface to spi flash
}

int WifiConfig::load(size_t index, WifiConfigData *config)
{
	if (index >= MaxRememberedNetworks || !config)
	{
		return -1;
	}

	// TODO const WirelessConfigurationData * const tempData = EEPROM.getPtr<WirelessConfigurationData>(i * sizeof(WirelessConfigurationData));

	return 0;
}

int WifiConfig::save(size_t index, const WifiConfigData *config)
{
	if (index >= MaxRememberedNetworks || !config)
	{
		return -1;
	}

	return 0;

}


int WifiConfig::remove(size_t index)
{
	// TODO
	return -1;
}

int WifiConfig::removeAll(void)
{
	// TODO
	return -1;
}

int WifiConfig::findByName(const char *name)
{
	// TODO
	return -1;
}

int WifiConfig::findBySSID(const char *SSID)
{
	// TODO
	return -1;
}
