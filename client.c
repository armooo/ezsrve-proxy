/*
    Copyright Jason Michalski

    This file is part ofi ezsrve_proxy.

    ezsrve_proxy is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ezsrve_proxy is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ezsrve_proxy.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "client.h"
#include "log.h"

void client_clear(client *client) {
    client->sock = -1;
    client->free = CLIENT_BUFFER_SIZE;
    memset(client->buf, 0, CLIENT_BUFFER_SIZE);
    strncpy(client->name, "<cleared>", CLIENT_NAME_SIZE);
}

void client_init(client *client, int sock) {
    socklen_t peer_len;
    struct sockaddr_in peer;

    client->sock = sock;
    client->free = CLIENT_BUFFER_SIZE;
    memset(client->buf, 0, CLIENT_BUFFER_SIZE);

    peer_len = sizeof(peer);
    if ( getpeername(sock, (struct sockaddr*) &peer, &peer_len) < 0 ) {
        strncpy(client->name, "Unknown", CLIENT_NAME_SIZE);
    } else {
        snprintf(client->name, CLIENT_NAME_SIZE, "%s, %i", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    }

    write_log("connected %s\n", client->name);
}

void client_close(client *client) {
    write_log("close %s\n", client->name);
    close(client->sock);
    client_clear(client);
}

void client_write(client *client, const char *buf, size_t len) {
    if ( client->free < len ){
        write_log("%s Client buffer full\n", client->name);
        client_close(client);
        return;
    }
    memcpy(client->buf + (CLIENT_BUFFER_SIZE - client->free), buf, len);
    client->free -= len;
}

void client_send(client *client) {
    ssize_t sent, content_len;

    content_len = CLIENT_BUFFER_SIZE - client->free;
    sent = send(client->sock, client->buf, content_len, MSG_NOSIGNAL);
    if ( sent == -1 ) {
        log_error("Bad shit went down on send");
        client_close(client);
    }
    memmove(client->buf, &client->buf[sent], content_len - sent);
    client->free += sent;
}

int client_fd(client *client) {
    return client->sock;
}

int client_recv(client *client, char *buf, size_t len) {
    int r;
    r = recv(client->sock, buf, len, 0);
    if ( r == 0 ) {
       client_close(client);
    } else if ( r == -1 ) {
        log_error("Bad shit went down on recv");
        client_close(client);
    }
    return r;
}

int client_has_data(client *client){
    return client->free != CLIENT_BUFFER_SIZE;
}

int client_connected(client *client) {
    return client->sock != -1;
}

const char* client_name(client *client) {
    return client->name;
}
