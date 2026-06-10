#if NETPLAY_ENABLED

#ifndef NETPLAY_RELAY_ADAPTER_H
#define NETPLAY_RELAY_ADAPTER_H

#include "gekkonet.h"

struct NET_DatagramSocket;

// Wraps a UDP socket as a GekkoNetAdapter that prepends a 36+1=37 byte
// header (match UUID + local side) on send and strips/validates the header
// on receive. All sends target the relay endpoint (server's :19002).
// Received packets are validated against the expected match UUID.
GekkoNetAdapter* RelayAdapter_Create(
    struct NET_DatagramSocket* sock,
    const char* relay_ip,
    int relay_port,
    const char* match_uuid,
    int local_side  // 1 or 2
);

void RelayAdapter_Destroy(void);

#endif

#endif
