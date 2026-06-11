#pragma once

#include <cstdint>

#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

class DhcpServer {
public:
    bool start(const ip4_addr_t &server_ip);

private:
    static void receive_callback(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port);
    void handle_packet(udp_pcb *pcb, pbuf *p);

    udp_pcb *pcb_ = nullptr;
    ip4_addr_t server_ip_{};
};
