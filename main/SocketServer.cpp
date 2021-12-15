/*
 * SocketServer.cpp
 *
 *  Created on: 25 Mar 2017
 *      Author: David
 */


#include "Config.h"

#if SOCKETSERVER_ENABLE

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "DwssSpiffs.h"
#include "TcpServer.h"
#include "WifiClient.h"

#if 0

#include "ecv.h"
#undef yield
#undef array
#define array _ecv_array

#else

#define pre(...)
#define array

#endif

#include <cstdarg>
#include "HSPI.h"

#include "arduino/gpio.h"

#include "include/MessageFormats.h"
#include "Connection.h"
#include "Listener.h"
#include "Misc.h"

#define millis xTaskGetTickCount

static const gpio_num_t ONBOARD_LED = GPIO_NUM_2;	// active low
static const bool ONBOARD_LED_ON = false;
static const uint32_t ONBOARD_LED_BLINK_INTERVAL = 500;	// ms
static const uint32_t TransferReadyTimeout = 10;			// how many milliseconds we allow for the Duet to set TransferReady low after the end of a transaction, before we assume that we missed seeing it

// Pin numbers
static const gpio_num_t SamCsPin = GPIO_NUM_15;          // output to SAM, SS pin for SPI transfer, active low
static const gpio_num_t EspReqTransferPin = GPIO_NUM_0;  // output, indicates to the SAM that we want to send something
static const gpio_num_t SamTfrReadyPin = GPIO_NUM_4;     // input, indicates that SAM is ready to execute an SPI transaction


static const char * const MdnsProtocolNames[3] = { "HTTP", "FTP", "Telnet" };
static const char * const MdnsServiceStrings[3] = { "_http", "_ftp", "_telnet" };
static const char * const MdnsTxtRecords[2] = { "product=DuetWiFi", "version=" VERSION_MAIN };
static const unsigned int MdnsTtl = 10 * 60;			// same value as on the Duet 0.6/0.8.5

static const uint32_t MaxConnectTime = 40 * 1000;		// how long we wait for WiFi to connect in milliseconds
static const uint32_t StatusReportMillis = 200;

static const int DefaultWiFiChannel = 6;

// Global data
static char currentSsid[SsidLength + 1];
static char webHostName[HostNameLength + 1] = "Duet-WiFi";

static const char* lastError = nullptr;
static const char* prevLastError = nullptr;
static uint32_t whenLastTransactionFinished = 0;
static bool connectErrorChanged = false;
static bool transferReadyChanged = false;

static char lastConnectError[100];

static WiFiState currentState = WiFiState::idle,
				prevCurrentState = WiFiState::disabled,
				lastReportedState = WiFiState::disabled;
static uint32_t lastBlinkTime = 0;

ADC_MODE(ADC_VCC);          // need this for the ESP.getVcc() call to work

static HSPIClass hspi;
static uint32_t connectStartTime;
static uint32_t lastStatusReportTime;
static uint32_t transferBuffer[NumDwords(MaxDataLength + 1)];


static const WirelessConfigurationData *ssidData = nullptr;

extern "C" {
	void app_main(void);
}

static void arduino_setup(void);
static void arduino_loop(void);

static int app_init(void)
{
	// TODO
	// init led gpio
	// init spi interrupt pins
	// init spi interface

#if 0
	wifi_init_sta();
	dwss_spiffs_init();
	TcpServer_init();
#else
	arduino_setup();
#endif

	return 0;
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

	vTaskDelay(1000 / portTICK_PERIOD_MS);

	bool led = false;
#if 1

	gpio_set_level(SamCsPin, 1);
	gpio_set_level(EspReqTransferPin, 1);

	for (;;) {
		debug("led test %d\n", led);
		//digitalWrite(ONBOARD_LED, led);
		gpio_set_level(ONBOARD_LED, led ? 0 : 1);
		led = !led;
		gpio_set_level(EspReqTransferPin, led);
		//arduino_loop();
		if (transferReadyChanged) {
			debug("ready pin changed\n");
			transferReadyChanged = false;
		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
#else
	for (int i = 10; i >= 0; i--) {
		digitalWrite(ONBOARD_LED, led);
		led = !led;
		info("Restarting in %d seconds...\n", i);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		digitalWrite(ONBOARD_LED, led);
		led = !led;
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
#endif
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
		currentState = WiFiState::reconnecting;
	}
	else
	{
		currentState = WiFiState::connecting;
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
	case WiFiState::connecting:
	case WiFiState::reconnecting:
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
			retry = (currentState == WiFiState::reconnecting);
			break;

		case STATION_CONNECT_FAIL:
			error = "Failed";
			retry = (currentState == WiFiState::reconnecting);
			break;

		case STATION_GOT_IP:
			if (currentState == WiFiState::reconnecting)
			{
				lastError = "Reconnect succeeded";
			}
			else
			{
				mdns_resp_netif_settings_changed(netif_list);	// STA is on first interface
			}

			debug("Connected to AP\n");
			currentState = WiFiState::connected;
			digitalWrite(ONBOARD_LED, ONBOARD_LED_ON);
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
				currentState = WiFiState::idle;
				digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
			}
		}
		break;

	case WiFiState::connected:
		if (status != STATION_GOT_IP)
		{
			// We have just lost the connection
			connectStartTime = millis();						// start the auto reconnect timer

			switch (status)
			{
			case STATION_CONNECTING:							// auto reconnecting
				error = "auto reconnecting";
				currentState = WiFiState::autoReconnecting;
				break;

			case STATION_IDLE:
				error = "state 'idle'";
				retry = true;
				break;

			case STATION_WRONG_PASSWORD:
				error = "state 'wrong password'";
				currentState = WiFiState::idle;
				digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
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
				currentState = WiFiState::idle;
				digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
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

	case WiFiState::autoReconnecting:
		if (status == STATION_GOT_IP)
		{
			lastError = "Auto reconnect succeeded";
			currentState = WiFiState::connected;
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
pre(currentState == WiFiState::idle)
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
			currentState = WiFiState::idle;
			digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
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
			currentState = WiFiState::runningAsAccessPoint;
			digitalWrite(ONBOARD_LED, ONBOARD_LED_ON);
			mdns_resp_netif_settings_changed(netif_list->next);		// AP is on second interface
		}
		else
		{
			WiFi.mode(WIFI_OFF);
			lastError = "Failed to start access point";
			debug("%s\n", lastError);
			currentState = WiFiState::idle;
			digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
		}
	}
	else
	{
		lastError = "invalid access point configuration";
		debug("%s\n", lastError);
		currentState = WiFiState::idle;
		digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
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

// This is called when the SAM is asking to transfer data
static void ICACHE_RAM_ATTR ProcessRequest()
{
#if 1
	static union
	{
		MessageHeaderSamToEsp hdr;			// the actual header
		uint32_t asDwords[headerDwords];	// to force alignment
	} messageHeaderIn;

	static union
	{
		MessageHeaderEspToSam hdr;
		uint32_t asDwords[headerDwords];	// to force alignment
	} messageHeaderOut;

	messageHeaderIn.hdr.formatVersion = InvalidFormatVersion;
	messageHeaderOut.hdr.formatVersion = MyFormatVersion;
	messageHeaderOut.hdr.state = currentState;

	// Begin the transaction
	digitalWrite(SamCsPin, LOW);            // assert CS to SAM
	hspi.beginTransaction();

	// Exchange headers, except for the last dword which will contain our response
	hspi.transferDwords(messageHeaderOut.asDwords, messageHeaderIn.asDwords, headerDwords - 1);

	if (messageHeaderIn.hdr.formatVersion != MyFormatVersion)
	{
		SendErrorResponse(ResponseBadRequestFormatVersion);
	}
	else if (messageHeaderIn.hdr.dataLength > MaxDataLength)
	{
		SendErrorResponse(ResponseBadDataLength);
	}
	else
	{
		SendErrorResponse(ResponseUnknownCommand);
	}

	hspi.endTransaction();

#else
	// Set up our own headers
	messageHeaderIn.hdr.formatVersion = InvalidFormatVersion;
	messageHeaderOut.hdr.formatVersion = MyFormatVersion;
	messageHeaderOut.hdr.state = currentState;
	bool deferCommand = false;

	// Begin the transaction
	digitalWrite(SamCsPin, LOW);            // assert CS to SAM
	hspi.beginTransaction();

	// Exchange headers, except for the last dword which will contain our response
	hspi.transferDwords(messageHeaderOut.asDwords, messageHeaderIn.asDwords, headerDwords - 1);

	if (messageHeaderIn.hdr.formatVersion != MyFormatVersion)
	{
		SendErrorResponse(ResponseBadRequestFormatVersion);
	}
	else if (messageHeaderIn.hdr.dataLength > MaxDataLength)
	{
		SendErrorResponse(ResponseBadDataLength);
	}
	else
	{
		const size_t dataBufferAvailable = std::min<size_t>(messageHeaderIn.hdr.dataBufferAvailable, MaxDataLength);

		// See what command we have received and take appropriate action
		switch (messageHeaderIn.hdr.command)
		{
		case NetworkCommand::nullCommand:					// no command being sent, SAM just wants the network status
			SendErrorResponse(ResponseEmpty);
			break;

		case NetworkCommand::networkStartClient:			// connect to an access point
			if (currentState == WiFiState::idle)
			{
				deferCommand = true;
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				if (messageHeaderIn.hdr.dataLength != 0 && messageHeaderIn.hdr.dataLength <= SsidLength + 1)
				{
					hspi.transferDwords(nullptr, transferBuffer, NumDwords(messageHeaderIn.hdr.dataLength));
					reinterpret_cast<char *>(transferBuffer)[messageHeaderIn.hdr.dataLength] = 0;
				}
			}
			else
			{
				SendErrorResponse(ResponseWrongState);
			}
			break;

		case NetworkCommand::networkStartAccessPoint:		// run as an access point
			if (currentState == WiFiState::idle)
			{
				deferCommand = true;
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			}
			else
			{
				SendErrorResponse(ResponseWrongState);
			}
			break;

		case NetworkCommand::networkFactoryReset:			// clear remembered list, reset factory defaults
			deferCommand = true;
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			break;

		case NetworkCommand::networkStop:					// disconnect from an access point, or close down our own access point
			deferCommand = true;
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			break;

		case NetworkCommand::networkGetStatus:				// get the network connection status
			{
				const bool runningAsAp = (currentState == WiFiState::runningAsAccessPoint);
				const bool runningAsStation = (currentState == WiFiState::connected);
				NetworkStatusResponse * const response = reinterpret_cast<NetworkStatusResponse*>(transferBuffer);
				response->ipAddress = (runningAsAp)
										? static_cast<uint32_t>(WiFi.softAPIP())
										: (runningAsStation)
										  ? static_cast<uint32_t>(WiFi.localIP())
											  : 0;
				response->freeHeap = system_get_free_heap_size();
				response->resetReason = system_get_rst_info()->reason;
				response->flashSize = 1u << ((spi_flash_get_id() >> 16) & 0xFF);
				response->rssi = (runningAsStation) ? wifi_station_get_rssi() : 0;
				response->numClients = (runningAsAp) ? wifi_softap_get_station_num() : 0;
				response->sleepMode = (uint8_t)wifi_get_sleep_type() + 1;
				response->phyMode = (uint8_t)wifi_get_phy_mode();
				response->zero1 = 0;
				response->zero2 = 0;
				response->vcc = system_get_vdd33();
				wifi_get_macaddr((runningAsAp) ? SOFTAP_IF : STATION_IF, response->macAddress);
				SafeStrncpy(response->versionText, firmwareVersion, sizeof(response->versionText));
				SafeStrncpy(response->hostName, webHostName, sizeof(response->hostName));
				SafeStrncpy(response->ssid, currentSsid, sizeof(response->ssid));
				response->clockReg = SPI1CLK;
				SendErrorResponse(sizeof(NetworkStatusResponse));
			}
			break;

		case NetworkCommand::networkAddSsid:				// add to our known access point list
		case NetworkCommand::networkConfigureAccessPoint:	// configure our own access point details
			if (messageHeaderIn.hdr.dataLength == sizeof(WirelessConfigurationData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(sizeof(WirelessConfigurationData)));
				const WirelessConfigurationData * const receivedClientData = reinterpret_cast<const WirelessConfigurationData *>(transferBuffer);
				int index;
				if (messageHeaderIn.hdr.command == NetworkCommand::networkConfigureAccessPoint)
				{
					index = 0;
				}
				else
				{
					index = -1;
					(void)RetrieveSsidData(receivedClientData->ssid, &index);
					if (index < 0)
					{
						(void)FindEmptySsidEntry(&index);
					}
				}

				if (index >= 0)
				{
					EEPROM.put(index * sizeof(WirelessConfigurationData), *receivedClientData);
					EEPROM.commit();
				}
				else
				{
					lastError = "SSID table full";
				}
			}
			else
			{
				SendErrorResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkDeleteSsid:				// delete a network from our access point list
			if (messageHeaderIn.hdr.dataLength == SsidLength)
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(SsidLength));

				int index;
				if (RetrieveSsidData(reinterpret_cast<char*>(transferBuffer), &index) != nullptr)
				{
					WirelessConfigurationData localSsidData;
					memset(&localSsidData, 0xFF, sizeof(localSsidData));
					EEPROM.put(index * sizeof(WirelessConfigurationData), localSsidData);
					EEPROM.commit();
				}
				else
				{
					lastError = "SSID not found";
				}
			}
			else
			{
				SendErrorResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkRetrieveSsidData:	// list the access points we know about, including our own access point details
			if (dataBufferAvailable < ReducedWirelessConfigurationDataSize)
			{
				SendErrorResponse(ResponseBufferTooSmall);
			}
			else
			{
				char *p = reinterpret_cast<char*>(transferBuffer);
				for (size_t i = 0; i <= MaxRememberedNetworks && (i + 1) * ReducedWirelessConfigurationDataSize <= dataBufferAvailable; ++i)
				{
					const WirelessConfigurationData * const tempData = EEPROM.getPtr<WirelessConfigurationData>(i * sizeof(WirelessConfigurationData));
					if (tempData->ssid[0] != 0xFF)
					{
						memcpy(p, tempData, ReducedWirelessConfigurationDataSize);
						p += ReducedWirelessConfigurationDataSize;
					}
					else if (i == 0)
					{
						memset(p, 0, ReducedWirelessConfigurationDataSize);
						p += ReducedWirelessConfigurationDataSize;
					}
				}
				const size_t numBytes = p - reinterpret_cast<char*>(transferBuffer);
				SendResponse(transferBuffer, numBytes);
			}
			break;

		case NetworkCommand::networkListSsids_deprecated:	// list the access points we know about, plus our own access point details
			{
				char *p = reinterpret_cast<char*>(transferBuffer);
				for (size_t i = 0; i <= MaxRememberedNetworks; ++i)
				{
					const WirelessConfigurationData * const tempData = EEPROM.getPtr<WirelessConfigurationData>(i * sizeof(WirelessConfigurationData));
					if (tempData->ssid[0] != 0xFF)
					{
						for (size_t j = 0; j < SsidLength && tempData->ssid[j] != 0; ++j)
						{
							*p++ = tempData->ssid[j];
						}
						*p++ = '\n';
					}
					else if (i == 0)
					{
						// Include an empty entry for our own access point SSID
						*p++ = '\n';
					}
				}
				*p++ = 0;
				const size_t numBytes = p - reinterpret_cast<char*>(transferBuffer);
				if (numBytes <= dataBufferAvailable)
				{
					SendResponse(transferBuffer, numBytes);
				}
				else
				{
					SendErrorResponse(ResponseBufferTooSmall);
				}
			}
			break;

		case NetworkCommand::networkSetHostName:			// set the host name
			if (messageHeaderIn.hdr.dataLength == HostNameLength)
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(HostNameLength));
				memcpy(webHostName, transferBuffer, HostNameLength);
				webHostName[HostNameLength] = 0;			// ensure null terminator
				netbiosns_set_name(webHostName);
			}
			else
			{
				SendErrorResponse(ResponseBadDataLength);
			}
			break;

		case NetworkCommand::networkGetLastError:
			if (lastError == nullptr)
			{
				SendErrorResponse(ResponseEmpty);
			}
			else
			{
				const size_t len = strlen(lastError) + 1;
				if (dataBufferAvailable >= len)
				{
					strcpy(reinterpret_cast<char*>(transferBuffer), lastError);		// copy to 32-bit aligned buffer
					SendResponse(transferBuffer, len);
				}
				else
				{
					SendErrorResponse(ResponseBufferTooSmall);
				}
				lastError = nullptr;
			}
			lastReportedState = currentState;
			break;

		case NetworkCommand::networkListen:				// listen for incoming connections
			if (messageHeaderIn.hdr.dataLength == sizeof(ListenOrConnectData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				ListenOrConnectData lcData;
				hspi.transferDwords(nullptr, reinterpret_cast<uint32_t*>(&lcData), NumDwords(sizeof(lcData)));
				const bool ok = Listener::Listen(lcData.remoteIp, lcData.port, lcData.protocol, lcData.maxConnections);
				if (ok)
				{
					if (lcData.protocol < 3)			// if it's FTP, HTTP or Telnet protocol
					{
						MdnsRebuildServices();				// update the MDNS services
					}
					debug("%sListening on port %u\n", (lcData.maxConnections == 0) ? "Stopped " : "", lcData.port);
				}
				else
				{
					lastError = "Listen failed";
					debug("Listen failed\n");
				}
			}
			break;

#if 0	// We don't use the following command, instead we use networkListen with maxConnections = 0
		case NetworkCommand::unused_networkStopListening:
			if (messageHeaderIn.hdr.dataLength == sizeof(ListenOrConnectData))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				ListenOrConnectData lcData;
				hspi.transferDwords(nullptr, reinterpret_cast<uint32_t*>(&lcData), NumDwords(sizeof(lcData)));
				Listener::StopListening(lcData.port);
				MdnsRebuildServices();						// update the MDNS services
				debug("Stopped listening on port %u\n", lcData.port);
			}
			break;
#endif

		case NetworkCommand::connAbort:					// terminate a socket rudely
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				Connection::Get(messageHeaderIn.hdr.socketNumber).Terminate(true);
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connClose:					// close a socket gracefully
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
				Connection::Get(messageHeaderIn.hdr.socketNumber).Close();
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connRead:					// read data from a connection
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				const size_t amount = conn.Read(reinterpret_cast<uint8_t *>(transferBuffer), std::min<size_t>(messageHeaderIn.hdr.dataBufferAvailable, MaxDataLength));
				messageHeaderIn.hdr.param32 = hspi.transfer32(amount);
				hspi.transferDwords(transferBuffer, nullptr, NumDwords(amount));
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connWrite:					// write data to a connection
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				const size_t requestedlength = messageHeaderIn.hdr.dataLength;
				const size_t acceptedLength = std::min<size_t>(conn.CanWrite(), std::min<size_t>(requestedlength, MaxDataLength));
				const bool closeAfterSending = (acceptedLength == requestedlength) && (messageHeaderIn.hdr.flags & MessageHeaderSamToEsp::FlagCloseAfterWrite) != 0;
				const bool push = (acceptedLength == requestedlength) && (messageHeaderIn.hdr.flags & MessageHeaderSamToEsp::FlagPush) != 0;
				messageHeaderIn.hdr.param32 = hspi.transfer32(acceptedLength);
				hspi.transferDwords(nullptr, transferBuffer, NumDwords(acceptedLength));
				const size_t written = conn.Write(reinterpret_cast<uint8_t *>(transferBuffer), acceptedLength, push, closeAfterSending);
				if (written != acceptedLength)
				{
					lastError = "incomplete write";
				}
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::connGetStatus:				// get the status of a socket, and summary status for all sockets
			if (ValidSocketNumber(messageHeaderIn.hdr.socketNumber))
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(sizeof(ConnStatusResponse));
				Connection& conn = Connection::Get(messageHeaderIn.hdr.socketNumber);
				ConnStatusResponse resp;
				conn.GetStatus(resp);
				Connection::GetSummarySocketStatus(resp.connectedSockets, resp.otherEndClosedSockets);
				hspi.transferDwords(reinterpret_cast<const uint32_t *>(&resp), nullptr, NumDwords(sizeof(resp)));
			}
			else
			{
				messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseBadParameter);
			}
			break;

		case NetworkCommand::diagnostics:					// print some debug info over the UART line
			SendErrorResponse(ResponseEmpty);
			deferCommand = true;							// we need to send the diagnostics after we have sent the response, so the SAM is ready to receive them
			break;

		case NetworkCommand::networkSetTxPower:
			{
				const uint8_t txPower = messageHeaderIn.hdr.flags;
				if (txPower <= 82)
				{
					system_phy_set_max_tpw(txPower);
					SendErrorResponse(ResponseEmpty);
				}
				else
				{
					SendErrorResponse(ResponseBadParameter);
				}
			}
			break;

		case NetworkCommand::networkSetClockControl:
			messageHeaderIn.hdr.param32 = hspi.transfer32(ResponseEmpty);
			deferCommand = true;
			break;

		case NetworkCommand::connCreate:					// create a connection
			// Not implemented yet
		default:
			SendErrorResponse(ResponseUnknownCommand);
			break;
		}
	}

	digitalWrite(SamCsPin, HIGH);     						// de-assert CS to SAM to end the transaction and tell SAM the transfer is complete
	hspi.endTransaction();

	// If we deferred the command until after sending the response (e.g. because it may take some time to execute), complete it now
	if (deferCommand)
	{
		// The following functions must set up lastError if an error occurs
		lastError = nullptr;								// assume no error
		switch (messageHeaderIn.hdr.command)
		{
		case NetworkCommand::networkStartClient:			// connect to an access point
			if (messageHeaderIn.hdr.dataLength == 0 || reinterpret_cast<const char*>(transferBuffer)[0] == 0)
			{
				StartClient(nullptr);						// connect to strongest known access point
			}
			else
			{
				StartClient(reinterpret_cast<const char*>(transferBuffer));		// connect to specified access point
			}
			break;

		case NetworkCommand::networkStartAccessPoint:		// run as an access point
			StartAccessPoint();
			break;

		case NetworkCommand::networkStop:					// disconnect from an access point, or close down our own access point
			Connection::TerminateAll();						// terminate all connections
			Listener::StopListening(0);						// stop listening on all ports
			MdnsRebuildServices();								// remove the MDNS services
			switch (currentState)
			{
			case WiFiState::connected:
			case WiFiState::connecting:
			case WiFiState::reconnecting:
				MdnsRemoveServices();
				delay(20);									// try to give lwip time to recover from stopping everything
				WiFi.disconnect(true);
				break;

			case WiFiState::runningAsAccessPoint:
				dns.stop();
				delay(20);									// try to give lwip time to recover from stopping everything
				WiFi.softAPdisconnect(true);
				break;

			default:
				break;
			}
			delay(100);
			currentState = WiFiState::idle;
			digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);
			break;

		case NetworkCommand::networkFactoryReset:			// clear remembered list, reset factory defaults
			FactoryReset();
			break;

		case NetworkCommand::diagnostics:
			Connection::ReportConnections();
			delay(20);										// give the Duet main processor time to digest that
			stats_display();
			break;

		case NetworkCommand::networkSetClockControl:
			hspi.setClockDivider(messageHeaderIn.hdr.param32);
			break;

		default:
			lastError = "bad deferred command";
			break;
		}
	}
#endif
}

static void ICACHE_RAM_ATTR TransferReadyIsr()
{
	transferReadyChanged = true;
}

static void gpio_isr_handler(void *arg)
{
	//gpio_num_t gpio = static_cast<gpio_num_t>(arg);
	transferReadyChanged = true;
}

static void arduino_setup(void)
{
	esp_err_t err;

	gpio_config_t led_gpio;
	led_gpio.intr_type = GPIO_INTR_DISABLE;
	led_gpio.mode = GPIO_MODE_OUTPUT;
	led_gpio.pin_bit_mask = BIT(ONBOARD_LED);
	led_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	led_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&led_gpio);
	if (err) {
		err("failed to init led_gpio.\n");
		return;
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
		return;
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
		return;
	}

	gpio_set_level(EspReqTransferPin, 0);

	gpio_config_t receive_gpio;
	receive_gpio.intr_type = GPIO_INTR_ANYEDGE;
	receive_gpio.mode = GPIO_MODE_INPUT;
	receive_gpio.pin_bit_mask = BIT(SamTfrReadyPin);
	receive_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
	receive_gpio.pull_up_en = GPIO_PULLUP_DISABLE;
	err = gpio_config(&receive_gpio);
	if (err) {
		err("failed to init receive_gpio.\n");
		return;
	}

	gpio_install_isr_service(0);
	gpio_isr_handler_add(SamTfrReadyPin, gpio_isr_handler, (void *)SamTfrReadyPin);

#if 1
#if 0
	// Turn off LED
	pinMode(ONBOARD_LED, OUTPUT);
	digitalWrite(ONBOARD_LED, true);

	pinMode(SamTfrReadyPin, INPUT);
	pinMode(EspReqTransferPin, OUTPUT);
	digitalWrite(EspReqTransferPin, LOW);				// not ready to transfer data yet
	pinMode(SamCsPin, OUTPUT);
	digitalWrite(SamCsPin, HIGH);

	hspi.InitMaster(SPI_MODE1, defaultClockControl, true);
#endif
#else
	// Enable serial port for debugging
	Serial.begin(WiFiBaudRate);
	Serial.setDebugOutput(true);

	// Turn off LED
	pinMode(ONBOARD_LED, OUTPUT);
	digitalWrite(ONBOARD_LED, !ONBOARD_LED_ON);

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

	// Set up the SPI subsystem
    pinMode(SamTfrReadyPin, INPUT);
    pinMode(EspReqTransferPin, OUTPUT);
    digitalWrite(EspReqTransferPin, LOW);				// not ready to transfer data yet
    pinMode(SamCsPin, OUTPUT);
    digitalWrite(SamCsPin, HIGH);

    // Set up the fast SPI channel
    hspi.InitMaster(SPI_MODE1, defaultClockControl, true);

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
	whenLastTransactionFinished = millis();
	lastStatusReportTime = millis();
	digitalWrite(EspReqTransferPin, HIGH);				// tell the SAM we are ready to receive a command
#endif
}

static void arduino_loop()
{
#if 1
#if 0
	digitalWrite(EspReqTransferPin, HIGH);				// tell the SAM we are ready to receive a command

	if (digitalRead(SamTfrReadyPin) == HIGH && (transferReadyChanged || millis() - whenLastTransactionFinished > TransferReadyTimeout))
	{
		transferReadyChanged = false;
		ProcessRequest();
		whenLastTransactionFinished = millis();
	}
#endif
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
	if (digitalRead(SamTfrReadyPin) == HIGH && (transferReadyChanged || millis() - whenLastTransactionFinished > TransferReadyTimeout))
	{
		transferReadyChanged = false;
		ProcessRequest();
		whenLastTransactionFinished = millis();
	}

	ConnectPoll();
	Connection::PollOne();

	if (currentState == WiFiState::runningAsAccessPoint)
	{
		dns.processNextRequest();
	}
	else if (	(currentState == WiFiState::autoReconnecting ||
				 currentState == WiFiState::connecting ||
				 currentState == WiFiState::reconnecting) &&
				(millis() - lastBlinkTime > ONBOARD_LED_BLINK_INTERVAL))
	{
		lastBlinkTime = millis();
		digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));
	}
#endif
}

#else
#endif

// End
