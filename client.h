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

#ifndef CLIENT_H
#define CLIENT_H 1

#define CLIENT_BUFFER_SIZE 16384
#define CLIENT_NAME_SIZE 40

typedef struct {
    int sock;
    size_t free;
    char buf[CLIENT_BUFFER_SIZE  + 1];
    char name[CLIENT_NAME_SIZE + 1];
} client;


void client_clear(client*);
void client_init(client*, int);
void client_close(client*);
void client_write(client*, const char*, size_t);
void client_send(client*);
int client_fd(client* );
int client_recv(client*, char*, size_t);
int client_has_data(client*);
int client_connected(client*);
const char* client_name(client*);

#endif
