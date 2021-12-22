#include "WifiClient.h"

#include <cassert>
#include <cstring>

extern "C" {

#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

}

#define DEBUG 0
#include "Debug.h"

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define WIFI_CLIENT_WIFI_SSID      "ssid"
#define WIFI_CLIENT_WIFI_PASS      "pwd"
#define WIFI_CLIENT_MAXIMUM_RETRY  3

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_ALL_BITS 0xffffffff

static const char *TAG = __FILE__;

void WifiClient::EventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	WifiClient *client = reinterpret_cast<WifiClient *>(arg);

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (client->connectRetryNum < WIFI_CLIENT_MAXIMUM_RETRY) {
			esp_wifi_connect();
			client->connectRetryNum++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(client->eventGroup, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
		client->connectRetryNum = 0;
		xEventGroupSetBits(client->eventGroup, WIFI_CONNECTED_BIT);
		xEventGroupSetBits(client->eventGlobal, (1 << 1));
	}
}


WifiClient::WifiClient(EventGroupHandle_t global)
{
	state = WifiState::disabled;

	eventGlobal = global;

	eventGroup = xEventGroupCreate();
	if (eventGroup == nullptr)
	{
		err("failed to create event group.\n");
	}

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, EventHandler, this));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, EventHandler, this));

	return;
}

WifiClient::~WifiClient()
{
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &EventHandler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &EventHandler));
	vEventGroupDelete(eventGroup);

	esp_wifi_deinit();
}

int WifiClient::SetConfig(const WifiConfigData *newConfig)
{
	config = newConfig;

	return 0;
}


const WifiConfigData *WifiClient::GetConfig()
{
	return config;
}


WifiState WifiClient::GetStatus()
{
	return state;
}

int WifiClient::Start()
{
	esp_err_t err;

	debug("starting...\n");

	if (state == WifiState::connecting ||
	    state == WifiState::connected)
	{
		debug("already connected\n");
		return 0;
	}

	state = WifiState::connecting;

	wifi_config_t wifi_config;

	assert(config);

	memset(&wifi_config, 0, sizeof(wifi_config));

	strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));
	if (strlen((char *)wifi_config.sta.password)) {
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}

	err = esp_wifi_set_mode((wifi_mode_t)config->mode);
	if (err)
	{
		err("failed to configure mode.\n");
		return err;
	}

	err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
	if (err)
	{
		err("failed to set config.\n");
		return err;
	}

	err = esp_wifi_start();

	debug(TAG, "starting - done.\n");

	return err;
}

int WifiClient::Stop()
{
	esp_err_t err = esp_wifi_stop();

	state = WifiState::idle;

	return err;
}

int WifiClient::Process()
{
	assert(config);
#if 1
	EventBits_t bits = xEventGroupClearBits(eventGroup, WIFI_ALL_BITS);
#else
	debug("processing\n");
	EventBits_t bits = xEventGroupWaitBits(eventGroup,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
	debug("waiting done\n");
#endif

	if (bits == 0)
	{
		//debug("nothing to do\n");
		return 0;
	}

	if (bits & WIFI_CONNECTED_BIT) {
		info("connected to ap SSID:%s password:%s\n", config->ssid, "xxx");

		state = WifiState::connected;
		bits &= ~WIFI_CONNECTED_BIT;
	}
	if (bits & WIFI_FAIL_BIT) {
		err("connected to ap SSID:%s\n", config->ssid);
		state = WifiState::reconnecting;
		bits &= ~WIFI_FAIL_BIT;
	}
	if (bits) {
		warn("unexpected event %08x", (int)bits);
	}

	return 0;
}
