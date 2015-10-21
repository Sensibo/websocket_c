#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include "debug_utils.h"
#include "websocket_client.h"

#define _HTTP_HEADER_SEP                "\r\n"
#define _HTTP_HEADER_SEP_LENGTH         2
#define _HTTP_REQUEST_LINE              "GET %s HTTP/1.1"
#define _HTTP_HEADER_HOST               "Host: %s"
#define _HTTP_STATUS_LINE_BEGIN         "HTTP/"
#define _HTTP_MAX_STATUS_CODE           999
#define _HTTP_STATUS_CODE_LENGTH        3
#define _HTTP_STATUS_LINE_BEGIN_LENGTH  5
#define _HTTP_STATUS_LINE_MIN_LENGTH    _HTTP_STATUS_LINE_BEGIN_LENGTH + _HTTP_STATUS_CODE_LENGTH + 1

#define _HTTP_MAX_REDIRECTS             6

static const char* const _HTTP_HEADERS[] = {
        "Upgrade: websocket",
        "Connection: Upgrade",
        "Sec-WebSocket-Key: wP2PzujezcAw/ad3x8RzOw==",
        "Sec-WebSocket-Version: 13"
};

#define _WS_MAX_PAYLOAD_FOR_SHORT_HEADER 125
#define _WS_FRAME_HEADER_FOR_SHORT_PAYLOAD 6
#define _WS_FRAME_HEADER_FOR_LONG_PAYLOAD 8

#define _WS_HEADER_FIN_BIT                  (1<<7)
#define _WS_HEADER_OPCODE_BITMASK           15
#define _WS_HEADER_OPCODE_TEXT              0x1
#define _WS_HEADER_OPCODE_BINARY            0x2
#define _WS_HEADER_OPCODE_PING              0x9
#define _WS_HEADER_OPCODE_PONG              0xA
#define _WS_HEADER_MASK_BIT                 (1<<7)
#define _WS_HEADER_PAYLOAD_LENGTH_BITMASK   127
#define _WS_PAYLOAD_LENGTH_EXTENDED_16BIT   126
#define _WS_PAYLOAD_LENGTH_EXTENDED_64BIT   127
#define _WS_HEADER_MASK_SIZE                4

char* ws_get_outgoing_payload_ptr(const ws_handle* handle) {
    return handle->network_buffer.s + _WS_FRAME_HEADER_FOR_LONG_PAYLOAD;
}

#define SNPRINTF_SAFE(dest, dest_len, format, ...) \
    do { \
        int _w = snprintf(dest, dest_len, format, __VA_ARGS__); \
        if (_w >= dest_len) return -1; \
        dest += _w; \
        dest_len -= _w; \
    } while(0);

static int _http_handshake_buffer(
        ws_lstr buffer,
        char* hostname,
        char* path,
        const char* const extra_headers[],
        const size_t num_extra_headers
) {
    char* current_buffer = buffer.s;
    SNPRINTF_SAFE(current_buffer, buffer.length, _HTTP_REQUEST_LINE
            _HTTP_HEADER_SEP, path);
    SNPRINTF_SAFE(current_buffer, buffer.length, _HTTP_HEADER_HOST
            _HTTP_HEADER_SEP, hostname);
    int i;
    size_t num_headers = sizeof(_HTTP_HEADERS) / sizeof(const char*);
    for (i = 0; i < num_headers; ++i) {
        SNPRINTF_SAFE(current_buffer, buffer.length, "%s"
                _HTTP_HEADER_SEP, _HTTP_HEADERS[i]);
    }
    for (i = 0; i < num_extra_headers; ++i) {
        SNPRINTF_SAFE(current_buffer, buffer.length, "%s"
                _HTTP_HEADER_SEP, extra_headers[i]);
    }
    SNPRINTF_SAFE(current_buffer, buffer.length, "%s", _HTTP_HEADER_SEP);
    return (int) (current_buffer - buffer.s);
}

static int _send_http_handshake(
        ws_handle* handle,
        char* hostname,
        char* path_and_query,
        const char* const extra_headers[],
        const size_t num_extra_headers
) {
    int result = _http_handshake_buffer(
            handle->network_buffer, hostname, path_and_query, extra_headers, num_extra_headers);

    if (result < 0) return WS_ERROR_BUFFER_TOO_SHORT;
    size_t request_length = (size_t) result;
    ssize_t write_result = write(handle->sockfd, handle->network_buffer.s, request_length);
    return write_result == request_length ? 0 : WS_ERROR_WRITING_TO_SOCKET;
}

static int _get_http_status_code_from_status_line(char* line, size_t length) {
    if (length < _HTTP_STATUS_LINE_MIN_LENGTH ||
        memcmp(line, _HTTP_STATUS_LINE_BEGIN, _HTTP_STATUS_LINE_BEGIN_LENGTH) != 0) {
        return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
    }

    size_t i = _HTTP_STATUS_LINE_BEGIN_LENGTH;
    while(i < length && *(line+i) != ' ') i++;
    if (*(line+i) != ' ') {
        return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
    }

    i++;
    unsigned short status_code = 0;
    while (i<length && '0' <= *(line+i) && *(line+i) <= '9') {
        status_code = (unsigned short) (status_code * 10 + *(line+i) - '0');
        if (status_code > _HTTP_MAX_STATUS_CODE) {
            return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
        }
        i++;
    }

    return status_code;
}

static int _get_http_header(ws_lstr headers, char* header_name, ws_lstr* value) {
    char* pos = headers.s;
    char* header_end;

    do {
        hex_dump("header pos", pos, headers.length - (pos - headers.s));
        header_end = strnstr(pos, _HTTP_HEADER_SEP, headers.length - (pos - headers.s));
        if (header_end == NULL) {
            printf("PROTOCOL_ERROR: no header end\n");
            return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
        }

        ws_lstr name;
        name.s = pos;
        name.length = 0;

        while (*pos != ':') {
            if (pos >= header_end) {
                printf("PROTOCOL_ERROR: no colon\n");
                return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
            }
            name.length++;
            pos++;
        }

        if (strncasecmp(name.s, header_name, name.length) == 0) {
            pos++; // skip ':'
            if (pos >= header_end) {
                printf("PROTOCOL_ERROR: no value\n");
                return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
            }

            // skip leading whitespace
            while (*pos == ' ' || *pos == '\t') {
                pos++;
                if (pos >= header_end) {
                    printf("PROTOCOL_ERROR: no value after leading whitespace\n");
                    return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
                }
            }

            value->s = pos;
            value->length = header_end - pos;

            // trim trailing whitespace
            while(value->length > 0 && (*(value->s + value->length - 1) == ' ' || *(value->s + value->length - 1) == '\t')) {
                value->length--;
            }

            if (value->length == 0) {
                printf("PROTOCOL_ERROR: no value after trimming trailing whitespace\n");
                return WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR;
            }

            return 1;
        }

        pos = header_end + _HTTP_HEADER_SEP_LENGTH;
    } while(pos <= headers.s + headers.length && strncmp(pos, _HTTP_HEADER_SEP, _HTTP_HEADER_SEP_LENGTH) != 0);

    return 0;
}

#define _HTTP_RESPONSE_IS_REDIRECT 300
static int _receive_http_handshake_response(const ws_handle* handle, ws_lstr* redirect_url) {
    ssize_t read_result = read(handle->sockfd, handle->network_buffer.s, handle->network_buffer.length);
    if (read_result < 0) {
        return WS_ERROR_READING_FROM_SOCKET;
    }
    if (read_result == 0) {
        return WS_ERROR_REMOTE_SOCKET_CLOSED;
    }

    size_t buffer_length = (size_t) read_result;

    printf("http handshake response length %d\n", (int) buffer_length);
    hex_dump("received http handshake response", handle->network_buffer.s, buffer_length);

    if (buffer_length == handle->network_buffer.length) {
        return WS_ERROR_BUFFER_TOO_SHORT;
    }

    char* pos = handle->network_buffer.s;
    char* status_line = pos;
    pos = strnstr(pos, _HTTP_HEADER_SEP, buffer_length);
    int status_code = _get_http_status_code_from_status_line(status_line, pos - status_line);
    if (status_code < 0) {
        return status_code;
    }

    if (status_code == 301 || status_code == 302) {
        pos += _HTTP_HEADER_SEP_LENGTH;
        ws_lstr headers;
        headers.s = pos;
        headers.length = buffer_length - (pos - handle->network_buffer.s);
        int r = _get_http_header(headers, "location", redirect_url);
        if (r < 0) {
            return r;
        }
        if (r == 0) {
            return WS_ERROR_HTTP_REDIRECT_MISSING_LOCATION_HEADER;
        }
        return _HTTP_RESPONSE_IS_REDIRECT;
    }
    if (status_code != 101) {
        printf("status code %d", status_code);
        return WS_ERROR_HTTP_HANDSHAKE_HTTP_ERROR;
    }

    return 0;
}

static int _send(const ws_handle* handle, const char opcode, const void* payload, const size_t payload_length) {
    bool is_long_payload = payload_length > _WS_MAX_PAYLOAD_FOR_SHORT_HEADER ? true : false;
    int header_length = is_long_payload ?
                        _WS_FRAME_HEADER_FOR_LONG_PAYLOAD : _WS_FRAME_HEADER_FOR_SHORT_PAYLOAD;

    char* header_start = (char*) (payload - header_length);
    char* header_pos = header_start;
    *(header_pos) = (char) (_WS_HEADER_FIN_BIT);
    *(header_pos) |= opcode;
    header_pos++;
    *(header_pos) = (char) (_WS_HEADER_MASK_BIT);

    if (is_long_payload) {
        *(header_pos) |= _WS_PAYLOAD_LENGTH_EXTENDED_16BIT;
        header_pos++;
        uint16_t l;
        l = htons(payload_length);
        memcpy(header_pos, &l, sizeof(uint16_t));
        header_pos += sizeof(uint16_t);
    }
    else {
        *(header_pos) |= payload_length;
        header_pos++;
    }

    char* mask = header_pos;

    int i;
    for (i = 0; i < _WS_HEADER_MASK_SIZE; ++i) {
        mask[i] = (char) (rand() % 256);
    }

    char* payload_byte = (char*) payload;
    for (i = 0; i < payload_length; ++i) {
        *payload_byte = *payload_byte ^ mask[i % _WS_HEADER_MASK_SIZE];
        payload_byte++;
    }

    size_t frame_length = header_length + payload_length;
    return write(handle->sockfd, header_start, frame_length) == frame_length ? 0 : WS_ERROR_WRITING_TO_SOCKET;
}

int ws_send_text(const ws_handle* handle, const size_t payload_length) {
    return _send(handle, _WS_HEADER_OPCODE_TEXT, ws_get_outgoing_payload_ptr(handle), payload_length);
}

int ws_send_pong(const ws_handle* handle, const void* payload, const size_t payload_length) {
    if (payload < (void*)handle->network_buffer.s || payload > (void*)(handle->network_buffer.s) + handle->network_buffer.length) {
        return WS_ERROR_INVALID_PONG_PAYLOAD;
    }
    return _send(handle, _WS_HEADER_OPCODE_PONG, payload, payload_length);
}

// returns payload length
int ws_receive(const ws_handle* handle, ws_received_message_type* message_type, void** payload,
               struct timeval* timeout) {
    fd_set read_fds;

    FD_ZERO(&read_fds);
    FD_SET(handle->sockfd, &read_fds);

    if (select(handle->sockfd + 1, &read_fds, NULL, NULL, timeout) == 0) {
        *message_type = WS_PAYLOAD_TYPE_NONE;
        return 0;
    }

    ssize_t buffer_length = read(handle->sockfd, handle->network_buffer.s, handle->network_buffer.length);

    if (buffer_length < 0) {
        return WS_ERROR_READING_FROM_SOCKET;
    }
    else if (buffer_length == 0) {
        *message_type = WS_PAYLOAD_TYPE_NONE;
        return WS_ERROR_REMOTE_SOCKET_CLOSED;
    }

    if (buffer_length == handle->network_buffer.length) {
        return WS_ERROR_BUFFER_TOO_SHORT;
    }

    char* frame_pos;
    frame_pos = handle->network_buffer.s;
    hex_dump("received on socket", frame_pos, (size_t) buffer_length);
    unsigned char is_fin = (unsigned char) (_WS_HEADER_FIN_BIT & *(frame_pos));

    if (!is_fin) {
        return WS_ERROR_CONTINUATION_NOT_SUPPORTED;
    }

    char op_code = (char) (*(frame_pos) & _WS_HEADER_OPCODE_BITMASK);
    if (op_code == _WS_HEADER_OPCODE_TEXT) {
        *message_type = WS_PAYLOAD_TYPE_TEXT;
    }
    else if (op_code == _WS_HEADER_OPCODE_BINARY) {
        *message_type = WS_PAYLOAD_TYPE_BINARY;
    }
    else if (op_code == _WS_HEADER_OPCODE_PING) {
        *message_type = WS_PAYLOAD_TYPE_PING;
    }
    else {
        return WS_ERROR_UNSUPPORTED_OPCODE;
    }

    frame_pos++;

    char is_masked = (char) (*(frame_pos) & _WS_HEADER_MASK_BIT);

    size_t payload_length = (size_t) (*(frame_pos) & _WS_HEADER_PAYLOAD_LENGTH_BITMASK);
    frame_pos++;

    if (payload_length == _WS_PAYLOAD_LENGTH_EXTENDED_16BIT) {
        payload_length = ntohs(*((uint16_t*) frame_pos));
        frame_pos += sizeof(uint16_t);
    }
    else if (payload_length == _WS_PAYLOAD_LENGTH_EXTENDED_64BIT) {
        uint64_t payload_length_64bit = ntohll(*((uint64_t*) frame_pos));
        if (payload_length_64bit > SIZE_MAX) {
            return WS_ERROR_PAYLOAD_EXCEEDED_MAX_LENGTH;
        }
        payload_length = (size_t) payload_length_64bit;
        frame_pos += sizeof(uint64_t);
    }

    if (is_masked) {
        char* mask;
        mask = frame_pos;
        frame_pos += _WS_HEADER_MASK_SIZE;
        char* payload_byte;
        payload_byte = frame_pos;
        int i;
        for (i = 0; i < payload_length; ++i) {
            *payload_byte = *payload_byte ^ mask[i % _WS_HEADER_MASK_SIZE];
            payload_byte++;
        }
    }

    *payload = frame_pos;

    return (int) payload_length;
}

int ws_init(
        ws_handle* handle,
        void* network_buffer,
        const unsigned short network_buffer_length,
        ws_endpoint endpoint,
        const char* const extra_http_headers[],
        const size_t num_extra_http_headers
) {
    struct hostent* he;
    struct sockaddr_in server_address;

    unsigned short num_redirects = 0;
    int r;
    do {
        if ((handle->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            return WS_ERROR_CREATING_SOCKET;
        }

        if ((he = gethostbyname(endpoint.hostname)) == NULL) {
            return WS_ERROR_RESOLVING_HOSTNAME;
        }

        memcpy(&server_address.sin_addr, he->h_addr_list[0], he->h_length);

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(endpoint.port);

        if (connect(handle->sockfd, (struct sockaddr*) &server_address, sizeof(server_address)) < 0) {
            return WS_ERROR_CONNECT_FAILED;
        }

        handle->network_buffer.s = network_buffer;
        handle->network_buffer.length = network_buffer_length;
        r = _send_http_handshake(
                handle, endpoint.hostname, endpoint.path_and_query, extra_http_headers, num_extra_http_headers
        );
        if (r < 0) return r;

        ws_lstr redirect_url;
        r = _receive_http_handshake_response(handle, &redirect_url);
        if (r < 0) return r;
        if (r == _HTTP_RESPONSE_IS_REDIRECT) {
            close(handle->sockfd);
            num_redirects++;
            if (num_redirects > _HTTP_MAX_REDIRECTS) {
                return WS_ERROR_TOO_MANY_REDIRECTS;
            }
            *(redirect_url.s + redirect_url.length) = 0; // null terminate the url
            printf("redirect url: '%s'\n", redirect_url.s);
            ws_endpoint redirect_endpoint;
            int r2 = ws_parse_url(redirect_url.s, &redirect_endpoint, true);
            if (r2 < 0) {
                printf("Invalid redirect: %d\n", r2);
                return WS_ERROR_INVALID_REDIRECT_URL;
            }
            if (*redirect_endpoint.hostname == 0) { // relative url
                strncpy(endpoint.path_and_query, redirect_endpoint.path_and_query, WS_MAX_PATH_AND_QUERY_LENGTH);
            } else {
                endpoint = redirect_endpoint;
            }
        }
    } while (r == _HTTP_RESPONSE_IS_REDIRECT);

    return 0;
}

static char* const URL_SCHEME_SEPARATOR = "://";

int ws_parse_url(const char* url, ws_endpoint* endpoint, const bool allow_relative) {
    char* pos = (char*) url;
    char* scheme_separator = strstr(pos, URL_SCHEME_SEPARATOR);
    if (scheme_separator == NULL) {
        endpoint->is_ssl = false;
    } else {
        size_t scheme_length = scheme_separator - pos;
        if (strncmp("https", pos, scheme_length) == 0 || strncmp("wss", pos, scheme_length) == 0) {
            endpoint->is_ssl = true;
        } else if (strncmp("http", pos, scheme_length) == 0 || strncmp("ws", pos, scheme_length) == 0) {
            endpoint->is_ssl = false;
        } else {
            return WS_ERROR_INVALID_URL_SCHEME;
        }
        pos = scheme_separator + strlen(URL_SCHEME_SEPARATOR);
    }

    char* hostname = pos;
    while (*pos && !strchr(":/?#", *pos))
        if (pos - hostname > WS_MAX_HOSTNAME_LENGTH)
            return WS_ERROR_HOSTNAME_TOO_LONG;
        else
            pos++;

    memset(endpoint->hostname, 0, sizeof(endpoint->hostname));

    bool is_relative;
    if (pos - hostname == 0) { // hostname is empty
        if (scheme_separator != NULL) {  // has scheme
            return WS_ERROR_EMPTY_HOSTNAME;
        } else if (!allow_relative) {
            return WS_ERROR_RELATIVE_URL_NOT_ALLOWED;
        }
        is_relative = true;
    } else {
        is_relative = false;
    }

    memcpy(endpoint->hostname, hostname, pos - hostname);

    if (*pos == ':') {
        if (is_relative) {
            return WS_ERROR_INVALID_URL;
        }
        pos++;
        uint32_t port = 0;
        while ('0' <= *pos && *pos <= '9') {
            port = port * 10 + *pos - '0';
            pos++;
            if (port > UINT16_MAX) {
                return WS_ERROR_INVALID_PORT;
            }
        }
        if (port == 0) {
            return WS_ERROR_INVALID_PORT;
        }
        endpoint->port = (unsigned short) port;
    } else {
        endpoint->port = (unsigned short) (endpoint->is_ssl ? 443 : 80);
    }

    memset(endpoint->path_and_query, 0, sizeof(endpoint->path_and_query));

    if (*pos == '/') {
        char* path = pos;
        while (*pos && *pos != '#')
            if (pos - path > WS_MAX_PATH_AND_QUERY_LENGTH)
                return WS_ERROR_PATH_AND_QUERY_TOO_LONG;
            else
                pos++;

        memcpy(endpoint->path_and_query, path, pos - path);
    } else if (!*hostname) {
        return WS_ERROR_INVALID_URL;
    } else {
        endpoint->path_and_query[0] = '/';
    }

    return 0;
}


//int send_websocket_pong(const int sockfd, const size_t payload_length)
//{
//    send_websocket
//}

