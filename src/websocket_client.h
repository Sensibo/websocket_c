#ifndef WEBSOCKET_C_WEBSOCKET_CLIENT_H
#define WEBSOCKET_C_WEBSOCKET_CLIENT_H

#include <stddef.h>
#include <stdbool.h>

#define WS_ERROR_CREATING_SOCKET                        -1001
#define WS_ERROR_RESOLVING_HOSTNAME                     -1002
#define WS_ERROR_CONNECT_FAILED                         -1003
#define WS_ERROR_BUFFER_TOO_SHORT                       -1004
#define WS_ERROR_WRITING_TO_SOCKET                      -1005
#define WS_ERROR_READING_FROM_SOCKET                    -1006
#define WS_ERROR_HTTP_HANDSHAKE_PROTOCOL_ERROR          -1007
#define WS_ERROR_HTTP_HANDSHAKE_HTTP_ERROR              -1008
#define WS_ERROR_HTTP_REDIRECT_MISSING_LOCATION_HEADER  -1009
#define WS_ERROR_CONTINUATION_NOT_SUPPORTED             -1010
#define WS_ERROR_UNSUPPORTED_OPCODE                     -1011
#define WS_ERROR_PAYLOAD_EXCEEDED_MAX_LENGTH            -1012
#define WS_ERROR_INVALID_PONG_PAYLOAD                   -1013
#define WS_ERROR_TOO_MANY_REDIRECTS                     -1014
#define WS_ERROR_INVALID_REDIRECT_URL                   -1015
#define WS_ERROR_REMOTE_SOCKET_CLOSED                   -1101
#define WS_ERROR_INVALID_URL_SCHEME                     -1201
#define WS_ERROR_RELATIVE_URL_NOT_ALLOWED               -1202
#define WS_ERROR_EMPTY_HOSTNAME                         -1203
#define WS_ERROR_INVALID_URL                            -1204
#define WS_ERROR_HOSTNAME_TOO_LONG                      -1205
#define WS_ERROR_INVALID_PORT                           -1206
#define WS_ERROR_PATH_AND_QUERY_TOO_LONG                -1207


#define WS_PAYLOAD_TYPE_NONE                            0
#define WS_PAYLOAD_TYPE_TEXT                            1
#define WS_PAYLOAD_TYPE_BINARY                          2
#define WS_PAYLOAD_TYPE_PING                            3

#define WS_MAX_HOSTNAME_LENGTH                          80
#define WS_MAX_PATH_AND_QUERY_LENGTH                    80

typedef struct {
    size_t length;
    char* s;
} ws_lstr;

typedef struct {
    char hostname[WS_MAX_HOSTNAME_LENGTH+1];
    unsigned short port;
    char path_and_query[WS_MAX_PATH_AND_QUERY_LENGTH+1];
    bool is_ssl;
} ws_endpoint;

typedef struct {
    int sockfd;
    ws_lstr network_buffer;
} ws_handle;

typedef char ws_received_message_type;

int ws_init(
        ws_handle* handle,
        void* network_buffer,
        const unsigned short network_buffer_length,
        ws_endpoint endpoint,
        const char* const extra_http_headers[],
        const size_t num_extra_http_headers
);

char* ws_get_outgoing_payload_ptr(const ws_handle* handle);
int ws_send_text(const ws_handle* handle, const size_t payload_length);
int ws_send_pong(const ws_handle* handle, const void* payload, const size_t payload_length);
int ws_receive(const ws_handle* handle, ws_received_message_type* message_type, void** payload, struct timeval* timeout);
int ws_parse_url(const char* url, ws_endpoint* endpoint, const bool allow_relative);

#endif //WEBSOCKET_C_WEBSOCKET_CLIENT_H
