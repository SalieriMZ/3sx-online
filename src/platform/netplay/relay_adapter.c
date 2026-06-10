#if NETPLAY_ENABLED

#include "platform/netplay/relay_adapter.h"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#define MAX_NETWORK_RESULTS 128
#define RELAY_HEADER_BYTES 37
#define RELAY_SEND_BUF_BYTES 2048

static NET_DatagramSocket* adapter_sock = NULL;
static GekkoNetAdapter adapter;
static GekkoNetResult* results[MAX_NETWORK_RESULTS];
static int result_count = 0;

static char cfg_match_uuid[37];
static int  cfg_local_side_int = 1;
static char cfg_local_side_byte = '1';
static NET_Address* relay_addr = NULL;
static Uint16 relay_port_cached = 0;
static char send_buf[RELAY_SEND_BUF_BYTES];

static void send_data(GekkoNetAddress* addr, const char* data, int length) {
    (void)addr;
    if (adapter_sock == NULL || relay_addr == NULL) {
        return;
    }
    if (length < 0 || length + RELAY_HEADER_BYTES > (int)sizeof(send_buf)) {
        SDL_Log("RelayAdapter: packet too large (%d bytes)", length);
        return;
    }

    switch (NET_GetAddressStatus(relay_addr)) {
    case NET_SUCCESS:
        break;
    case NET_FAILURE:
        NET_UnrefAddress(relay_addr);
        relay_addr = NULL;
        return;
    case NET_WAITING: // still resolving, skip — GekkoNet will retransmit
        return;
    }

    SDL_memcpy(send_buf, cfg_match_uuid, 36);
    send_buf[36] = cfg_local_side_byte;
    SDL_memcpy(send_buf + RELAY_HEADER_BYTES, data, (size_t)length);
    NET_SendDatagram(adapter_sock, relay_addr, relay_port_cached,
                     send_buf, length + RELAY_HEADER_BYTES);
}

static GekkoNetResult** receive_data(int* length) {
    result_count = 0;

    if (adapter_sock == NULL) {
        *length = 0;
        return results;
    }

    NET_Datagram* dgram = NULL;

    while (result_count < MAX_NETWORK_RESULTS
           && NET_ReceiveDatagram(adapter_sock, &dgram) && dgram) {
        if (dgram->buflen >= RELAY_HEADER_BYTES
            && SDL_memcmp(dgram->buf, cfg_match_uuid, 36) == 0) {
            int payload_len = (int)dgram->buflen - RELAY_HEADER_BYTES;
            const Uint8* payload = dgram->buf + RELAY_HEADER_BYTES;
            int other_side = (cfg_local_side_int == 1) ? 2 : 1;

            char addr_str[16];
            SDL_snprintf(addr_str, sizeof(addr_str), "peer-%d", other_side);

            GekkoNetResult* res = SDL_malloc(sizeof(GekkoNetResult));
            size_t addr_len = SDL_strlen(addr_str);

            res->addr.data = SDL_malloc(addr_len + 1);
            SDL_strlcpy((char*)res->addr.data, addr_str, addr_len + 1);
            res->addr.size = (unsigned int)addr_len;

            res->data = SDL_malloc((size_t)payload_len);
            SDL_memcpy(res->data, payload, (size_t)payload_len);
            res->data_len = (unsigned int)payload_len;

            results[result_count++] = res;
        }
        NET_DestroyDatagram(dgram);
        dgram = NULL;
    }

    *length = result_count;
    return results;
}

static void free_data(void* ptr) {
    SDL_free(ptr);
}

GekkoNetAdapter* RelayAdapter_Create(
    NET_DatagramSocket* sock,
    const char* relay_ip,
    int relay_port,
    const char* match_uuid,
    int local_side
) {
    adapter_sock = sock;
    SDL_strlcpy(cfg_match_uuid, match_uuid, sizeof(cfg_match_uuid));
    cfg_local_side_int = local_side;
    cfg_local_side_byte = (local_side == 2) ? '2' : '1';
    if (relay_addr != NULL) {
        NET_UnrefAddress(relay_addr);
    }
    relay_addr = NET_ResolveHostname(relay_ip);
    relay_port_cached = (Uint16)relay_port;

    adapter.send_data = send_data;
    adapter.receive_data = receive_data;
    adapter.free_data = free_data;
    return &adapter;
}

void RelayAdapter_Destroy(void) {
    adapter_sock = NULL;
    if (relay_addr != NULL) {
        NET_UnrefAddress(relay_addr);
        relay_addr = NULL;
    }
    relay_port_cached = 0;
}

#endif
