/*
 * SocketServer.cpp
 *
 *  Created on: 25 Mar 2017
 *      Author: David
 */

#include "Config.h"

#if SOCKETSERVER_ENABLE

extern "C" {

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_spi_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "nvs.h"
#include "nvs_flash.h"

}

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "arduino/gpio.h"
#include "include/MessageFormats.h"

#include "Connection.h"
#include "DwssSpiffs.h"
#include "HSPI.h"
#include "Listener.h"
#include "Misc.h"
#include "Process.h"
#include "TcpServer.h"
#include "WifiClient.h"

#define DEBUG 1
#include "Debug.h"

#if 0

#include "ecv.h"
#undef yield
#undef array
#define array _ecv_array

#else

#define pre(...)
#define array

#endif

#define millis xTaskGetTickCount

// gpios
static const gpio_num_t OnBoardLed = GPIO_NUM_2;	// active low
static const gpio_num_t SamCsPin = GPIO_NUM_15;		// output to SAM, SS pin for SPI transfer, active low
static const gpio_num_t EspReqTransferPin = GPIO_NUM_0;	// output, indicates to the SAM that we want to send something
static const gpio_num_t SamTfrReadyPin = GPIO_NUM_4;	// input, indicates that SAM is ready to execute an SPI transaction

static const char* lastError = nullptr;
static const char* prevLastError = nullptr;

//ADC_MODE(ADC_VCC);          // need this for the ESP.getVcc() call to work

// led
static const bool ONBOARD_LED_ON = false;
static const uint32_t ONBOARD_LED_BLINK_CHANGE = 100;	// ms
static const uint32_t ONBOARD_LED_BLINK_STABLE = 200;	// ms
static uint32_t lastBlinkTime = 0;

// spi
static HSPIClass hspi;
static bool transferReadyChanged = false;
static const uint32_t TransferReadyTimeout = 10;	// how many milliseconds we allow for the Duet to set TransferReady low after the end of a transaction, before we assume that we missed seeing it
static uint32_t transferBuffer[NumDwords(MaxDataLength + 1)];
static const uint32_t StatusReportMillis = 200;
static uint32_t lastSpiTransferTime = 0;

// wifi
static const uint32_t MaxConnectTime = 40 * 1000;		// how long we wait for WiFi to connect in milliseconds
static uint32_t connectStartTime;
static const int DefaultWiFiChannel = 6;
static bool connectErrorChanged = false;
static WifiClient *wifiClient = nullptr;
static WifiConfigData wifiConfig;

// mdns
static const char * const MdnsProtocolNames[3] = { "HTTP", "FTP", "Telnet" };
static const char * const MdnsServiceStrings[3] = { "_http", "_ftp", "_telnet" };
static const char * const MdnsTxtRecords[2] = { "product=DuetWiFi", "version=" VERSION_MAIN };
static const unsigned int MdnsTtl = 10 * 60;			// same value as on the Duet 0.6/0.8.5
static char webHostName[HostNameLength + 1] = "Duet-WiFi";

// obsolete???
static uint32_t lastStatusReportTime;
static char currentSsid[SsidLength + 1];

static char lastConnectError[100];


static const WirelessConfigurationData *ssidData = nullptr;

extern "C" {
	void app_main(void);
}

static void arduino_setup(void);
static void arduino_loop(void);
static void ICACHE_RAM_ATTR ProcessRequest(void);

static void led_blink(void)
{
	uint32_t now = millis();

	assert(wifiClient);

	switch (wifiClient->GetStatus())
	{
	case WifiState::autoReconnecting:
	case WifiState::connecting:
	case WifiState::reconnecting:
		if (now - lastBlinkTime > ONBOARD_LED_BLINK_CHANGE)
		{
			lastBlinkTime = now;
			gpio_set_level(OnBoardLed, !gpio_get_level(OnBoardLed));
		}
		break;
	default:
		if (now - lastBlinkTime > ONBOARD_LED_BLINK_STABLE)
		{
			lastBlinkTime = now;
			gpio_set_level(OnBoardLed, !gpio_get_level(OnBoardLed));
		}
		break;
	}
}

static EventGroupHandle_t globalEventGroup;
#define APP_EVENT_RRF BIT0
#define APP_EVENT_WIFI BIT1
#define APP_EVENT_TCP BIT2
#define APP_EVENT_ALL (APP_EVENT_RRF | APP_EVENT_WIFI | APP_EVENT_TCP)

static void GpioIsrHandler(void *arg)
{
	int event = (int)(arg);
	xEventGroupSetBitsFromISR(globalEventGroup, event, nullptr);
}

static int app_init(void)
{
	esp_err_t err;

	gpio_config_t led_gpio;
	led_gpio.intr_type = GPIO_INTR_DISABLE;
	led_gpio.mode = GPIO_MODE_OUTPUT;
	led_gpio.pin_bit_mask = BIT(OnBoardLed);
	led_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	led_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&led_gpio);
	if (err) {
		err("failed to init led_gpio.\n");
		return -1;
	}

	gpio_config_t cs_gpio;
	cs_gpio.intr_type = GPIO_INTR_DISABLE;
	cs_gpio.mode = GPIO_MODE_OUTPUT;
	cs_gpio.pin_bit_mask = BIT(SamCsPin);
	cs_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	cs_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&cs_gpio);
	if (err) {
		err("failed to init cs_gpio.\n");
		return -1;
	}

	gpio_set_level(SamCsPin, 1);

	gpio_config_t transfer_gpio;
	transfer_gpio.intr_type = GPIO_INTR_DISABLE;
	transfer_gpio.mode = GPIO_MODE_OUTPUT;
	transfer_gpio.pin_bit_mask = BIT(EspReqTransferPin);
	transfer_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	transfer_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&transfer_gpio);
	if (err) {
		err("failed to init transfer_gpio.\n");
		return -1;
	}

	gpio_set_level(EspReqTransferPin, 0);

	gpio_config_t receive_gpio;
	receive_gpio.intr_type = GPIO_INTR_POSEDGE;
	receive_gpio.mode = GPIO_MODE_INPUT;
	receive_gpio.pin_bit_mask = BIT(SamTfrReadyPin);
	receive_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	receive_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&receive_gpio);
	if (err) {
		err("failed to init receive_gpio.\n");
		return -1;
	}

	err = gpio_install_isr_service(0);
	if (err)
	{
		err("failed to disable gpio isr service.\n");
		return -1;
	}
	gpio_isr_handler_add(SamTfrReadyPin, GpioIsrHandler, (void *)APP_EVENT_RRF);
	if (err)
	{
		err("failed to add gpio isr.\n");
		return -1;
	}

	hspi.InitMaster(SPI_MODE1, defaultClockControl, true);

	globalEventGroup = xEventGroupCreate();
	if (!globalEventGroup)
	{
		err("failed to create gpio event group\n");
		return -1;
	}

	// wifi dependencies
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(nvs_flash_init());

	wifiClient = new WifiClient(globalEventGroup);

	wifiConfig.ip = 0,
	wifiConfig.gateway = 0,
	wifiConfig.netmask = 0,
	wifiConfig.channel = DefaultWiFiChannel,
	wifiConfig.mode = WIFI_MODE_STA,
	SafeStrncpy(wifiConfig.ssid, CONFIG_WIFI_SSID_DEFAULT, sizeof(wifiConfig.ssid));
	SafeStrncpy(wifiConfig.password, CONFIG_WIFI_PASSWORD_DEFAULT, sizeof(wifiConfig.password));

	wifiClient->SetConfig(&wifiConfig);
#if 1
	TcpServer_init();

	Connection::Init();
	Listener::Init();
#endif

	return 0;
}

static void signalChange(void)
{
	// signal status change
	gpio_set_level(EspReqTransferPin, 0);
	vTaskDelay(10 / portTICK_PERIOD_MS);
	gpio_set_level(EspReqTransferPin, 1);
	vTaskDelay(10 / portTICK_PERIOD_MS);
}

void app_main(void)
{
	info("DuetWebSocketServer\n");

	/* Print chip information */
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);

	info("This is ESP8266 chip with %d CPU cores, WiFi, ", chip_info.cores);

	info("silicon revision %d, ", chip_info.revision);

	info("%uMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
			(chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

	app_init();

	info("Init - Done\n");

#if 1

	WifiState oldState = wifiClient->GetStatus();

	esp_err_t err;

	err = wifiClient->Start();
	if (err)
	{
		err("failed to start client %d\n", err);
	}

	signalChange();

	for (;;) {
		// toggle led
		led_blink();

		vTaskDelay(10 / portTICK_PERIOD_MS);
		EventBits_t bits = xEventGroupWaitBits(globalEventGroup, APP_EVENT_ALL,
					pdTRUE, pdFALSE, 1000 / portTICK_PERIOD_MS);
		if (!bits)
		{
			debug("timeout\n");
			signalChange();
			continue;
		}

		debug("bits %08x\n", (int)bits);

		if (bits & APP_EVENT_WIFI && wifiClient->Process() < 0)
		{
			err("error processing wifiClient.\n");
			break;
		}

		WifiState state = wifiClient->GetStatus();
		//debug("running...\n");
		if (bits & APP_EVENT_ALL)
		{
			if (bits & (APP_EVENT_WIFI | APP_EVENT_TCP))
				signalChange();

			debug("event %02x wifi state %d -> %d\n", bits, (int)oldState, (int)state);
			bits = 0;
			oldState = state;
		}
		else
		{
			debug("waiting for event...\n");
			continue;
		}
#if 1
		// activate spi CS
		gpio_set_level(SamCsPin, 0);
		vTaskDelay(10 / portTICK_PERIOD_MS);

		ProcessRequest();

		lastSpiTransferTime = millis();

		// deactivate spi CS
		gpio_set_level(SamCsPin, 1);

		debug("processing - done\n");
#endif
	}
#else
	for (int i = 10; i >= 0; i--) {
		digitalWrite(OnBoardLed, led);
		led = !led;
		info("Restarting in %d seconds...\n", i);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		digitalWrite(OnBoardLed, led);
		led = !led;
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
#endif
	nvs_flash_deinit();

	info("Restarting now.\n");
	fflush(stdout);
	esp_restart();
}

// Look up a SSID in our remembered network list, return pointer to it if found
static const WirelessConfigurationData *RetrieveSsidData(const char *ssid, int *index = nullptr)
{
#if 0
	for (size_t i = 1; i <= MaxRememberedNetworks; ++i)
	{
		const WirelessConfigurationData *wp = EEPROM.getPtr<WirelessConfigurationData>(i * sizeof(WirelessConfigurationData));
		if (wp != nullptr && strncmp(ssid, wp->ssid, sizeof(wp->ssid)) == 0)
		{
			if (index != nullptr)
			{
				*index = i;
			}
			return wp;
		}
	}
	return nullptr;
#else
	return nullptr;
#endif
}

// Find an empty entry in the table of known networks
static bool FindEmptySsidEntry(int *index)
{
#if 0
	for (size_t i = 1; i <= MaxRememberedNetworks; ++i)
	{
		const WirelessConfigurationData *wp = EEPROM.getPtr<WirelessConfigurationData>(i * sizeof(WirelessConfigurationData));
		if (wp != nullptr && wp->ssid[0] == 0xFF)
		{
			*index = i;
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}

// Check socket number in range, returning true if yes. Otherwise, set lastError and return false;
static bool ValidSocketNumber(uint8_t num)
{
	if (num < MaxConnections)
	{
		return true;
	}
	lastError = "socket number out of range";
	return false;
}

// Reset to default settings
static void FactoryReset()
{
#if 0
	WirelessConfigurationData temp;
	memset(&temp, 0xFF, sizeof(temp));
	for (size_t i = 0; i <= MaxRememberedNetworks; ++i)
	{
		EEPROM.put(i * sizeof(WirelessConfigurationData), temp);
	}
	EEPROM.commit();
#endif
}

// Try to connect using the specified SSID and password
static void ConnectToAccessPoint(const WirelessConfigurationData& apData, bool isRetry)
pre(currentState == NetworkState::idle)
{
#if 0
	SafeStrncpy(currentSsid, apData.ssid, ARRAY_SIZE(currentSsid));

	WiFi.mode(WIFI_STA);
	wifi_station_set_hostname(webHostName);     				// must do this before calling WiFi.begin()
	WiFi.setAutoConnect(false);
//	WiFi.setAutoReconnect(false);								// auto reconnect NEVER works in our configuration so disable it, it just wastes time
	WiFi.setAutoReconnect(true);
#if NO_WIFI_SLEEP
	wifi_set_sleep_type(NONE_SLEEP_T);
#else
	wifi_set_sleep_type(MODEM_SLEEP_T);
#endif
	WiFi.config(IPAddress(apData.ip), IPAddress(apData.gateway), IPAddress(apData.netmask), IPAddress(), IPAddress());
	debug("Trying to connect to ssid \"%s\" with password \"%s\"\n", apData.ssid, apData.password);
	WiFi.begin(apData.ssid, apData.password);

	if (isRetry)
	{
		currentState = WifiState::reconnecting;
	}
	else
	{
		currentState = WifiState::connecting;
		connectStartTime = millis();
	}
#endif
}

static void ConnectPoll()
{
#if 0
	// The Arduino WiFi.status() call is fairly useless here because it discards too much information, so use the SDK API call instead
	const uint8_t status = wifi_station_get_connect_status();
	const char *error = nullptr;
	bool retry = false;

	switch (currentState)
	{
	case WifiState::connecting:
	case WifiState::reconnecting:
		// We are trying to connect or reconnect, so check for success or failure
		switch (status)
		{
		case STATION_IDLE:
			error = "Unexpected WiFi state 'idle'";
			break;

		case STATION_CONNECTING:
			if (millis() - connectStartTime >= MaxConnectTime)
			{
				error = "Timed out";
			}
			break;

		case STATION_WRONG_PASSWORD:
			error = "Wrong password";
			break;

		case STATION_NO_AP_FOUND:
			error = "Didn't find access point";
			retry = (currentState == WifiState::reconnecting);
			break;

		case STATION_CONNECT_FAIL:
			error = "Failed";
			retry = (currentState == WifiState::reconnecting);
			break;

		case STATION_GOT_IP:
			if (currentState == WifiState::reconnecting)
			{
				lastError = "Reconnect succeeded";
			}
			else
			{
				mdns_resp_netif_settings_changed(netif_list);	// STA is on first interface
			}

			debug("Connected to AP\n");
			currentState = WifiState::connected;
			digitalWrite(OnBoardLed, ONBOARD_LED_ON);
			break;

		default:
			error = "Unknown WiFi state";
			break;
		}

		if (error != nullptr)
		{
			strcpy(lastConnectError, error);
			SafeStrncat(lastConnectError, " while trying to connect to ", ARRAY_SIZE(lastConnectError));
			SafeStrncat(lastConnectError, currentSsid, ARRAY_SIZE(lastConnectError));
			lastError = lastConnectError;
			connectErrorChanged = true;
			debug("Failed to connect to AP\n");

			if (!retry)
			{
				WiFi.mode(WIFI_OFF);
				currentState = WifiState::idle;
				digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
			}
		}
		break;

	case WifiState::connected:
		if (status != STATION_GOT_IP)
		{
			// We have just lost the connection
			connectStartTime = millis();						// start the auto reconnect timer

			switch (status)
			{
			case STATION_CONNECTING:							// auto reconnecting
				error = "auto reconnecting";
				currentState = WifiState::autoReconnecting;
				break;

			case STATION_IDLE:
				error = "state 'idle'";
				retry = true;
				break;

			case STATION_WRONG_PASSWORD:
				error = "state 'wrong password'";
				currentState = WifiState::idle;
				digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
				break;

			case STATION_NO_AP_FOUND:
				error = "state 'no AP found'";
				retry = true;
				break;

			case STATION_CONNECT_FAIL:
				error = "state 'fail'";
				retry = true;
				break;

			default:
				error = "unknown WiFi state";
				currentState = WifiState::idle;
				digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
				break;
			}

			strcpy(lastConnectError, "Lost connection, ");
			SafeStrncat(lastConnectError, error, ARRAY_SIZE(lastConnectError));
			lastError = lastConnectError;
			connectErrorChanged = true;
			debug("Lost connection to AP\n");
			break;
		}
		break;

	case WifiState::autoReconnecting:
		if (status == STATION_GOT_IP)
		{
			lastError = "Auto reconnect succeeded";
			currentState = WifiState::connected;
		}
		else if (status != STATION_CONNECTING && lastError == nullptr)
		{
			lastError = "Auto reconnect failed, trying manual reconnect";
			connectStartTime = millis();						// start the manual reconnect timer
			retry = true;
		}
		else if (millis() - connectStartTime >= MaxConnectTime)
		{
			lastError = "Timed out trying to auto-reconnect";
			retry = true;
		}
		break;

	default:
		break;
	}

	if (retry)
	{
		ConnectToAccessPoint(*ssidData, true);
	}
#endif
}

static void StartClient(const char * array ssid)
pre(currentState == WifiState::idle)
{
#if 0
	ssidData = nullptr;

	if (ssid == nullptr || ssid[0] == 0)
	{
		// Auto scan for strongest known network, then try to connect to it
		const int8_t num_ssids = WiFi.scanNetworks(false, true);
		if (num_ssids < 0)
		{
			lastError = "network scan failed";
			currentState = WifiState::idle;
			digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
			return;
		}

		// Find the strongest network that we know about
		int8_t strongestNetwork = -1;
		for (int8_t i = 0; i < num_ssids; ++i)
		{
			info("found network %s\n", WiFi.SSID(i).c_str());
			if (strongestNetwork < 0 || WiFi.RSSI(i) > WiFi.RSSI(strongestNetwork))
			{
				const WirelessConfigurationData *wp = RetrieveSsidData(WiFi.SSID(i).c_str(), nullptr);
				if (wp != nullptr)
				{
					strongestNetwork = i;
					ssidData = wp;
				}
			}
		}
		if (strongestNetwork < 0)
		{
			lastError = "no known networks found";
			return;
		}
	}
	else
	{
		ssidData = RetrieveSsidData(ssid, nullptr);
		if (ssidData == nullptr)
		{
			lastError = "no data found for requested SSID";
			return;
		}
	}

	// ssidData contains the details of the strongest known access point
	ConnectToAccessPoint(*ssidData, false);
#endif
}

static bool CheckValidSSID(const char * array s)
{
	size_t len = 0;
	while (*s != 0)
	{
		if (*s < 0x20 || *s == 0x7F)
		{
			return false;					// bad character
		}
		++s;
		++len;
		if (len == SsidLength)
		{
			return false;					// ESP8266 core requires strlen(ssid) <= 31
		}
	}
	return len != 0;
}

static bool CheckValidPassword(const char * array s)
{
	size_t len = 0;
	while (*s != 0)
	{
		if (*s < 0x20 || *s == 0x7F)
		{
			return false;					// bad character
		}
		++s;
		++len;
		if (len == PasswordLength)
		{
			return false;					// ESP8266 core requires strlen(password) <= 63
		}
	}
	return len == 0 || len >= 8;			// password must be empty or at least 8 characters (WPA2 restriction)
}

// Check that the access point data is valid
static bool ValidApData(const WirelessConfigurationData &apData)
{
	// Check the IP address
	if (apData.ip == 0 || apData.ip == 0xFFFFFFFF)
	{
		return false;
	}

	// Check the channel. 0 means auto so it OK.
	if (apData.channel > 13)
	{
		return false;
	}

	return CheckValidSSID(apData.ssid) && CheckValidPassword(apData.password);
}

static void StartAccessPoint()
{
#if 0
	WirelessConfigurationData apData;
	EEPROM.get(0, apData);

	if (ValidApData(apData))
	{
		SafeStrncpy(currentSsid, apData.ssid, ARRAY_SIZE(currentSsid));
		bool ok = WiFi.mode(WIFI_AP);
		if (ok)
		{
			IPAddress apIP(apData.ip);
			ok = WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
			if (ok)
			{
				debug("Starting AP %s with password \"%s\"\n", currentSsid, apData.password);
				ok = WiFi.softAP(currentSsid, apData.password, (apData.channel == 0) ? DefaultWiFiChannel : apData.channel);
				if (!ok)
				{
					info("Failed to start AP\n");
				}
			}
			else
			{
				info("Failed to set AP config\n");
			}
		}
		else
		{
			info("Failed to set AP mode\n");
		}

		if (ok)
		{
			info("AP started\n");
			dns.setErrorReplyCode(DNSReplyCode::NoError);
			if (!dns.start(53, "*", apData.ip))
			{
				lastError = "Failed to start DNS\n";
				debug("%s\n", lastError);
			}
			SafeStrncpy(currentSsid, apData.ssid, ARRAY_SIZE(currentSsid));
			currentState = WifiState::runningAsAccessPoint;
			digitalWrite(OnBoardLed, ONBOARD_LED_ON);
			mdns_resp_netif_settings_changed(netif_list->next);		// AP is on second interface
		}
		else
		{
			WiFi.mode(WIFI_OFF);
			lastError = "Failed to start access point";
			debug("%s\n", lastError);
			currentState = WifiState::idle;
			digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
		}
	}
	else
	{
		lastError = "invalid access point configuration";
		debug("%s\n", lastError);
		currentState = WifiState::idle;
		digitalWrite(OnBoardLed, !ONBOARD_LED_ON);
	}
#endif
}

static void MdnsGetServiceTxtEntries(struct mdns_service *service, void *txt_userdata)
{
#if 0
	for (size_t i = 0; i < ARRAY_SIZE(MdnsTxtRecords); i++)
	{
		mdns_resp_add_service_txtitem(service, MdnsTxtRecords[i], strlen(MdnsTxtRecords[i]));
	}
#endif
}

// Rebuild the mDNS services
static void MdnsRebuildServices()
{
#if 0
	for (struct netif *item = netif_list; item != nullptr; item = item->next)
	{
		mdns_resp_remove_netif(item);
		mdns_resp_add_netif(item, webHostName, MdnsTtl);
		mdns_resp_add_service(item, "echo", "_echo", DNSSD_PROTO_TCP, 0, 0, nullptr, nullptr);

		for (size_t protocol = 0; protocol < 3; protocol++)
		{
			const uint16_t port = Listener::GetPortByProtocol(protocol);
			if (port != 0)
			{
				service_get_txt_fn_t txtFunc = (protocol == 0/*HttpProtocol*/) ? MdnsGetServiceTxtEntries : nullptr;
				mdns_resp_add_service(item, webHostName, MdnsServiceStrings[protocol], DNSSD_PROTO_TCP, port, MdnsTtl, txtFunc, nullptr);
			}
		}

		mdns_resp_netif_settings_changed(item);
	}
#endif
}

static void MdnsRemoveServices()
{
#if 0
	for (struct netif *item = netif_list; item != nullptr; item = item->next)
	{
		mdns_resp_remove_netif(item);
	}
#endif
}

// Send a response.
// 'response' is the number of bytes of response if positive, or the error code if negative.
// Use only to respond to commands which don't include a data block, or when we don't want to read the data block.
static void ICACHE_RAM_ATTR SendErrorResponse(int32_t error)
{
	(void)hspi.transfer32(error);
}

static void ICACHE_RAM_ATTR SendResponse(const uint32_t *buffer, size_t size)
{
	(void)hspi.transfer32(size);
	if (size > 0)
	{
		hspi.transferDwords(buffer, nullptr, NumDwords(size));
	}
}

static int ReceiveRequest(NetworkCommand *cmd, uint32_t *buffer, size_t size)
{
	static union
	{
		MessageHeaderSamToEsp hdr;			// the actual header
		uint32_t asDwords[headerDwords];	// to force alignment
	} msgIn;

	static union
	{
		MessageHeaderEspToSam hdr;
		uint32_t asDwords[headerDwords];	// to force alignment
	} msgOut;

	msgIn.hdr.formatVersion = InvalidFormatVersion;
	msgOut.hdr.formatVersion = MyFormatVersion;
	msgOut.hdr.state = (wifiClient) ? wifiClient->GetStatus() : WifiState::disabled;

	// Exchange headers, except for the last dword which will contain our response if message has no data payload
	hspi.transferDwords(msgOut.asDwords, msgIn.asDwords, headerDwords - 1);

	if (msgIn.hdr.formatVersion != MyFormatVersion)
	{
		return ResponseBadRequestFormatVersion;
	}
	else if (msgIn.hdr.dataLength > size)
	{
		return ResponseBadDataLength;
	}

	if (msgIn.hdr.dataLength > 0)
	{
		// TODO strange behavior, pop up param32 which follows after data length
		msgIn.hdr.param32 = hspi.transfer32(ResponseEmpty);

		hspi.transferDwords(nullptr, transferBuffer, NumDwords(msgIn.hdr.dataLength));
		reinterpret_cast<char *>(transferBuffer)[msgIn.hdr.dataLength] = 0;
	}

	*cmd = msgIn.hdr.command;

	return msgIn.hdr.dataLength;
}

// This is called when the SAM is asking to transfer data
static void ICACHE_RAM_ATTR ProcessRequest()
{
	NetworkCommand cmd = NetworkCommand::nullCommand;

	// activate spi CS
	gpio_set_level(SamCsPin, 0);
	vTaskDelay(10 / portTICK_PERIOD_MS);

	// Begin the transaction
	hspi.beginTransaction();

#if 1
	int result = ReceiveRequest(&cmd, transferBuffer, sizeof(transferBuffer));
	if (result < 0)
	{
		SendErrorResponse(result);
		return;
	}
#endif
	int32_t resp = ResponseUnknownCommand;
	int len;

	len = result;
	if (result == 0)
	{
		// response is expected
		len = sizeof(transferBuffer);
	}

	debug("received cmd %d len %d res %d.\n", (int)cmd, len, result);

#if 1
	if (cmd >= NetworkCommand::connMin && cmd <= NetworkCommand::connMax)
	{
		resp = ProcessConnRequest(cmd, transferBuffer, len);
	}
	else if (cmd >= NetworkCommand::networkWifiMin && cmd <= NetworkCommand::networkWifiMax)
	{
		resp = ProcessWifiRequest(wifiClient, cmd, transferBuffer, len);
	}
	else if (cmd >= NetworkCommand::networkMiscMin && cmd <= NetworkCommand::networkMiscMax)
	{
		resp = ProcessMiscRequest(cmd, transferBuffer, len);
	}
#endif

	debug("processed cmd %d len %d resp %d\n", (int)cmd, (int)len, resp);

	if (resp <= 0)
	{
		SendErrorResponse(resp);
		if (resp < 0)
			err("error processing command %d resp %d\n", static_cast<int>(cmd), resp);
	}
	else if (resp > 0)
	{
		debug("cmd %d resp %d\n", (int)cmd, resp);

		SendResponse(transferBuffer, resp);
	}

	hspi.endTransaction();

	// deactivate spi CS
	gpio_set_level(SamCsPin, 1);

}

static void arduino_setup(void)
{
#if 1
	Connection::Init();
	Listener::Init();
#else
	WiFi.mode(WIFI_OFF);
	WiFi.persistent(false);

	// If we started abnormally, send the exception details to the serial port
	const rst_info *resetInfo = system_get_rst_info();
	if (resetInfo->reason != 0 && resetInfo->reason != 6)	// if not power up or external reset
	{
		info("Restart after exception:%d flag:%d epc1:0x%08x epc2:0x%08x epc3:0x%08x excvaddr:0x%08x depc:0x%08x\n",
			resetInfo->exccause, resetInfo->reason, resetInfo->epc1, resetInfo->epc2, resetInfo->epc3, resetInfo->excvaddr, resetInfo->depc);
	}

	// Reserve some flash space for use as EEPROM. The maximum EEPROM supported by the core is SPI_FLASH_SEC_SIZE (4Kb).
	const size_t eepromSizeNeeded = (MaxRememberedNetworks + 1) * sizeof(WirelessConfigurationData);
	static_assert(eepromSizeNeeded <= SPI_FLASH_SEC_SIZE, "Insufficient EEPROM");
	EEPROM.begin(eepromSizeNeeded);

    Connection::Init();
    Listener::Init();
    mdns_resp_init();
	for (struct netif *item = netif_list; item != nullptr; item = item->next)
	{
		mdns_resp_add_netif(item, webHostName, MdnsTtl);
	}
    netbiosns_init();
    lastError = nullptr;
    debug("Init completed\n");
	attachInterrupt(SamTfrReadyPin, TransferReadyIsr, CHANGE);
	lastSpiTransferTime = millis();
	lastStatusReportTime = millis();
	digitalWrite(EspReqTransferPin, HIGH);				// tell the SAM we are ready to receive a command
#endif
}

static void arduino_loop()
{
#if 1
#else
	digitalWrite(EspReqTransferPin, HIGH);				// tell the SAM we are ready to receive a command
	system_soft_wdt_feed();								// kick the watchdog

	if (   (lastError != prevLastError || connectErrorChanged || currentState != prevCurrentState)
		|| ((lastError != nullptr || currentState != lastReportedState) && millis() - lastStatusReportTime > StatusReportMillis)
	   )
	{
		delayMicroseconds(2);							// make sure the pin stays high for long enough for the SAM to see it
		digitalWrite(EspReqTransferPin, LOW);			// force a low to high transition to signal that an error message is available
		delayMicroseconds(2);							// make sure it is low enough to create an interrupt when it goes high
		digitalWrite(EspReqTransferPin, HIGH);			// tell the SAM we are ready to receive a command
		prevLastError = lastError;
		prevCurrentState = currentState;
		connectErrorChanged = false;
		lastStatusReportTime = millis();
	}

	// See whether there is a request from the SAM.
	// Duet WiFi 1.04 and earlier have hardware to ensure that TransferReady goes low when a transaction starts.
	// Duet 3 Mini doesn't, so we need to see TransferReady go low and then high again. In case that happens so fast that we dn't get the interrupt, we have a timeout.
	if (digitalRead(SamTfrReadyPin) == HIGH && (transferReadyChanged || millis() - lastSpiTransferTime > TransferReadyTimeout))
	{
		transferReadyChanged = false;
		ProcessRequest();
		lastSpiTransferTime = millis();
	}

	ConnectPoll();
	Connection::PollOne();

	if (currentState == WifiState::runningAsAccessPoint)
	{
		dns.processNextRequest();
	}
#endif
}

#else
#endif

// End
