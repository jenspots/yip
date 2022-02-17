/*
 * Simple web server that responds with the client's IP address.
 *
 * Copyright (C) 2022 Jens Pots.
 * License: AGPL-3.0-only.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>

#define PORT 80

void* handle_request(void * arg)
{
    int socket_identifier, handler;
    char *ip;
    struct sockaddr_in client;
    socklen_t address_len = sizeof(client);

    /* The void pointer contains the original socket number. */
    socket_identifier = * (int*) arg;

    /* Respond with the address and close the handling socket. */
    handler = accept(socket_identifier, (struct sockaddr *) &client, &address_len);
    ip = inet_ntoa(client.sin_addr);
    write(handler, "HTTP/1.1 200 OK\n\n", 16);
    write(handler, "Content-length: 9\n\n", 19);
    write(handler, ip, strlen(ip));
    close(handler);
}

int main(int argc, char ** argv)
{
    int socket_identifier;
    struct sockaddr_in server;
    pthread_t thread_id;

    /* Configure the socket. */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    /* Create the socket, bind it, and start listening. */
    socket_identifier = socket(PF_INET, SOCK_STREAM, 0);
    bind(socket_identifier, (const struct sockaddr *) &server, sizeof(server));
    listen(socket_identifier, 1);

    /* Infinitely respond with the IP address. */
    while (1) {
        pthread_create(&thread_id, NULL, handle_request, &socket_identifier);
        pthread_join(thread_id, NULL);
    }
}
