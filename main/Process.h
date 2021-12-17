#ifndef PROCESS_H
#define PROCESS_H 1

#include <cstdint>
#include "include/MessageFormats.h"
#include "WifiClient.h"

int32_t ProcessConnRequest(NetworkCommand cmd, uint32_t *buffer, size_t size);
int32_t ProcessWifiRequest(WifiClient *client, NetworkCommand cmd, uint32_t *buffer, size_t size);
int32_t ProcessMiscRequest(NetworkCommand cmd, uint32_t *buffer, size_t size);

#endif /* ifndef PROCESS_H */
