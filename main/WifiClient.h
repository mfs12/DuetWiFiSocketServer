#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H 1


extern "C" {
	void wifi_init_sta(void);
}

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "WifiConfig.h"
#include "include/MessageFormats.h"

class WifiClient
{
private:
	WifiState state;
	WifiConfigData *config;

	int Scan();
	int GetScanResults();
public:
	WifiClient();
	~WifiClient();

	/* FreeRTOS event group to signal when we are connected*/
	EventGroupHandle_t s_wifi_event_group;
	int s_retry_num = 0;

	int Configure(const WifiConfigData *config);
	WifiConfigData *GetConfig();
	WifiState GetStatus();

	int Start();
	int Stop();
	int Wait();
	int StartAccessPoint();
};

#endif /* ifndef WIFI_CLIENT_H */
