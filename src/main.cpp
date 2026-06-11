#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "bsp/board.h"
#include "cyw43.h"
#include "dhcp_server.h"
#include "hardware/watchdog.h"
#include "http_server.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef UDP_LISTEN_PORT
#define UDP_LISTEN_PORT 4242
#endif

namespace {

constexpr size_t kQueueSize = 256;
constexpr uint8_t kNoModifier = 0;
constexpr const char *kApIpAddress = "192.168.4.1";

struct KeyEvent {
    uint8_t modifier;
    uint8_t keycode;
};

std::array<KeyEvent, kQueueSize> key_queue{};
volatile size_t queue_head = 0;
volatile size_t queue_tail = 0;
bool release_pending = false;
udp_pcb *keyboard_pcb = nullptr;
DhcpServer dhcp_server;
HttpServer http_server;

bool queue_is_full() {
    return ((queue_head + 1) % kQueueSize) == queue_tail;
}

bool queue_is_empty() {
    return queue_head == queue_tail;
}

void enqueue_key(uint8_t modifier, uint8_t keycode) {
    if (keycode == 0 || queue_is_full()) {
        return;
    }

    key_queue[queue_head] = KeyEvent{modifier, keycode};
    queue_head = (queue_head + 1) % kQueueSize;
}

bool dequeue_key(KeyEvent &event) {
    if (queue_is_empty()) {
        return false;
    }

    event = key_queue[queue_tail];
    queue_tail = (queue_tail + 1) % kQueueSize;
    return true;
}

void enqueue_text(const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        const char c = *p;

        if (c >= 'a' && c <= 'z') {
            enqueue_key(kNoModifier, HID_KEY_A + (c - 'a'));
        } else if (c >= 'A' && c <= 'Z') {
            enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_A + (c - 'A'));
        } else if (c >= '1' && c <= '9') {
            enqueue_key(kNoModifier, HID_KEY_1 + (c - '1'));
        } else if (c == '0') {
            enqueue_key(kNoModifier, HID_KEY_0);
        } else {
            switch (c) {
                case ' ': enqueue_key(kNoModifier, HID_KEY_SPACE); break;
                case '\n': enqueue_key(kNoModifier, HID_KEY_ENTER); break;
                case '\r': break;
                case '\t': enqueue_key(kNoModifier, HID_KEY_TAB); break;
                case '-': enqueue_key(kNoModifier, HID_KEY_MINUS); break;
                case '_': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_MINUS); break;
                case '=': enqueue_key(kNoModifier, HID_KEY_EQUAL); break;
                case '+': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_EQUAL); break;
                case '[': enqueue_key(kNoModifier, HID_KEY_BRACKET_LEFT); break;
                case '{': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_BRACKET_LEFT); break;
                case ']': enqueue_key(kNoModifier, HID_KEY_BRACKET_RIGHT); break;
                case '}': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_BRACKET_RIGHT); break;
                case '\\': enqueue_key(kNoModifier, HID_KEY_BACKSLASH); break;
                case '|': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_BACKSLASH); break;
                case ';': enqueue_key(kNoModifier, HID_KEY_SEMICOLON); break;
                case ':': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_SEMICOLON); break;
                case '\'': enqueue_key(kNoModifier, HID_KEY_APOSTROPHE); break;
                case '"': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_APOSTROPHE); break;
                case '`': enqueue_key(kNoModifier, HID_KEY_GRAVE); break;
                case '~': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_GRAVE); break;
                case ',': enqueue_key(kNoModifier, HID_KEY_COMMA); break;
                case '<': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_COMMA); break;
                case '.': enqueue_key(kNoModifier, HID_KEY_PERIOD); break;
                case '>': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_PERIOD); break;
                case '/': enqueue_key(kNoModifier, HID_KEY_SLASH); break;
                case '?': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_SLASH); break;
                case '!': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_1); break;
                case '@': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_2); break;
                case '#': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_3); break;
                case '$': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_4); break;
                case '%': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_5); break;
                case '^': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_6); break;
                case '&': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_7); break;
                case '*': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_8); break;
                case '(': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_9); break;
                case ')': enqueue_key(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_0); break;
                default: break;
            }
        }
    }
}

void uppercase(char *value) {
    for (char *p = value; *p != '\0'; p++) {
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    }
}

void trim_line_end(char *value) {
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n')) {
        value[len - 1] = '\0';
        len--;
    }
}

uint8_t parse_keycode(const char *key) {
    if (strlen(key) == 1 && key[0] >= 'A' && key[0] <= 'Z') return HID_KEY_A + (key[0] - 'A');
    if (strlen(key) == 1 && key[0] >= '0' && key[0] <= '9') return key[0] == '0' ? HID_KEY_0 : HID_KEY_1 + (key[0] - '1');

    if (key[0] == 'F' && key[1] >= '1' && key[1] <= '9') {
        int number = key[1] - '0';
        if (key[2] >= '0' && key[2] <= '9') number = number * 10 + (key[2] - '0');
        if ((key[2] == '\0' || key[3] == '\0') && number >= 1 && number <= 24) {
            return HID_KEY_F1 + (number - 1);
        }
    }

    if (strcmp(key, "ENTER") == 0 || strcmp(key, "RETURN") == 0) return HID_KEY_ENTER;
    if (strcmp(key, "ESC") == 0 || strcmp(key, "ESCAPE") == 0) return HID_KEY_ESCAPE;
    if (strcmp(key, "BACKSPACE") == 0 || strcmp(key, "BKSP") == 0) return HID_KEY_BACKSPACE;
    if (strcmp(key, "TAB") == 0) return HID_KEY_TAB;
    if (strcmp(key, "SPACE") == 0) return HID_KEY_SPACE;
    if (strcmp(key, "MINUS") == 0 || strcmp(key, "-") == 0) return HID_KEY_MINUS;
    if (strcmp(key, "EQUAL") == 0 || strcmp(key, "EQUALS") == 0 || strcmp(key, "=") == 0) return HID_KEY_EQUAL;
    if (strcmp(key, "LBRACKET") == 0 || strcmp(key, "LEFTBRACKET") == 0 || strcmp(key, "[") == 0) return HID_KEY_BRACKET_LEFT;
    if (strcmp(key, "RBRACKET") == 0 || strcmp(key, "RIGHTBRACKET") == 0 || strcmp(key, "]") == 0) return HID_KEY_BRACKET_RIGHT;
    if (strcmp(key, "BACKSLASH") == 0 || strcmp(key, "\\") == 0) return HID_KEY_BACKSLASH;
    if (strcmp(key, "SEMICOLON") == 0 || strcmp(key, ";") == 0) return HID_KEY_SEMICOLON;
    if (strcmp(key, "APOSTROPHE") == 0 || strcmp(key, "QUOTE") == 0 || strcmp(key, "'") == 0) return HID_KEY_APOSTROPHE;
    if (strcmp(key, "GRAVE") == 0 || strcmp(key, "BACKQUOTE") == 0 || strcmp(key, "`") == 0) return HID_KEY_GRAVE;
    if (strcmp(key, "COMMA") == 0 || strcmp(key, ",") == 0) return HID_KEY_COMMA;
    if (strcmp(key, "PERIOD") == 0 || strcmp(key, "DOT") == 0 || strcmp(key, ".") == 0) return HID_KEY_PERIOD;
    if (strcmp(key, "SLASH") == 0 || strcmp(key, "/") == 0) return HID_KEY_SLASH;
    if (strcmp(key, "CAPSLOCK") == 0 || strcmp(key, "CAPS_LOCK") == 0) return HID_KEY_CAPS_LOCK;
    if (strcmp(key, "PRINTSCREEN") == 0 || strcmp(key, "PRTSC") == 0) return HID_KEY_PRINT_SCREEN;
    if (strcmp(key, "SCROLLLOCK") == 0 || strcmp(key, "SCROLL_LOCK") == 0) return HID_KEY_SCROLL_LOCK;
    if (strcmp(key, "PAUSE") == 0 || strcmp(key, "BREAK") == 0) return HID_KEY_PAUSE;
    if (strcmp(key, "INSERT") == 0 || strcmp(key, "INS") == 0) return HID_KEY_INSERT;
    if (strcmp(key, "HOME") == 0) return HID_KEY_HOME;
    if (strcmp(key, "PAGEUP") == 0 || strcmp(key, "PGUP") == 0) return HID_KEY_PAGE_UP;
    if (strcmp(key, "DELETE") == 0 || strcmp(key, "DEL") == 0) return HID_KEY_DELETE;
    if (strcmp(key, "END") == 0) return HID_KEY_END;
    if (strcmp(key, "PAGEDOWN") == 0 || strcmp(key, "PGDN") == 0) return HID_KEY_PAGE_DOWN;
    if (strcmp(key, "RIGHT") == 0) return HID_KEY_ARROW_RIGHT;
    if (strcmp(key, "LEFT") == 0) return HID_KEY_ARROW_LEFT;
    if (strcmp(key, "DOWN") == 0) return HID_KEY_ARROW_DOWN;
    if (strcmp(key, "UP") == 0) return HID_KEY_ARROW_UP;
    if (strcmp(key, "NUMLOCK") == 0 || strcmp(key, "NUM_LOCK") == 0) return HID_KEY_NUM_LOCK;
    if (strcmp(key, "KP/") == 0 || strcmp(key, "KP_DIVIDE") == 0) return HID_KEY_KEYPAD_DIVIDE;
    if (strcmp(key, "KP*") == 0 || strcmp(key, "KP_MULTIPLY") == 0) return HID_KEY_KEYPAD_MULTIPLY;
    if (strcmp(key, "KP-") == 0 || strcmp(key, "KP_SUBTRACT") == 0) return HID_KEY_KEYPAD_SUBTRACT;
    if (strcmp(key, "KP+") == 0 || strcmp(key, "KP_ADD") == 0) return HID_KEY_KEYPAD_ADD;
    if (strcmp(key, "KPENTER") == 0 || strcmp(key, "KP_ENTER") == 0) return HID_KEY_KEYPAD_ENTER;
    if (strcmp(key, "KP.") == 0 || strcmp(key, "KP_DECIMAL") == 0) return HID_KEY_KEYPAD_DECIMAL;
    if (key[0] == 'K' && key[1] == 'P' && key[2] >= '0' && key[2] <= '9' && key[3] == '\0') {
        return key[2] == '0' ? HID_KEY_KEYPAD_0 : HID_KEY_KEYPAD_1 + (key[2] - '1');
    }
    if (strcmp(key, "RO") == 0 || strcmp(key, "JIS_RO") == 0) return HID_KEY_KANJI1;
    if (strcmp(key, "KATAKANA_HIRAGANA") == 0 || strcmp(key, "KATAHIRA") == 0) return HID_KEY_KANJI2;
    if (strcmp(key, "YEN") == 0 || strcmp(key, "JIS_YEN") == 0) return HID_KEY_KANJI3;
    if (strcmp(key, "HENKAN") == 0 || strcmp(key, "CONVERT") == 0) return HID_KEY_KANJI4;
    if (strcmp(key, "MUHENKAN") == 0 || strcmp(key, "NONCONVERT") == 0) return HID_KEY_KANJI5;
    if (strcmp(key, "KANA") == 0 || strcmp(key, "KANA_MODE") == 0) return HID_KEY_LANG1;
    if (strcmp(key, "EISU") == 0 || strcmp(key, "EISUU") == 0 || strcmp(key, "ALPHANUMERIC") == 0) return HID_KEY_LANG2;
    if (strcmp(key, "KATAKANA") == 0) return HID_KEY_LANG3;
    if (strcmp(key, "HIRAGANA") == 0) return HID_KEY_LANG4;
    if (strcmp(key, "HANKAKU") == 0 || strcmp(key, "ZENKAKU") == 0 ||
        strcmp(key, "HANKAKU_ZENKAKU") == 0 || strcmp(key, "ZENKAKU_HANKAKU") == 0 ||
        strcmp(key, "ZENKAKUHANKAKU") == 0 || strcmp(key, "KANJI") == 0) {
        return HID_KEY_LANG5;
    }

    return 0;
}

void enqueue_named_key(char *name) {
    trim_line_end(name);
    uppercase(name);

    uint8_t modifier = 0;
    char *key = name;

    while (true) {
        if (strncmp(key, "CTRL+", 5) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
            key += 5;
        } else if (strncmp(key, "CONTROL+", 8) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
            key += 8;
        } else if (strncmp(key, "LCTRL+", 6) == 0 || strncmp(key, "LEFTCTRL+", 9) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
            key += key[0] == 'L' && key[1] == 'C' ? 6 : 9;
        } else if (strncmp(key, "RCTRL+", 6) == 0 || strncmp(key, "RIGHTCTRL+", 10) == 0) {
            modifier |= KEYBOARD_MODIFIER_RIGHTCTRL;
            key += key[0] == 'R' && key[1] == 'C' ? 6 : 10;
        } else if (strncmp(key, "ALT+", 4) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTALT;
            key += 4;
        } else if (strncmp(key, "LALT+", 5) == 0 || strncmp(key, "LEFTALT+", 8) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTALT;
            key += key[0] == 'L' && key[1] == 'A' ? 5 : 8;
        } else if (strncmp(key, "RALT+", 5) == 0 || strncmp(key, "RIGHTALT+", 9) == 0) {
            modifier |= KEYBOARD_MODIFIER_RIGHTALT;
            key += key[0] == 'R' && key[1] == 'A' ? 5 : 9;
        } else if (strncmp(key, "SHIFT+", 6) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
            key += 6;
        } else if (strncmp(key, "LSHIFT+", 7) == 0 || strncmp(key, "LEFTSHIFT+", 10) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
            key += key[0] == 'L' && key[1] == 'S' ? 7 : 10;
        } else if (strncmp(key, "RSHIFT+", 7) == 0 || strncmp(key, "RIGHTSHIFT+", 11) == 0) {
            modifier |= KEYBOARD_MODIFIER_RIGHTSHIFT;
            key += key[0] == 'R' && key[1] == 'S' ? 7 : 11;
        } else if (strncmp(key, "GUI+", 4) == 0 || strncmp(key, "WIN+", 4) == 0 || strncmp(key, "CMD+", 4) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTGUI;
            key += 4;
        } else if (strncmp(key, "LGUI+", 5) == 0 || strncmp(key, "LEFTGUI+", 8) == 0) {
            modifier |= KEYBOARD_MODIFIER_LEFTGUI;
            key += key[0] == 'L' && key[1] == 'G' ? 5 : 8;
        } else if (strncmp(key, "RGUI+", 5) == 0 || strncmp(key, "RIGHTGUI+", 9) == 0) {
            modifier |= KEYBOARD_MODIFIER_RIGHTGUI;
            key += key[0] == 'R' && key[1] == 'G' ? 5 : 9;
        } else {
            break;
        }
    }

    const uint8_t keycode = parse_keycode(key);
    if (keycode != 0) {
        enqueue_key(modifier, keycode);
    }
}

void handle_packet(char *packet) {
    if (strncmp(packet, "TEXT ", 5) == 0) {
        enqueue_text(packet + 5);
    } else if (strncmp(packet, "KEY ", 4) == 0) {
        enqueue_named_key(packet + 4);
    } else {
        enqueue_text(packet);
    }
}

void udp_recv_callback(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port) {
    (void) arg;
    (void) pcb;
    (void) addr;
    (void) port;

    if (p == nullptr) {
        return;
    }

    char buffer[256];
    const uint16_t copy_len = p->tot_len < sizeof(buffer) - 1 ? p->tot_len : sizeof(buffer) - 1;
    pbuf_copy_partial(p, buffer, copy_len, 0);
    buffer[copy_len] = '\0';
    pbuf_free(p);

    handle_packet(buffer);
}

bool start_udp_server() {
    cyw43_arch_lwip_begin();

    keyboard_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (keyboard_pcb == nullptr) {
        cyw43_arch_lwip_end();
        return false;
    }

    if (udp_bind(keyboard_pcb, IP_ANY_TYPE, UDP_LISTEN_PORT) != ERR_OK) {
        udp_remove(keyboard_pcb);
        keyboard_pcb = nullptr;
        cyw43_arch_lwip_end();
        return false;
    }

    udp_recv(keyboard_pcb, udp_recv_callback, nullptr);
    cyw43_arch_lwip_end();
    return true;
}

void send_next_hid_report() {
    if (!tud_hid_ready()) {
        return;
    }

    if (release_pending) {
        tud_hid_keyboard_report(0, 0, nullptr);
        release_pending = false;
        return;
    }

    KeyEvent event{};
    if (!dequeue_key(event)) {
        return;
    }

    uint8_t keycodes[6] = {event.keycode, 0, 0, 0, 0, 0};
    tud_hid_keyboard_report(0, event.modifier, keycodes);
    release_pending = true;
}

void start_access_point() {
    if (strlen(WIFI_SSID) == 0) {
        printf("WIFI_SSID is empty. Configure with -DMOBILEKBD_WIFI_SSID=...\n");
        return;
    }

    const uint32_t auth = strlen(WIFI_PASSWORD) == 0 ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
    const char *password = strlen(WIFI_PASSWORD) == 0 ? nullptr : WIFI_PASSWORD;

    ip4_addr_t ip;
    ip4addr_aton(kApIpAddress, &ip);

    printf("Starting Wi-Fi AP SSID: %s\n", WIFI_SSID);
    cyw43_arch_enable_ap_mode(WIFI_SSID, password, auth);

    cyw43_arch_lwip_begin();
    const bool dhcp_started = dhcp_server.start(ip);
    cyw43_arch_lwip_end();

    const bool udp_started = start_udp_server();

    cyw43_arch_lwip_begin();
    const bool http_started = http_server.start(enqueue_text, enqueue_named_key);
    cyw43_arch_lwip_end();

    if (!dhcp_started) {
        printf("DHCP server failed to start\n");
    }

    if (!udp_started) {
        printf("UDP server failed to start\n");
    }

    if (!http_started) {
        printf("HTTP server failed to start\n");
    } else {
        printf("AP ready. Connect to SSID '%s', then open http://%s/ or send UDP to %s:%d\n",
               WIFI_SSID,
               kApIpAddress,
               kApIpAddress,
               UDP_LISTEN_PORT);
    }
}

} // namespace

int main() {
    stdio_init_all();
    board_init();
    tusb_init();

    if (cyw43_arch_init() != 0) {
        printf("cyw43 init failed\n");
        return 1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    start_access_point();

    while (true) {
        tud_task();
        send_next_hid_report();
        sleep_ms(1);
    }
}
