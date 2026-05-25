#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include "libs/base64.h"
#include "libs/sha1.h"
#include "libs/htmlPage.h"
#include "libs/logo.h"
#include "webvnc.h"


static int wake_pipe[2];
int XWEBVNC_http_server_port = 8080;

void ws_wakeup(void);
void ws_setup_wakepipe(void);


Websocket * ws_init(void) {
    signal(SIGPIPE,SIG_IGN);
    Websocket * ws = malloc(sizeof(Websocket));
    ws->server_fd = 0;
    ws->clients = 0;
    ws->socketPort = XWEBVNC_http_server_port;
    ws->stop = false;
    ws->ready = 0;
    ws->callBack = NULL;
    ws->callBackMsg = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ws->client_socket[i] = 0;
        ws->ws_client_socket[i] = 0;
    }
    ws_setup_wakepipe();
    return ws;
}

void ws_setup_wakepipe(void) {
    if (pipe(wake_pipe) < 0) {
        perror("pipe");
        exit(1);
    }
    fcntl(wake_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(wake_pipe[1], F_SETFL, O_NONBLOCK);
}

void ws_wakeup(void) {
    write(wake_pipe[1], "x", 1);
}

void ws_assign(Websocket *ws, void (*cbm)(char *data, uint64_t len, int sid), void (*cb)(int sid)) {
    ws->callBackMsg = cbm;
    ws->callBack = cb;
}

void ws_begin(Websocket *ws, int port) {
    ws->socketPort = port;
}

void ws_close(Websocket *ws) {
    if (!ws) return;
    ws->stop = true;
    ws_wakeup();
}


int ws_has_client(Websocket *ws) {
    return ws->clients;
}

uint64_t ws_get_payload_len(unsigned char *data) {
    uint64_t payload_len = data[1] & 0x7F;

    if (payload_len == 126) {
        payload_len = (data[2] << 8) | data[3];
    }
    else if (payload_len == 127) {
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | data[2 + i];
        }
    }

    return payload_len;
}

void ws_decode(unsigned char *data, char *result) {
    if (data[0] == 0) return;
    uint64_t payload_len = data[1] & 0x7F;
    size_t offset = 2;
    if (payload_len == 126) {
        payload_len = (data[2] << 8) | data[3];
        offset += 2;
    }
    else if (payload_len == 127) {
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | data[2 + i];
        }
        offset += 8;
    }
    unsigned char *mask = &data[offset];
    offset += 4;
    for (uint64_t i = 0; i < payload_len; i++) {
        result[i] = data[offset + i] ^ mask[i % 4];
    }
}

void ws_sendRaw(Websocket *ws, int startByte, char *data, long size, int sid) {
    char header[11];
    int moded = 0;
    header[0] = startByte;

    if (size <= 125) {
        header[1] = size;
        moded = 2;
    } else if (size <= 65535) {
        header[1] = 126;
        header[2] = ((size >> 8) & 255);
        header[3] = (size & 255);
        moded = 4;
    } else {
        header[1] = 127;
        header[2] = ((size >> 56) & 255);
        header[3] = ((size >> 48) & 255);
        header[4] = ((size >> 40) & 255);
        header[5] = ((size >> 32) & 255);
        header[6] = ((size >> 24) & 255);
        header[7] = ((size >> 16) & 255);
        header[8] = ((size >> 8) & 255);
        header[9] = (size & 255);
        moded = 10;
    }

    if (sid != -1) {
        send(ws->client_socket[sid], header, moded, 0);
        send(ws->client_socket[sid], data, size, 0);
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ws->client_socket[i] == 0 || ws->ws_client_socket[i] == 0) continue;
        send(ws->client_socket[i], header, moded, 0);
        send(ws->client_socket[i], data, size, 0);
    }
}

void ws_p_sendRaw(Websocket *ws, int opcode,
                  char *data1, char *data2,
                  long data1Size, long data2Size, int sid)
{
    long payloadLen = data1Size + data2Size;
    unsigned char header[14];
    int headerLen = 0;

    // FIN=1, opcode=2 (binary)
    header[0] = 0x82;

    if (payloadLen <= 125) {
        header[1] = (unsigned char)payloadLen;
        headerLen = 2;
    } else if (payloadLen <= 65535) {
        header[1] = 126;
        header[2] = (payloadLen >> 8) & 0xFF;
        header[3] = payloadLen & 0xFF;
        headerLen = 4;
    } else {
        header[1] = 127;
        for (int i=0;i<8;i++) {
            header[2+i] = (payloadLen >> (56 - 8*i)) & 0xFF;
        }
        headerLen = 10;
    }

    struct iovec iov[3];
    iov[0].iov_base = header;
    iov[0].iov_len  = headerLen;
    iov[1].iov_base = (void*)data1;
    iov[1].iov_len  = data1Size;
    iov[2].iov_base = (void*)data2;
    iov[2].iov_len  = data2Size;

    if (sid != -1) {
        ssize_t expected = headerLen + payloadLen;
        ssize_t n = writev(ws->client_socket[sid], iov, 3);
        if(n != expected){
            close(ws->client_socket[sid]);
            ws->client_socket[sid] = 0;
            ws->ws_client_socket[sid] = 0;
        }
    } else {
        for (int i=0; i<MAX_CLIENTS; i++) {
            if (ws->client_socket[i] == 0 || ws->ws_client_socket[i] == 0) continue;
            ssize_t expected = headerLen + payloadLen;
            ssize_t n = writev(ws->client_socket[i], iov, 3);
            if(n != expected){
                close(ws->client_socket[i]);
                ws->client_socket[i] = 0;
                ws->ws_client_socket[i] = 0;
            }
        }
    }
}



void ws_sendText(Websocket *ws, char *text, int sid) {
    ws_sendRaw(ws, 129, text, strlen(text), sid);
}

void ws_sendFrame(Websocket *ws, char *img, long size, int sid) {
    ws_sendRaw(ws, 130, img, size, sid);
}

int strcomncase_(char *text, const char *pattern, int start, int end);
int strcomncase_(char *text, const char *pattern, int start, int end) {
    int j = 0, k = 32;
    for (int i = start; i < end; i++, j++) {
        if (text[i] != pattern[j] && text[i] + k != pattern[j])
            return 0;
    }
    return 1;
}
int check_ws_auth(const char *request, int sd);
int check_ws_auth(const char *request, int sd)
{
    if(!isLoginRequired) return 1;
    char reqcopy[2048];
    strncpy(reqcopy, request, sizeof(reqcopy) - 1);
    reqcopy[sizeof(reqcopy) - 1] = '\0';
    char *ptr = strtok(reqcopy, "\n");
    while (ptr != NULL)
    {
        size_t plen = strlen(ptr);
        if (plen > 0 && ptr[plen - 1] == '\r')
            ptr[plen - 1] = '\0';
        if (strcomncase_(ptr, "authorization:", 0, 14) == 1)
        {
            const char *p = ptr + 15;
            while (*p == ' ' || *p == '\t')
                p++;
            if(p[0] == 'B' && p[1] == 'a' && p[2] == 's' && p[3] == 'i' && p[4] == 'c'){
                BYTE_ARRAY auth_info = base64_decode(p+6);
                auth_info.data[auth_info.size] = '\0';
                char *decoded = (char *)auth_info.data;
                char *colon = strchr(decoded, ':');
                 if (!colon) {
                    free(auth_info.data);
                    return 0;
                }
                *colon = '\0';
                char *user = decoded;
                char *password = colon + 1;
                int ok = check_password(user, password);
                free(auth_info.data);
                return ok;
            }
            break;
        }
        ptr = strtok(NULL, "\n");
    }
    return 0;
}

void ws_handshake(Websocket *ws, unsigned char *data, int sd, int sid)
{
    bool flag = false;

    // Make a writable copy of the request line(s) — strtok will modify it.
    char reqcopy[1024];
    strncpy(reqcopy, (const char *)data, sizeof(reqcopy)-1);
    reqcopy[sizeof(reqcopy)-1] = '\0';

    char *ptr = strtok(reqcopy, "\n");
    char key[1024];
    key[0] = '\0';

    while (ptr != NULL)
    {
        size_t plen = strlen(ptr);
        if (plen > 0 && ptr[plen-1] == '\r') ptr[plen-1] = '\0';
        if (strcomncase_(ptr, "sec-websocket-key:", 0, 18) == 1)
        {
            if (!check_ws_auth((const char *)data, sd))
            {
                close(sd);
                ws->client_socket[sid] = 0;
                ws->ws_client_socket[sid] = 0;
                if (ws->clients > 0) ws->clients--;
                return;
            }
            const char *p = ptr + 18;
            while (*p == ' ' || *p == '\t') p++;
            strncpy(key, p, sizeof(key)-1);
            key[sizeof(key)-1] = '\0';
            char *hash = generate_websocket_accept(key); // returns malloc'd string
            if (!hash) {
                send(sd, "HTTP/1.1 500 Internal Server Error\r\n\r\n", 37, 0);
                close(sd);
                ws->client_socket[sid] = 0;
                return;
            }
            char response[1024];
            int n = snprintf(response, sizeof(response),
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Server: PIwebVNC (by Jishan)\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n"
                "\r\n",
                hash);
            if (n > 0 && n < (int)sizeof(response)) {
                send(sd, response, (size_t)n, 0);
            } else {
                send(sd, response, strlen(response), 0);
            }

            free(hash);

            ws->ws_client_socket[sid] = 1;
            ws->ready = 1;
            flag = true;
            break;
        }

        ptr = strtok(NULL, "\n");
    }

    if (!flag)
    {
        char header[256];  // plenty of space for HTTP headers

        if (strcomncase_((char*) data, "GET /favicon.ico", 0, 16) ||
            strcomncase_((char*) data, "GET /logo.png", 0, 13)) {
            int hlen = sprintf(header,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/png\r\n"
                "Content-Length: 14910\r\n"
                "Cache-Control: max-age=14400\r\n"   // same as CF uses
                "Connection: close\r\n"
                "Server: PIwebVNC\r\n"
                "\r\n");
            send(ws->client_socket[sid], header, hlen, 0);
            send(ws->client_socket[sid], ___xwebvnc_libs_assets_vnclogo_PNG, ___xwebvnc_libs_assets_vnclogo_PNG_len, 0);
        } else {
            // Check websocket auth before upgrade
            if (!check_ws_auth((const char *)data, sd))
            {
                send(sd,
                "HTTP/1.1 401 Unauthorized\r\n"
                "WWW-Authenticate: Basic realm=\"PIwebVNC Secure WebSocket\"\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n",
                145,
                0);
                close(sd);
                ws->client_socket[sid] = 0;
                ws->ws_client_socket[sid] = 0;
                if (ws->clients > 0) ws->clients--;
                return;
            }
            int hlen = sprintf(header,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Server: PIwebVNC (by Jishan)\r\n"
                "\r\n");
            send(ws->client_socket[sid], header, hlen, 0);
            send(ws->client_socket[sid], ___xwebvnc_libs_assets_index_html, ___xwebvnc_libs_assets_index_html_len, 0);
        }
        close(sd);
        ws->client_socket[sid] = 0;
        ws->ws_client_socket[sid] = 0;
        if (ws->clients > 0) ws->clients--;
    }
    else {
        if (ws->callBack != NULL)
        {
            (*ws->callBack)(sid);
        }
    }
}


void ws_connections(Websocket *ws) {
     int new_socket, valread = 0;
    struct sockaddr_in address;
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    int opt = 1, max_sd = 0;
    int addrlen = sizeof(address);
    unsigned char buffer[1024] = {0};
    fd_set readfds;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(ws->socketPort);

    if ((ws->server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(ws->server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    // timeout
    if (setsockopt(ws->server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,sizeof timeout) < 0)
        perror("setsockopt failed\n");
    if (setsockopt(ws->server_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,sizeof timeout) < 0)
        perror("setsockopt failed\n");
    //
    if (bind(ws->server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(ws->server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    ErrorF("[WsocVNC] > LOG: Waiting For Client Connections : Port %d Opened (HTTP/WS)\n",ws->socketPort);
    while (!ws->stop)
    {
        FD_ZERO(&readfds);
        FD_SET(ws->server_fd, &readfds);
        max_sd = ws->server_fd;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int sd = ws->client_socket[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }
        FD_SET(wake_pipe[0], &readfds);
        if (wake_pipe[0] > max_sd) max_sd = wake_pipe[0];
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR))
        {
            perror("bind failed");
        }

        if (FD_ISSET(wake_pipe[0], &readfds)) {
            char buf[32];
            read(wake_pipe[0], buf, sizeof(buf)); // drain pipe
            if (ws->stop) {
                ErrorF("[WsocVNC] > LOG: Server Shutdown Wakepipe triggered\n");
                break; // only break if ws->stop really set
            }
        }

        if (FD_ISSET(ws->server_fd, &readfds))
        {
            if ((new_socket = accept(ws->server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                continue;
            }
            int flags = fcntl(ws->server_fd,F_GETFL,0);
            fcntl(ws->server_fd,F_SETFL,flags | O_NONBLOCK);
            ws->ready = 1;
            bool isAccespted = false;
            ErrorF("[WsocVNC] > LOG: New Connection : Client_No [%d] -> socket_fd=%d, ipAddr=%s, Port=%d\n", ws->clients, new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (ws->client_socket[i] == 0 && ws->clients < MAX_CLIENTS)
                {
                    ws->client_socket[i] = new_socket;
                    ws->clients++;
                    isAccespted = true;
                    break;
                }
            }
            if (!isAccespted)
            {
                //reject connection
                send(new_socket, "HTTP/1.1 400 Bad Request\r\n\r\n", 25, 0);
                close(new_socket);
                ErrorF("[WsocVNC] > ERR: Max connections reached\n");
            }
        }
        else
        {
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (ws->client_socket[i] == 0)
                    continue;
                int sd = ws->client_socket[i];
                if (FD_ISSET(sd, &readfds))
                {
                    if ((valread = read(sd, buffer, 1024)) == 0)
                    {
                        getpeername(sd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
                        ErrorF("[WsocVNC] > LOG: Client [%d] Disconnected : ip=%s, port=%d\n", ws->clients, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                        close(sd);
                        ws->client_socket[i] = 0;
                        ws->ws_client_socket[i] = 0;
                        ws->clients--;
                    }
                    else
                    {
                        buffer[valread] = '\0';
                        if (buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T')
                        {
                            ws_handshake(ws, buffer, sd, i);
                        }
                        else if ((int)buffer[0] == 136)
                        {
                            close(sd);
                            ws->client_socket[i] = 0;
                            ws->ws_client_socket[i] = 0;
                            ws->clients--;
                            ErrorF("[WsocVNC] > LOG: Host disconnected : %d | current active clients %d\n", sd,ws->clients);
                        }
                        else
                        {
                            if (ws->callBackMsg != NULL)
                            {
                                uint64_t payload_len = ws_get_payload_len(buffer);
                                if(payload_len < 127){
                                    char inputData[200]={0};
                                    ws_decode(buffer , inputData);
                                    ws->callBackMsg(inputData, payload_len, i);
                                } else {
                                  size_t header_size = 2;

                                  if ((buffer[1] & 0x7F) == 126)
                                    header_size += 2;
                                  else if ((buffer[1] & 0x7F) == 127)
                                    header_size += 8;
                                  header_size += 4; // mask

                                  uint64_t full_frame_size = header_size + payload_len;

                                  unsigned char *frame = malloc(full_frame_size);
                                  if (!frame)
                                    return;
                                  memcpy(frame, buffer, valread);
                                  size_t total_read = valread;

                                  while (total_read < full_frame_size) {
                                    ssize_t n =
                                        read(sd, frame + total_read,
                                             full_frame_size - total_read);
                                    if (n <= 0) {
                                      free(frame);
                                      return;
                                    }
                                    total_read += n;
                                  }
                                  char *inputData = malloc(payload_len);
                                  if (!inputData) {
                                    free(frame);
                                    return;
                                  }

                                  ws_decode(frame, inputData);
                                  ws->callBackMsg(inputData, payload_len, i);
                                  free(inputData);
                                  free(frame);
                                }  
                            }
                        }
                    }
                }
            }
        }
    }
     for (int i = 0; i < MAX_CLIENTS; i++) {
        if (ws->client_socket[i] > 0) {
            close(ws->client_socket[i]);
            ws->client_socket[i] = 0;
            ws->ws_client_socket[i] = 0;
            ws->clients--;
        }
    }
    if (ws->server_fd > 0) close(ws->server_fd);
    ErrorF("[WsocVNC] > LOG: WebSocket closed successfully\n");
}