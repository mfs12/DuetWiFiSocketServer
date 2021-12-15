// Configuration for RepRapWiFi

#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include <stdint.h>

#define SOCKETSERVER_ENABLE 1
#define CONNECTION_ENABLE 1
#define HSPI_ENABLE 1
#define LISTENER_ENABLE 0

#define NO_WIFI_SLEEP	0

#define VERSION_MAIN	"1.27-pre1"

#if NO_WIFI_SLEEP
#define VERSION_SLEEP	"-nosleep"
#else
#define VERSION_SLEEP	""
#endif

#ifdef DEBUG
#define VERSION_DEBUG	"-D"
#else
#define VERSION_DEBUG	""
#endif

const char* const firmwareVersion = VERSION_MAIN VERSION_DEBUG VERSION_SLEEP;

// Define the maximum length (bytes) of file upload data per SPI packet. Use a multiple of the SD card sector or cluster size for efficiency.
// ************ This must be kept in step with the corresponding value in RepRapFirmware *************
const uint32_t maxSpiFileData = 2048;

// Define the SPI clock register
// Useful values of the register are:
// 0x1001	40MHz 1:1
// 0x2001	26.7MHz 1:2
// 0x2402	26.7MHz 1:2
// 0x2002	26.7MHz 2:1
// 0x3043	20MHz 2:2

// The SAM occasionally transmits incorrect data at 40MHz, so we now use 26.7MHz.
// Due to the 15ns SCLK to MISO delay of the SAMD51, 2:1 is preferred over 1:2
const uint32_t defaultClockControl = 0x2002;		// 80MHz/3, mark:space 2:1

const uint8_t Backlog = 8;

#define ARRAY_SIZE(_x) (sizeof(_x)/sizeof((_x)[0]))

#ifdef DEBUG
#define debug(fmt, args...)	do { printf("DBG %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#else
#define debug(_format, ...)	do {} while(false)
#endif

#define info(fmt, args...)	do { printf("INFO %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#define warn(fmt, args...)	do { printf("WARN %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#define err(fmt, args...)	do { printf("ERR %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)

#endif
