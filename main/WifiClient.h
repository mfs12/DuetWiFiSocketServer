#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H 1


extern "C" {

#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

}

#include "WifiConfig.h"
#include "include/MessageFormats.h"

class WifiClient
{
private:
	WifiState state;
	const WifiConfigData *config;

	int Scan();
	int GetScanResults();
public:
	WifiClient(void (*callback)(void *arg), void *callbackArg);
	~WifiClient();

	int connectRetryNum = 0; // should be private

	EventGroupHandle_t eventGroup; // should be private

	void (*notifyChangeCb)(void *arg); // callback to signal need to call Process
	void *notifyChangeArg;

	int SetConfig(const WifiConfigData *config);
	const WifiConfigData *GetConfig();
	WifiState GetStatus();

	int Start();
	int StartAccessPoint();
	int Process();
	int Stop();

	static void EventHandler(void* arg, esp_event_base_t event_base,
				int32_t event_id, void* event_data);
};

#endif /* ifndef WIFI_CLIENT_H */
