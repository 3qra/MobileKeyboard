#include "dhcp_server.h"

#include <cstddef>
#include <cstring>

#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

namespace {

constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
constexpr uint8_t kDhcpOpBootRequest = 1;
constexpr uint8_t kDhcpOpBootReply = 2;
constexpr uint8_t kDhcpHtypeEthernet = 1;
constexpr uint32_t kDhcpMagicCookie = 0x63825363;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionServerId = 54;
constexpr uint8_t kOptionLeaseTime = 51;
constexpr uint8_t kOptionSubnetMask = 1;
constexpr uint8_t kOptionRouter = 3;
constexpr uint8_t kOptionDns = 6;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kDhcpDiscover = 1;
constexpr uint8_t kDhcpOffer = 2;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;

struct DhcpPacket {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __attribute__((packed));

uint32_t make_client_ip(const ip4_addr_t &server_ip, const uint8_t *mac) {
    const uint8_t host = static_cast<uint8_t>(2 + (mac[5] % 100));
    return (ip4_addr_get_u32(&server_ip) & PP_HTONL(0xffffff00UL)) | PP_HTONL(host);
}

uint8_t get_message_type(const DhcpPacket &packet, uint16_t len) {
    if (len <= offsetof(DhcpPacket, options)) {
        return 0;
    }

    const uint16_t options_len = len - offsetof(DhcpPacket, options);
    uint16_t i = 0;
    while (i < options_len) {
        const uint8_t option = packet.options[i++];
        if (option == kOptionEnd) {
            break;
        }
        if (option == 0) {
            continue;
        }
        if (i >= options_len) {
            break;
        }
        const uint8_t option_len = packet.options[i++];
        if (i + option_len > options_len) {
            break;
        }
        if (option == kOptionMessageType && option_len == 1) {
            return packet.options[i];
        }
        i += option_len;
    }

    return 0;
}

void add_option_u8(uint8_t *options, size_t &offset, uint8_t option, uint8_t value) {
    options[offset++] = option;
    options[offset++] = 1;
    options[offset++] = value;
}

void add_option_u32(uint8_t *options, size_t &offset, uint8_t option, uint32_t value) {
    options[offset++] = option;
    options[offset++] = 4;
    std::memcpy(&options[offset], &value, sizeof(value));
    offset += sizeof(value);
}

} // namespace

bool DhcpServer::start(const ip4_addr_t &server_ip) {
    server_ip_ = server_ip;
    pcb_ = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb_ == nullptr) {
        return false;
    }

    if (udp_bind(pcb_, IP_ANY_TYPE, kDhcpServerPort) != ERR_OK) {
        udp_remove(pcb_);
        pcb_ = nullptr;
        return false;
    }

    udp_recv(pcb_, receive_callback, this);
    return true;
}

void DhcpServer::receive_callback(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port) {
    (void) addr;
    (void) port;

    auto *server = static_cast<DhcpServer *>(arg);
    if (server != nullptr && p != nullptr) {
        server->handle_packet(pcb, p);
    }
    if (p != nullptr) {
        pbuf_free(p);
    }
}

void DhcpServer::handle_packet(udp_pcb *pcb, pbuf *p) {
    if (p->tot_len < offsetof(DhcpPacket, options)) {
        return;
    }

    DhcpPacket request{};
    const uint16_t request_len = p->tot_len < sizeof(request) ? p->tot_len : sizeof(request);
    pbuf_copy_partial(p, &request, request_len, 0);

    if (request.op != kDhcpOpBootRequest || request.htype != kDhcpHtypeEthernet ||
        request.hlen != 6 || request.magic != PP_HTONL(kDhcpMagicCookie)) {
        return;
    }

    const uint8_t request_type = get_message_type(request, request_len);
    uint8_t response_type = 0;
    if (request_type == kDhcpDiscover) {
        response_type = kDhcpOffer;
    } else if (request_type == kDhcpRequest) {
        response_type = kDhcpAck;
    } else {
        return;
    }

    DhcpPacket response{};
    response.op = kDhcpOpBootReply;
    response.htype = kDhcpHtypeEthernet;
    response.hlen = 6;
    response.xid = request.xid;
    response.flags = request.flags;
    response.yiaddr = make_client_ip(server_ip_, request.chaddr);
    response.siaddr = ip4_addr_get_u32(&server_ip_);
    std::memcpy(response.chaddr, request.chaddr, sizeof(response.chaddr));
    response.magic = PP_HTONL(kDhcpMagicCookie);

    size_t offset = 0;
    add_option_u8(response.options, offset, kOptionMessageType, response_type);
    add_option_u32(response.options, offset, kOptionServerId, ip4_addr_get_u32(&server_ip_));
    add_option_u32(response.options, offset, kOptionLeaseTime, PP_HTONL(24 * 60 * 60));
    add_option_u32(response.options, offset, kOptionSubnetMask, PP_HTONL(0xffffff00UL));
    add_option_u32(response.options, offset, kOptionRouter, ip4_addr_get_u32(&server_ip_));
    add_option_u32(response.options, offset, kOptionDns, ip4_addr_get_u32(&server_ip_));
    response.options[offset++] = kOptionEnd;

    pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, offsetof(DhcpPacket, options) + offset, PBUF_RAM);
    if (reply == nullptr) {
        return;
    }

    pbuf_take(reply, &response, offsetof(DhcpPacket, options) + offset);

    ip_addr_t broadcast;
    IP_ADDR4(&broadcast, 255, 255, 255, 255);

    netif *input_netif = ip_current_input_netif();
    if (input_netif != nullptr) {
        udp_sendto_if(pcb, reply, &broadcast, kDhcpClientPort, input_netif);
    } else {
        udp_sendto(pcb, reply, &broadcast, kDhcpClientPort);
    }
    pbuf_free(reply);
}
