#include "http_server.h"

#include <cstdio>
#include <cstring>

#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr size_t kRequestBufferSize = 1536;

struct ClientState {
    HttpServer *server;
    tcp_pcb *pcb;
    size_t request_len;
    size_t bytes_pending;
    const char *response_body;
    size_t response_len;
    size_t response_offset;
    bool responded;
    char request[kRequestBufferSize];
};

constexpr char kIndexHtml[] =
    "<!doctype html>"
    "<html lang=\"ja\">"
    "<head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>モバイルキーボード</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f5f5f2;color:#202124}"
    "main{max-width:720px;margin:0 auto;padding:20px}"
    "h1{font-size:24px;margin:0 0 16px}"
    "textarea{box-sizing:border-box;width:100%;min-height:180px;font:18px/1.4 ui-monospace,Consolas,monospace;padding:12px;border:1px solid #aaa;border-radius:6px}"
    ".row{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}"
    "button{font-size:16px;padding:10px 14px;border:1px solid #777;border-radius:6px;background:#fff;color:#111}"
    "button.primary{background:#1f6feb;color:#fff;border-color:#1f6feb}"
    "input{box-sizing:border-box;min-width:220px;font-size:16px;padding:10px 12px;border:1px solid #aaa;border-radius:6px}"
    "</style>"
    "</head>"
    "<body>"
    "<main>"
    "<h1>モバイルキーボード</h1>"
    "<form method=\"post\" action=\"/text\">"
    "<textarea name=\"text\" autofocus></textarea>"
    "<div class=\"row\">"
    "<button class=\"primary\" type=\"submit\">送信</button>"
    "<button type=\"submit\" formaction=\"/key/ENTER\">Enter</button>"
    "<button type=\"submit\" formaction=\"/key/BACKSPACE\">Backspace</button>"
    "<button type=\"submit\" formaction=\"/key/TAB\">Tab</button>"
    "</div>"
    "<div class=\"row\">"
    "<button type=\"submit\" formaction=\"/key/LEFT\">Left</button>"
    "<button type=\"submit\" formaction=\"/key/RIGHT\">Right</button>"
    "<button type=\"submit\" formaction=\"/key/UP\">Up</button>"
    "<button type=\"submit\" formaction=\"/key/DOWN\">Down</button>"
    "</div>"
    "<div class=\"row\">"
    "<button type=\"submit\" formaction=\"/key/CTRL+A\">Ctrl+A</button>"
    "<button type=\"submit\" formaction=\"/key/CTRL+C\">Ctrl+C</button>"
    "<button type=\"submit\" formaction=\"/key/CTRL+V\">Ctrl+V</button>"
    "<button type=\"submit\" formaction=\"/key/CTRL+Z\">Ctrl+Z</button>"
    "</div>"
    "<div class=\"row\">"
    "<button type=\"submit\" formaction=\"/key/ALT+TAB\">Alt+Tab</button>"
    "<button type=\"submit\" formaction=\"/key/WIN+R\">Win+R</button>"
    "<button type=\"submit\" formaction=\"/key/CTRL+ALT+DELETE\">Ctrl+Alt+Delete</button>"
    "</div>"
    "<div class=\"row\">"
    "<button type=\"submit\" formaction=\"/key/HANKAKU_ZENKAKU\">半角/全角</button>"
    "<button type=\"submit\" formaction=\"/key/KANA\">かな</button>"
    "<button type=\"submit\" formaction=\"/key/EISU\">英数</button>"
    "<button type=\"submit\" formaction=\"/key/HENKAN\">変換</button>"
    "<button type=\"submit\" formaction=\"/key/MUHENKAN\">無変換</button>"
    "<button type=\"submit\" formaction=\"/key/KATAKANA_HIRAGANA\">カタカナ/ひらがな</button>"
    "</div>"
    "</form>"
    "<form method=\"post\" action=\"/shortcut\">"
    "<div class=\"row\">"
    "<input name=\"key\" placeholder=\"CTRL+SHIFT+ESC\">"
    "<button class=\"primary\" type=\"submit\">ショートカット送信</button>"
    "</div>"
    "</form>"
    "</main>"
    "</body>"
    "</html>";

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode(char *value) {
    char *read = value;
    char *write = value;

    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && hex_value(read[1]) >= 0 && hex_value(read[2]) >= 0) {
            *write++ = static_cast<char>((hex_value(read[1]) << 4) | hex_value(read[2]));
            read += 3;
        } else {
            *write++ = *read++;
        }
    }

    *write = '\0';
}

char *find_body(char *request) {
    char *body = std::strstr(request, "\r\n\r\n");
    if (body != nullptr) {
        return body + 4;
    }
    body = std::strstr(request, "\n\n");
    return body == nullptr ? nullptr : body + 2;
}

int content_length(const char *request) {
    const char *line = std::strstr(request, "Content-Length:");
    if (line == nullptr) {
        line = std::strstr(request, "content-length:");
    }
    if (line == nullptr) {
        return 0;
    }

    line = std::strchr(line, ':');
    if (line == nullptr) {
        return 0;
    }
    line++;

    int value = 0;
    while (*line == ' ') {
        line++;
    }
    while (*line >= '0' && *line <= '9') {
        value = value * 10 + (*line - '0');
        line++;
    }
    return value;
}

bool request_is_complete(const ClientState *state) {
    const char *body = std::strstr(state->request, "\r\n\r\n");
    size_t header_len = 0;
    if (body != nullptr) {
        header_len = static_cast<size_t>((body + 4) - state->request);
    } else {
        body = std::strstr(state->request, "\n\n");
        if (body == nullptr) {
            return false;
        }
        header_len = static_cast<size_t>((body + 2) - state->request);
    }

    if (std::strncmp(state->request, "POST ", 5) != 0) {
        return true;
    }

    const int expected_body_len = content_length(state->request);
    return state->request_len >= header_len + static_cast<size_t>(expected_body_len);
}

void queue_response_body(ClientState *state) {
    if (state == nullptr || state->pcb == nullptr || state->response_body == nullptr) {
        return;
    }

    while (state->response_offset < state->response_len) {
        const uint16_t available = tcp_sndbuf(state->pcb);
        if (available == 0 || tcp_sndqueuelen(state->pcb) >= TCP_SND_QUEUELEN - 1) {
            break;
        }

        size_t remaining = state->response_len - state->response_offset;
        size_t chunk = remaining > 256 ? 256 : remaining;
        if (chunk > available) {
            chunk = available;
        }
        if (chunk == 0) {
            break;
        }

        err_t err = tcp_write(
            state->pcb,
            state->response_body + state->response_offset,
            static_cast<uint16_t>(chunk),
            TCP_WRITE_FLAG_COPY
        );
        if (err == ERR_MEM) {
            break;
        }
        if (err != ERR_OK) {
            HttpServer::close_connection(state->pcb);
            return;
        }

        state->response_offset += chunk;
        state->bytes_pending += chunk;
    }

    tcp_output(state->pcb);
}

} // namespace

bool HttpServer::start(TextHandler text_handler, KeyHandler key_handler) {
    text_handler_ = text_handler;
    key_handler_ = key_handler;

    pcb_ = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb_ == nullptr) {
        return false;
    }

    if (tcp_bind(pcb_, IP_ANY_TYPE, kHttpPort) != ERR_OK) {
        tcp_close(pcb_);
        pcb_ = nullptr;
        return false;
    }

    pcb_ = tcp_listen(pcb_);
    tcp_arg(pcb_, this);
    tcp_accept(pcb_, accept_callback);
    return true;
}

err_t HttpServer::accept_callback(void *arg, tcp_pcb *new_pcb, err_t err) {
    if (err != ERR_OK || new_pcb == nullptr) {
        return ERR_VAL;
    }

    auto *state = static_cast<ClientState *>(mem_calloc(1, sizeof(ClientState)));
    if (state == nullptr) {
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    state->server = static_cast<HttpServer *>(arg);
    state->pcb = new_pcb;

    tcp_arg(new_pcb, state);
    tcp_recv(new_pcb, receive_callback);
    tcp_sent(new_pcb, sent_callback);
    tcp_poll(new_pcb, poll_callback, 10);
    tcp_err(new_pcb, error_callback);
    return ERR_OK;
}

err_t HttpServer::receive_callback(void *arg, tcp_pcb *pcb, pbuf *p, err_t err) {
    if (err != ERR_OK || p == nullptr) {
        if (p != nullptr) {
            pbuf_free(p);
        }
        close_connection(pcb);
        return ERR_OK;
    }

    auto *state = static_cast<ClientState *>(arg);
    if (state == nullptr || state->server == nullptr) {
        pbuf_free(p);
        close_connection(pcb);
        return ERR_OK;
    }

    const size_t free_space = sizeof(state->request) - state->request_len - 1;
    const size_t copy_len = p->tot_len < free_space ? p->tot_len : free_space;
    if (copy_len > 0) {
        pbuf_copy_partial(p, state->request + state->request_len, copy_len, 0);
        state->request_len += copy_len;
        state->request[state->request_len] = '\0';
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (state->request_len >= sizeof(state->request) - 1) {
        state->server->send_response(state, "413 Payload Too Large", "text/plain; charset=utf-8", "too large");
        return ERR_OK;
    }

    if (!state->responded && request_is_complete(state)) {
        state->server->handle_request(state);
    }

    return ERR_OK;
}

err_t HttpServer::sent_callback(void *arg, tcp_pcb *pcb, uint16_t len) {
    auto *state = static_cast<ClientState *>(arg);
    if (state == nullptr) {
        close_connection(pcb);
        return ERR_OK;
    }

    if (len >= state->bytes_pending) {
        state->bytes_pending = 0;
    } else {
        state->bytes_pending -= len;
    }

    queue_response_body(state);

    if (state->responded && state->bytes_pending == 0) {
        close_connection(pcb);
    }
    return ERR_OK;
}

err_t HttpServer::poll_callback(void *arg, tcp_pcb *pcb) {
    (void) arg;
    close_connection(pcb);
    return ERR_OK;
}

void HttpServer::error_callback(void *arg, err_t err) {
    (void) arg;
    (void) err;
}

void HttpServer::close_connection(tcp_pcb *pcb) {
    if (pcb == nullptr) {
        return;
    }

    void *arg = pcb->callback_arg;
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    tcp_err(pcb, nullptr);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }

    if (arg != nullptr) {
        mem_free(arg);
    }
}

void HttpServer::handle_request(void *client_state) {
    auto *state = static_cast<ClientState *>(client_state);
    tcp_pcb *pcb = state->pcb;
    char *request = state->request;

    if (std::strncmp(request, "GET / ", 6) == 0 || std::strncmp(request, "GET /HTTP", 9) == 0) {
        send_response(state, "200 OK", "text/html; charset=utf-8", kIndexHtml);
        return;
    }

    if (std::strncmp(request, "POST /text ", 11) == 0) {
        char *body = find_body(request);
        if (body != nullptr && std::strncmp(body, "text=", 5) == 0 && text_handler_ != nullptr) {
            char *text = body + 5;
            url_decode(text);
            text_handler_(text);
        }
        send_redirect(state);
        return;
    }

    if (std::strncmp(request, "POST /shortcut ", 15) == 0) {
        char *body = find_body(request);
        if (body != nullptr && std::strncmp(body, "key=", 4) == 0 && key_handler_ != nullptr) {
            char *key = body + 4;
            url_decode(key);
            key_handler_(key);
        }
        send_redirect(state);
        return;
    }

    if (std::strncmp(request, "POST /key/", 10) == 0 && key_handler_ != nullptr) {
        char key[32];
        size_t i = 0;
        const char *name = request + 10;
        while (*name != '\0' && *name != ' ' && i < sizeof(key) - 1) {
            key[i++] = *name++;
        }
        key[i] = '\0';
        key_handler_(key);
        send_redirect(state);
        return;
    }

    send_response(state, "404 Not Found", "text/plain; charset=utf-8", "not found");
}

void HttpServer::send_response(void *client_state, const char *status, const char *content_type, const char *body) {
    auto *state = static_cast<ClientState *>(client_state);
    tcp_pcb *pcb = state->pcb;
    char header[256];
    const int body_len = static_cast<int>(std::strlen(body));
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %d\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  status,
                  content_type,
                  body_len);

    const size_t header_len = std::strlen(header);
    state->responded = true;
    state->response_body = body;
    state->response_len = body_len;
    state->response_offset = 0;

    err_t header_err = tcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (header_err != ERR_OK) {
        state->bytes_pending = 0;
        close_connection(pcb);
        return;
    }

    state->bytes_pending = header_len;
    queue_response_body(state);
    tcp_output(pcb);
}

void HttpServer::send_redirect(void *client_state) {
    auto *state = static_cast<ClientState *>(client_state);
    tcp_pcb *pcb = state->pcb;
    constexpr char header[] =
        "HTTP/1.1 303 See Other\r\n"
        "Location: /\r\n"
        "Content-Length: 0\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n";

    state->responded = true;
    state->response_body = nullptr;
    state->response_len = 0;
    state->response_offset = 0;

    const size_t header_len = std::strlen(header);
    err_t header_err = tcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (header_err != ERR_OK) {
        state->bytes_pending = 0;
        close_connection(pcb);
        return;
    }

    state->bytes_pending = header_len;
    tcp_output(pcb);
}
