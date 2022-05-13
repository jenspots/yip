/*
 * yip: Simple web server that responds with the client's IP address.
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
#include <stdnoreturn.h>
#include <getopt.h>
#include <stdio.h>
#include <time.h>
#include <netinet/tcp.h>
#include <errno.h>

/* Style codes to format terminal output. */
#define ANSI_STYLE_BOLD    "\033[1m"
#define ANSI_STYLE_RESET   "\033[22m"
#define BOLD(x) ANSI_STYLE_BOLD""x""ANSI_STYLE_RESET

#define PORT 80
#define THREAD_COUNT_DEFAULT 1
#define BACKLOG_SIZE 100
#define CACHE_SIZE 256

/* Global flag set by the user at runtime. */
static int verbose_flag = 0;

/* Message displayed as help page. */
static char* help_message =
        "yip: Lightweight, multi-threaded web server which echoes the client's IP address.\n"
        "Copyright (c) by Jens Pots\n"
        "Licensed under AGPL-3.0-only\n"
        "\n"
        BOLD("OPTIONS\n")
        "  -h --help\t\tDisplay this help page.\n"
        "  -c --count <u_int>\tSpecify number of threads.\n"
        "  -v --verbose\t\tBe more verbose.\n";

/**
 * Function that handles incoming requests until the end of time, or when the
 * program halts, whichever comes first.
 * @param arg A pointer to the socket_identifier received in `main`.
 * @warning Never returns.
 */
noreturn void* handle_request(void * arg)
{
    int socket_identifier, handler;
    char ip[INET6_ADDRSTRLEN];
    struct sockaddr_in client;
    time_t current_time;
    char cache[CACHE_SIZE] = "HTTP/1.1 200 OK\nConnection: close\nContent-length: ";
    long result;
    unsigned long ip_length, amount_sent, amount_to_send, offset = strlen(cache);

    /* The void pointer contains the original socket number. */
    socket_identifier = * (int*) arg;

    /* Required by `accept`. */
    socklen_t address_len = sizeof(client);

    /* Respond with the address and close the handling socket. */
    while (1) {
        handler = accept(socket_identifier, (struct sockaddr *) &client, &address_len);
        if (handler == -1) {
            perror("ERROR");
            continue;
        }

        result = 1;
        setsockopt(handler, IPPROTO_TCP, TCP_NODELAY, &result, sizeof(long));

        /* Craft and write the message. */
        if (client.sin_family == AF_INET) {
            inet_ntop(AF_INET, &client.sin_addr, ip, INET_ADDRSTRLEN);
        } else {
            inet_ntop(AF_INET6, &client.sin_addr, ip, INET6_ADDRSTRLEN);
        }

        ip_length = strlen(ip);
        sprintf(cache + offset, "%lu", ip_length);

        /* The IP address is either [0, 9] digits long, or [10, 100]. */
        if (ip_length < 10) {
            *(cache + offset + 1) = '\n';
            *(cache + offset + 2) = '\n';
            strcpy(cache + offset + 3, ip);
            *(cache + offset + 3 + ip_length) = '\0';
        } else {
            *(cache + offset + 2) = '\n';
            *(cache + offset + 3) = '\n';
            strcpy(cache + offset + 4, ip);
            *(cache + offset + 4 + ip_length) = '\0';
        }

        /* Write data to the socket. */
        amount_sent = 0;
        amount_to_send = strlen(cache);
        while (amount_sent < amount_to_send) {
            result = send(handler, cache + amount_sent, amount_to_send - amount_sent, 0);
            if (result != -1) {
                amount_sent += result;
            } else {
                perror("ERROR");
                exit(-1); // TODO
            }
        }

        /* Do not permit any new data to be send from either side. */
        result = shutdown(handler, SHUT_RDWR);
        if (result == -1) {
            switch(errno) {
                case ENOTCONN: {
                    break;
                }
                default: {
                    perror("ERROR");
                    exit(-1); // TODO
                }
            }
        }

        /* Close the TCP connection and corresponding socket. */
        result = close(handler);
        if (result == -1) {
            perror("ERROR");
            exit(-1); // TODO
        }

        /* Log IP address and time to stdout, if desired. */
        if (verbose_flag) {
            current_time = time(NULL);
            printf("%s\t%s", ip, ctime(&current_time));
        }
    }
}

int main(int argc, char ** argv)
{
    int socket_identifier, thread_count = THREAD_COUNT_DEFAULT, error, done;
    struct sockaddr_in server;
    pthread_t thread_id;

    /* These values define which switches are legal. */
    char options[] = "c:h";
    struct option long_options[] = {
        {"verbose", no_argument,       &verbose_flag, 1  },
        {"count",   required_argument, NULL,          'c'},
        {NULL,      0,                 NULL,          0  }
    };

    /* Parse runtime arguments. */
    done = 0;
    while (!done) {
        switch (getopt_long(argc, argv, options, long_options, NULL)) {
            case -1:
                done = 1;
                break;
            case 'c':
                thread_count = atoi(optarg);
                break;
            case 'h':
                printf("%s", help_message);
                exit(-1);
            case '?':
                printf("Unknown parameter\n");
                exit(-1);
        }
    }

    if (verbose_flag) {
        printf("Thread count: %d\n", thread_count);
    }

    /* Configure the socket. */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    /* Create the socket, bind it, and start listening. */
    socket_identifier = socket(PF_INET, SOCK_STREAM, 0);
    if (socket_identifier == -1) {
        perror("ERROR");
        exit(-1); // TODO
    }

    int optval = 1;
    setsockopt(socket_identifier, SOL_SOCKET, SO_REUSEADDR & SO_REUSEPORT, &optval, sizeof(int));
    setsockopt(socket_identifier, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int));

    error = bind(socket_identifier, (const struct sockaddr *) &server, sizeof(server));
    if (error == -1) {
        perror("ERROR");
        exit(-1); // TODO
    }

    error = listen(socket_identifier, BACKLOG_SIZE);
    if (error == -1) {
        perror("ERROR");
        exit(-1); // TODO
    }

    /* Spawn worker threads. The current thread is the zeroth, so i = 1. */
    for (int i = 1; i < thread_count; ++i) {
        error = pthread_create(&thread_id, NULL, handle_request, &socket_identifier);
        if (error != 0) {
            exit(-1);
        }
    }

    /* If the main thread exits, the program exits as a whole. Instead of
     * yielding the main thread, we might as well do something with it. */
    handle_request(&socket_identifier);
}
