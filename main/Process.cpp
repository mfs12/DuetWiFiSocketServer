#include "Process.h"

int32_t ProcessConnRequest(NetworkCommand cmd, uint32_t *buffer, size_t size)
{
	return ResponseUnknownCommand;
}

int32_t ProcessWifiRequest(NetworkCommand cmd, uint32_t *buffer, size_t size)
{
#if 0
	switch (cmd)
	{
		case NetworkCommand::networkGetStatus:				// get the network connection status
			{
				const bool runningAsAp = (currentState == WiFiState::runningAsAccessPoint);
				const bool runningAsStation = (currentState == WiFiState::connected);
				NetworkStatusResponse * const response = reinterpret_cast<NetworkStatusResponse*>(buffer);

				if (size < sizeof(*response))
				{
					return ResponseBufferTooSmall;
				}
#if 1
				memset(response, 0, sizeof(*response));
				SafeStrncpy(response->versionText, "hallo world", strlen("hallo world"));
#else
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
#endif
				return sizeof(*response);
			}
			break;
		default:
			break;
	}
#endif
	return ResponseUnknownCommand;
}

int32_t ProcessMiscRequest(NetworkCommand cmd, uint32_t *buffer, size_t size)
{
	return ResponseUnknownCommand;
}


#if 0
static void ICACHE_RAM_ATTR ProcessRequest()
{
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
}
#endif
