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
#include <stdnoreturn.h>
#include <getopt.h>
#include <stdio.h>

/* Style codes to format terminal output. */
#define ANSI_STYLE_BOLD    "\033[1m"
#define ANSI_STYLE_RESET   "\033[22m"
#define BOLD(x) ANSI_STYLE_BOLD""x""ANSI_STYLE_RESET

#define PORT 80

/* Global flag set by the user at runtime. */
static int verbose_flag = 0;
static int log_flag = 0;

/* Message displayed as help page. */
static char* help_message =
        "Lightweight, multi-threaded web server which echoes the client's IP address.\n"
        "Copyright (c) by Jens Pots\n"
        "Licensed under AGPL-3.0-only\n"
        "\n"
        BOLD("OPTIONS\n")
        "  -h --help\t\tDisplay this help page.\n"
        "  -c --count <u_int>\tSpecify number of threads.\n"
        "  -v --verbose\t\tBe more verbose.\n"
        "  -l --log\t\tLog addresses to stdout.\n";

/**
 * Function that handles incoming requests until the end of time, or when the
 * program halts, whichever comes first.
 * @param arg A pointer to the socket_identifier received in `main`.
 * @warning Never returns.
 */
noreturn void* handle_request(void * arg)
{
    int socket_identifier, handler;
    char *ip;
    struct sockaddr_in client;

    /* The void pointer contains the original socket number. */
    socket_identifier = * (int*) arg;

    /* Required by `accept`. */
    socklen_t address_len = sizeof(client);

    /* Respond with the address and close the handling socket. */
    while (1) {
        handler = accept(socket_identifier, (struct sockaddr *) &client, &address_len);
        ip = inet_ntoa(client.sin_addr);
        write(handler, "HTTP/1.1 200 OK\n\n", 16);
        write(handler, "Content-length: 9\n\n", 19);
        write(handler, ip, strlen(ip));
        close(handler);

        if (log_flag) {
            printf("Served client: %s\n", ip);
        }
    }
}

int main(int argc, char ** argv)
{
    int socket_identifier, thread_count;
    struct sockaddr_in server;
    pthread_t thread_id;

    /* These values define which switches are legal. */
    char options[] = "c:h";
    struct option long_options[] = {
        {"verbose", no_argument,       &verbose_flag, 1  },
        {"count",   required_argument, NULL,          'c'},
        {"log",     no_argument,       &log_flag,     1  },
        {NULL,      0,                 NULL,          0  }
    };

    /* Parse runtime arguments. */
    while (1) {
        switch (getopt_long(argc, argv, options, long_options, NULL)) {
            case -1:
                goto exit_while_loop;
            case 'c':
                thread_count = atoi(optarg);
                printf("Assigning %d worker threads\n", thread_count);
                break;
            case 'l':
                printf("Logging users\n");
                break;
            case 'h':
                printf("%s", help_message);
                exit(-1);
            case '?':
                printf("Unknown parameter\n");
                exit(-1);
        };
    }
    exit_while_loop:

    /* Configure the socket. */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    /* Create the socket, bind it, and start listening. */
    socket_identifier = socket(PF_INET, SOCK_STREAM, 0);
    bind(socket_identifier, (const struct sockaddr *) &server, sizeof(server));
    listen(socket_identifier, 1);

    /* Spawn worker threads. The current thread is the zeroth, so i = 1. */
    for (int i = 1; i < thread_count; ++i) {
        pthread_create(&thread_id, NULL, handle_request, &socket_identifier);
    }

    /* If the main thread exits, the program exits as a whole. Instead of
     * yielding the main thread, we might as well do something with it. */
    handle_request(&socket_identifier);
}
