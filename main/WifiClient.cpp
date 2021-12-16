#include "WifiClient.h"

#include <cassert>
#include <cstring>

#include "freertos/task.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_DWSS_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_DWSS_WIFI_PWD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_DWSS_WIFI_MAX_RETRY

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static void EventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	WifiClient *client = reinterpret_cast<WifiClient *>(event_data);

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (client->connectRetryNum < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			client->connectRetryNum++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(client->eventGroup, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:%s",
				ip4addr_ntoa(&event->ip_info.ip));
		client->connectRetryNum = 0;
		xEventGroupSetBits(client->eventGroup, WIFI_CONNECTED_BIT);
	}
}

WifiClient::WifiClient()
{
	eventGroup = xEventGroupCreate();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &EventHandler, this));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &EventHandler, this));

	return;
}

WifiClient::~WifiClient()
{
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &EventHandler));
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &EventHandler));
	vEventGroupDelete(eventGroup);

	esp_wifi_deinit();
}

int WifiClient::SetConfig(const WifiConfigData *config)
{
	esp_err_t err;

	wifi_config_t wifi_config;

	assert(config);

	strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));

	/* Setting a password implies station will connect to all security modes including WEP/WPA.
	 * However these modes are deprecated and not advisable to be used. Incase your Access point
	 * doesn't support WPA2, these mode can be enabled by commenting below line */

	if (strlen((char *)wifi_config.sta.password)) {
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}

	err = esp_wifi_set_mode(config->mode ? WIFI_MODE_STA : WIFI_MODE_AP);
	if (err)
	{
		return err;
	}

	err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);

	return err;
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

	err = esp_wifi_start();

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	return err;
}

int WifiClient::Stop()
{
	esp_err_t err = esp_wifi_stop();

	return err;
}

int WifiClient::Wait()
{
	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by EventHandler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(eventGroup,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
				EXAMPLE_ESP_WIFI_SSID, "xxx");
		//EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
				EXAMPLE_ESP_WIFI_SSID, "xxx");
		//EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	return 0;
}
