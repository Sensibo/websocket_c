# websocket_c
A partial implementation of the WebSocket protocol in C, designed for use in embedded systems

## Features:

1. Static memory usage at runtime - no `malloc`s.
2. Caller-provided memory for incoming packets.
3. Supports 30X redirect responses with relative or absolute URLs.
4. Custom HTTP headers (e.g, for authentication) may be provided in the connection request.
5. "Ping" message implementation for heartbeats/keepalive.

## Building

`cmake .`

`make`

### Tests

The included "main" will run some unit tests and connect to the URL provided at the command line.
If you don't have a websocket server to test with, see https://github.com/vi/websocat

### Customization

* If you're using a custom IP stack, replace gethostbyname and socket calls `read` and `write` with your platform's implementations.
* Remove or replace debug message calls `hex_dump` and `printf`.

## Input URL format

See the included "main" for examples of URLs accepted.



