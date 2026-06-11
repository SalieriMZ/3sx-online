#include "port/io/local_ip.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(__vita__)

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

bool LocalIP_GetPrimary(char* out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    SceNetCtlInfo info;
    SDL_zero(info);
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) {
        return false;
    }
    if (info.ip_address[0] == '\0' || SDL_strcmp(info.ip_address, "0.0.0.0") == 0) {
        return false;
    }
    SDL_strlcpy(out, info.ip_address, out_size);
    return true;
}

#elif defined(__ANDROID__)

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

bool LocalIP_GetPrimary(char* out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    struct ifaddrs* head = NULL;
    if (getifaddrs(&head) != 0 || head == NULL) {
        return false;
    }

    char wifi_ip[INET_ADDRSTRLEN] = { 0 };
    char fallback_ip[INET_ADDRSTRLEN] = { 0 };

    for (struct ifaddrs* it = head; it != NULL; it = it->ifa_next) {
        if (it->ifa_addr == NULL || it->ifa_addr->sa_family != AF_INET) continue;
        if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;

        const struct sockaddr_in* sa = (const struct sockaddr_in*)it->ifa_addr;
        char ip[INET_ADDRSTRLEN] = { 0 };
        if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip)) == NULL) continue;
        if (SDL_strncmp(ip, "169.254.", 8) == 0 || SDL_strcmp(ip, "0.0.0.0") == 0) continue;

        const char* name = it->ifa_name ? it->ifa_name : "";
        const bool is_wifi = (SDL_strncmp(name, "wlan", 4) == 0) ||
                             (SDL_strncmp(name, "ap", 2) == 0) ||
                             (SDL_strncmp(name, "eth", 3) == 0);
        if (is_wifi && wifi_ip[0] == '\0') {
            SDL_strlcpy(wifi_ip, ip, sizeof(wifi_ip));
        } else if (fallback_ip[0] == '\0') {
            SDL_strlcpy(fallback_ip, ip, sizeof(fallback_ip));
        }
    }

    freeifaddrs(head);

    const char* pick = wifi_ip[0] != '\0' ? wifi_ip : (fallback_ip[0] != '\0' ? fallback_ip : NULL);
    if (pick == NULL) {
        return false;
    }
    SDL_strlcpy(out, pick, out_size);
    return true;
}

#else

bool LocalIP_GetPrimary(char* out, size_t out_size) {
    (void)out;
    (void)out_size;
    return false;
}

#endif
