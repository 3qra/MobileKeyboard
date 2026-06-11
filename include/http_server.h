#pragma once

#include <cstddef>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

using TextHandler = void (*)(const char *text);
using KeyHandler = void (*)(char *name);

class HttpServer {
public:
    bool start(TextHandler text_handler, KeyHandler key_handler);
    static void close_connection(tcp_pcb *pcb);

private:
    static err_t accept_callback(void *arg, tcp_pcb *new_pcb, err_t err);
    static err_t receive_callback(void *arg, tcp_pcb *pcb, pbuf *p, err_t err);
    static err_t sent_callback(void *arg, tcp_pcb *pcb, uint16_t len);
    static err_t poll_callback(void *arg, tcp_pcb *pcb);
    static void error_callback(void *arg, err_t err);
    void handle_request(void *client_state);
    void send_response(void *client_state, const char *status, const char *content_type, const char *body);
    void send_redirect(void *client_state);

    TextHandler text_handler_ = nullptr;
    KeyHandler key_handler_ = nullptr;
    tcp_pcb *pcb_ = nullptr;
};
