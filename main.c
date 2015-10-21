#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "src/websocket_client.h"

#define NETWORK_BUFFER_LENGTH 1024
static char shared_network_buffer[NETWORK_BUFFER_LENGTH];

static const char* const EXTRA_HEADERS[] = {
        "X-Sensibo-Whatchamacallit: Foo=Bar,Baz",
        "X-Sensibo-Id: MyID"
};

void test_endpoint_parser(char *url, bool allow_relative) {
    int r;
    ws_endpoint endpoint;
    r = ws_parse_url(url, &endpoint, allow_relative);
    printf("%s\tr=%d\tis_ssl=%d hostname=%s port=%d path_and_query=%s\n", url, r, endpoint.is_ssl, endpoint.hostname, endpoint.port, endpoint.path_and_query);
}

int main(int argc, char *argv[])
{
    int r;

    test_endpoint_parser("localhost", false);
    test_endpoint_parser("localhost:1234", false);
    test_endpoint_parser("localhostlocalhostlocalhostlocalhostlocalhostlocalhostlocalhostlocalhostlocalhostlocalhostlocalhost:1234", false);
    test_endpoint_parser("localhost:66000", false);
    test_endpoint_parser("localhost:9999999", false);
    test_endpoint_parser("localhost:-1", false);
    test_endpoint_parser("localhost:abc", false);
    test_endpoint_parser("http://a.b.c:333/abc/efg?q=1", false);
    test_endpoint_parser("ws://a.b.c:333/abc/efg?q=1", false);
    test_endpoint_parser("https://a.b.c:333/abc/efg?q=1", false);
    test_endpoint_parser("wss://a.b.c:333/abc/efg?q=1", false);
    test_endpoint_parser("foo://a.b.c:333/abc/efg?q=1", false);
    test_endpoint_parser("wss://a.b.c:333/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg/abc/efg", false);
    test_endpoint_parser(":1234", true);
    test_endpoint_parser("", true);
    test_endpoint_parser("/abc", false);
    test_endpoint_parser("/abc", true);

    if(argc != 2)
    {
        printf("\n Usage: %s url \n",argv[0]);
        return 1;
    }

    ws_endpoint endpoint;
    r = ws_parse_url(argv[1], &endpoint, false);
    if (r < 0)
    {
        printf("\nError in ws_url_parse: %d\n", r);
        return 1;
    }

    ws_handle ws;
    r = ws_init(
            &ws,
            shared_network_buffer,
            NETWORK_BUFFER_LENGTH,
            endpoint,
            EXTRA_HEADERS,
            sizeof(EXTRA_HEADERS) / sizeof(EXTRA_HEADERS[0])
    );

    if (r < 0)
    {
        printf("\nError in ws_init: %d\n", r);
        return 1;
    }

	char *payload = "hello_world! aaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccddaaabbcccdd foobar";
	size_t payload_length = strlen(payload);
    memcpy(ws_get_outgoing_payload_ptr(&ws), payload, payload_length);
    r = ws_send_text(&ws, payload_length);
    if (r < 0)
    {
        printf("\nError in ws_send: %d\n", r);
        return 1;
    }

	printf("\nsent hello message\n");

    ws_received_message_type t;
    struct timeval timeout;

    do {
        timeout.tv_sec = 0;
        timeout.tv_usec = 100 * 1000; // 100 milliseconds

        r = ws_receive(&ws, &t, (void**) &payload, &timeout);
        if (r < 0)
        {
            if (r == WS_ERROR_REMOTE_SOCKET_CLOSED)
            {
                printf("Socket closed by server");
                return 0;
            }
            else
            {
                printf("\nError in ws_receive: %d\n", r);
                return 1;
            }
        }
        else if (r > 0)
        {
            if (t == WS_PAYLOAD_TYPE_PING)
            {
                r = ws_send_pong(&ws, (const void*) payload, (const size_t) r);
                if (r < 0)
                {
                    printf("\nError in ws_send_pong: %d\n", r);
                    return 1;
                }
            }
            else
            {
                payload_length = (size_t) r;

                printf("\nreceived: %d %d\n", t, (int) payload_length);
                printf("%.*s\n", (int) payload_length, payload);
            }
        }
    } while(1);

    return 0;
};