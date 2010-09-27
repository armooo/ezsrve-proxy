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
#include <math.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "client.h"

#define BUFFER_SIZE 8192
#define MAX_CLIENTS 70
#define TIMEOUT_SEC 3
#define COMMAND_DELAY_USEC 80000


/*************************************************************************
 What kind of magic is this you ask? This is a proxy that make a ezsvre
 suck less hard.

 We hold one connection to the ezsvre open. All traffic from it is sent to
 all connected clients. Simple.

 To sent commands to the ezsvre we need to make sure only one client is
 speaking at a time. This is the active_client.

 Whenever we read from a client it is made the active_client until we
 read something from the ezsvre. We guess that is the response.

 The trickest part is playing nice with select if it returns more than one
 socket to read. We have an extra fd_set old_rd. We put all the extra fd
 in it. As long as active_client is not NULL only the active client will
 be in the new rd to select. At the top of the loop if active_client is
 null we will look in old_rd for a client to make the new active_client
 before anyone else a shot to get in rd.

 We also add a command delay so we don't spam the ezsre and it stops
 working.

 So that is the kind of magic we have here and it seems to work.
*************************************************************************/

//Macros yanked from sys/time.h
# define timercmp(a, b, CMP) 						      \
  (((a)->tv_sec == (b)->tv_sec) ? 					      \
   ((a)->tv_usec CMP (b)->tv_usec) : 					      \
   ((a)->tv_sec CMP (b)->tv_sec))

# define timersub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;			      \
    if ((result)->tv_usec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_usec += 1000000;					      \
    }									      \
  } while (0)

int max(int x, int y) {
    return ((x) > (y) ? (x) : (y));
}

typedef struct {
    const char *ezsrve_address;
    int server_sock;
    fd_set rd;
    fd_set wr;
    fd_set old_rd;
    client ezsrve;
    client clients[MAX_CLIENTS];
    client *active_client;
    struct timeval last_sent_end;
} server_state;

void clear_active_client(server_state *state){
    state->active_client = NULL;
    gettimeofday(&state->last_sent_end, NULL);
}

void init_server_state(server_state *state, const char *ezsrve_address) {
    client_clear(&state->ezsrve);
    for ( int i = 0; i < MAX_CLIENTS; i++ ) {
        client_clear(&state->clients[i]);
    }
    clear_active_client(state);
    FD_ZERO(&state->old_rd);
    state->ezsrve_address = ezsrve_address;
}

void connect_eserv(server_state *state) {
    int sock;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    if ( getaddrinfo(state->ezsrve_address, "8002", &hints, &res) != 0 ) {
        printf("Error parsing hostname");
        _exit(3);
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if ( sock < 0 ){
        printf("Failed to create ezsrve socket\n");
        _exit(3);
    }

    for (;;) {
        if ( connect(sock, res->ai_addr, res->ai_addrlen) < 0 ) {
            perror("Failed to connect to ezsrve");
        } else {
            break;
        }
        sleep(5);
    }
    freeaddrinfo(res);
    client_init(&state->ezsrve, sock);
}

void reconnect_ezrve(server_state *state){
    for (int i=0; i<MAX_CLIENTS; i++) {
        if ( client_connected(&state->clients[i]) ){
            client_close(&state->clients[i]);
        }
    }
    connect_eserv(state);
}

void bind_server(server_state *state) {
    int sock;
    struct sockaddr_in addr;
    int on = 1;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) < 0 ) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8002);
    if ( bind(sock, (struct sockaddr *) &addr, sizeof(addr) ) < 0 ) {
        perror("Failed to bind");
        _exit(3);
    }
    listen(sock,5);

    state->server_sock = sock;
}

void delay_command(server_state *state) {
    struct timeval now, delta, delay, sleep;

    delay.tv_sec = 0;
    delay.tv_usec = COMMAND_DELAY_USEC;


    gettimeofday(&now, NULL);
    timersub(&now, &state->last_sent_end, &delta);
    if ( timercmp(&delta, &delay, <) ) {
        timersub(&delay, &delta, &sleep);
        printf("Waiting %i\n", (int)sleep.tv_usec);
        usleep(sleep.tv_usec);
    }

}

void handle_client_read(server_state *state, client *client) {
    int r;
    char buf[BUFFER_SIZE];

    r = client_recv(client, buf, sizeof(buf));
    if ( r == 0 || r == -1 ){
        if ( state->active_client == client ) {
            clear_active_client(state);
        }
        return;
    }

    state->active_client = client;
    client_write(&state->ezsrve, buf, r);
}

void handle_old_read_fds(server_state *state) {
    client *client;

    if ( state->active_client != NULL ){
        return;
    }

    for (int i=0; i<MAX_CLIENTS; i++){
        client = &state->clients[i];
        if ( client_connected(client) && FD_ISSET(client_fd(client), &state->old_rd) ) {
            FD_CLR(client_fd(client), &state->old_rd);
            handle_client_read(state, client);
            return;
        }
    }

    //this will clear out any closed FD that are stuck in here
    if ( state->active_client == NULL ) {
        FD_ZERO(&state->old_rd);
    }
}

int build_read_fds(server_state *state) {
    int nfds = 0;
    //Always have server and ezsrve
    FD_SET(state->server_sock, &state->rd);
    nfds = max(nfds, state->server_sock);
    if ( client_connected(&state->ezsrve) ) {
        FD_SET(client_fd(&state->ezsrve), &state->rd);
        nfds = max(nfds, client_fd(&state->ezsrve));
    }

    //All connected clients if not an active client
    if ( state->active_client != NULL && client_connected(state->active_client) ) {
        FD_SET(client_fd(state->active_client), &state->rd);
        nfds = max(nfds, client_fd(state->active_client));
    } else {
        for ( int i = 0; i < MAX_CLIENTS; i++ ) {
            client *client = &state->clients[i];
            if ( client_connected(client) ) {
                FD_SET(client_fd(client), &state->rd);
                nfds = max(nfds, client_fd(client));
            }
        }
    }
    return nfds;
}

int build_write_fds(server_state *state) {
    int nfds = 0;
    //Ezserv if has data
    if ( client_connected(&state->ezsrve) && client_has_data(&state->ezsrve) ) {
        FD_SET(client_fd(&state->ezsrve), &state->wr);
        nfds = max(nfds, client_fd(&state->ezsrve));
    }
    //All connected clients with data
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *client = &state->clients[i];
        if ( client_connected(client) && client_has_data(client) ) {
            FD_SET(client_fd(client), &state->wr);
            nfds = max(nfds, client_fd(client));
        }
    }
    return nfds;
}

void check_for_timeout(server_state *state){
    client *client;

    if ( FD_ISSET(state->server_sock, &state->rd) ) {
       return;
    }

    if ( client_connected(&state->ezsrve) &&
        ( FD_ISSET(client_fd(&state->ezsrve), &state->rd) ||
         FD_ISSET(client_fd(&state->ezsrve), &state->wr)
        )) {
        return;
    }

    for (int i=0; i<MAX_CLIENTS; i++) {
        client = &state->clients[i];
        if ( !client_connected(client) ){
            continue;
        }
        if ( FD_ISSET(client_fd(client), &state->rd) ||
            FD_ISSET(client_fd(client), &state->wr)){
            return;
        }
    }

    clear_active_client(state);
}

client* find_free_client(client clients[]) {
    for ( int i=0; i < MAX_CLIENTS; i++ ) {
        if ( !client_connected(&clients[i]) ) {
            return &clients[i];
        }
    }
    return NULL;
}

void handle_new_client(server_state *state) {
    client *new_client;
    int new_sock;

    new_sock = accept(state->server_sock, NULL, 0);
    new_client = find_free_client(state->clients);
    if ( new_client == NULL ) {
        printf("All client slots full closing socket\n");
        close(new_sock);
    } else {
        client_init(new_client, new_sock);
    }
}

int handle_ezsrve_read(server_state *state) {
    int r;
    char buf[BUFFER_SIZE];
    client *client;

    if ( state->active_client != NULL ) {
        clear_active_client(state);
    }

    r = client_recv(&state->ezsrve, buf, sizeof(buf));
    if ( r == 0 ){
        client_close(&state->ezsrve);
    } else if ( r != -1 ) {
        //send the data to all the clients
        for ( int i = 0; i < MAX_CLIENTS; i++ ) {
            client = &state->clients[i];
            if ( client_connected(client) ) {
                client_write(client, buf, r);
            }
        }
    }
    return r;
}

void check_client_reads(server_state *state) {
    client *client;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client = &state->clients[i];
        if ( client_connected(client) &&
            FD_ISSET(client_fd(client), &state->rd) ) {
            if (state->active_client == NULL ||
                state->active_client == client) {

                handle_client_read(state, client);
            } else {
                FD_SET(client_fd(client), &state->old_rd);
            }
        }
    }
}



void server(const char *ezsrve_address) {
    server_state state;
    int nfds, r;
    struct timeval r_timeout, *timeout;

    init_server_state(&state, ezsrve_address);
    connect_eserv(&state);
    bind_server(&state);

    for (;;) {
        nfds = 0;
        FD_ZERO(&state.rd);
        FD_ZERO(&state.wr);

        if ( !client_connected(&state.ezsrve) ) {
            reconnect_ezrve(&state);
        }

        delay_command(&state);
        handle_old_read_fds(&state);

        nfds = max(nfds, build_read_fds(&state));
        nfds = max(nfds, build_write_fds(&state));

        if ( state.active_client != NULL ) {
            timeout = &r_timeout;
            timeout->tv_sec = TIMEOUT_SEC;
            timeout->tv_usec = 0;
        } else {
            timeout = NULL;
        }
        r = select(nfds+1, &state.rd, &state.wr, NULL, timeout);
        if (r == -1){
            perror("Select failed");
            _exit(3);
        }

        check_for_timeout(&state);

        //reads
        //accept new connections
        if ( FD_ISSET(state.server_sock, &state.rd) ) {
            handle_new_client(&state);
        }
        if ( client_connected(&state.ezsrve) &&
            FD_ISSET(client_fd(&state.ezsrve), &state.rd) ) {

            handle_ezsrve_read(&state);
        }
        check_client_reads(&state);


        //writes
        if ( client_connected(&state.ezsrve) &&
            FD_ISSET(client_fd(&state.ezsrve), &state.wr) &&
            client_has_data(&state.ezsrve) ) {

            client_send(&state.ezsrve);
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client *client = &state.clients[i];
            if ( client_connected(client) &&
                FD_ISSET(client_fd(client), &state.wr) &&
                client_has_data(client) ) {

                client_send(client);
            }
        }
    }
}

int main(int argc, char* const argv[]) {
    int pid, daemonize = 0;
    char opt;

    while ( (opt = getopt(argc, argv, "d")) != -1 ) {
        switch (opt) {
            case 'd':
                daemonize = 1;
                break;
        }
    }

    if (argc - optind != 1) {
        printf("ezsrve_proxy [-d] <ezsrve address>\n");
        return 1;
    }

    if ( daemonize ) {
        pid = fork();
        if ( pid < 0 ) {
            perror("Fork failed");
            _exit(3);
        }
        if ( pid > 0 ) {
            //parent is done
            _exit(1);
        }

        if ( setsid() == -1 ){
            perror("setsid failed");
            _exit(3);
        }

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    server(argv[optind]);
    return 0;
}
