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
#include <regex.h>

/* Style codes to format terminal output. */
#define ANSI_STYLE_BOLD    "\033[1m"
#define ANSI_STYLE_RESET   "\033[22m"
#define BOLD(x) ANSI_STYLE_BOLD""x""ANSI_STYLE_RESET

/* Parameters that change the way yip works, ever so slightly. */
#define DEFAULT_PORT 80
#define THREAD_COUNT_DEFAULT 1
#define BACKLOG_SIZE 100
#define CACHE_SIZE 256
#define MESSAGE_BUFFER_SIZE 1024

/* Global configuration set by the user at runtime. */
static int verbose_flag = 0;
static int forward_flag = 0;
static int port = DEFAULT_PORT;

/* This regular expression gets used to parse HTTP requests. */
#define REGEX_HTTP_HEADER "\n[[:space:]]*X-Forwarded-For:[[:space:]]*([[:graph:]]+)[[:space:]]*\n"
static regex_t header_regex;

/* Message displayed as help page. */
static char* help_message =
        BOLD("yip: Lightweight, multi-threaded web server which echoes the client's "
        "IP address.\n")
        "Copyright (c) by Jens Pots, 2022\n"
        "Licensed under AGPL-3.0-only\n"
        "\n"
        BOLD("OPTIONS\n")
        "  -h --help\t\tDisplay this help page.\n"
        "  -p --port  <u_int>\tChoose port number.\n"
        "  -c --count <u_int>\tSpecify number of threads.\n"
        "  -v --verbose\t\tBe more verbose.\n"
        "  -f --forward\t\tUse \"X-Forwarded-For\" header to determine IP.\n";

/* If an error occurs, this message may be sent back to the client. */
static char* http_request_size_error = "ERROR: Your HTTP request could not be parsed. It may be too long.";

/**
 * If the argument indicates an error, the function will print to stdout and
 * exit the program using the appropriate error code.
 * @param error `-1` on error, anything else on success.
 */
void try_or_exit(int error)
{
    if (error == -1) {
        perror("ERROR");
        exit(errno);
    }
}

/**
 * Function that handles incoming requests until the end of time, or when the
 * program halts, whichever comes first.
 * @param arg A pointer to the socket_identifier received in `main`.
 * @warning Never returns.
 */
noreturn void* handle_request(void * arg)
{
    struct tm * time_info;
    int socket_identifier, handler, option_value = 1, error_occurred;
    char ip[MESSAGE_BUFFER_SIZE];
    char datetime[CACHE_SIZE];
    struct sockaddr_in client;
    time_t current_time;
    char cache[CACHE_SIZE] = "HTTP/1.1 200 OK\nConnection: close\nContent-length: ";
    char buffer[MESSAGE_BUFFER_SIZE];
    long result;
    regmatch_t capture_groups[2];
    long long ip_start, ip_end;
    unsigned long ip_length, amount_sent, amount_to_send, offset = strlen(cache);
    struct linger linger_options = {
        .l_onoff = 1,
        .l_linger = 0
    };

    /* The void pointer contains the original socket number. */
    socket_identifier = * (int*) arg;

    /* Required by `accept`. */
    socklen_t address_len = sizeof(client);

    /* Respond with the address and close the handling socket. */
    while (1) {
        handler = accept(socket_identifier, (struct sockaddr *) &client, &address_len);
        if (handler > 0) {
            error_occurred = 0;

            /* Disable TCP_WAIT. */
            setsockopt(handler, IPPROTO_TCP, TCP_NODELAY, &option_value, sizeof(int));

            /* Disable socket lingering, a.k.a. Nagle's Algorithm. */
            setsockopt(handler, SOL_SOCKET, SO_LINGER, &linger_options, sizeof(linger_options));

            /* Option A: use HTTP headers to determine the IP address. */
            if (forward_flag) {
                /* yip is designed to parse small requests. If it's larger than
                 * INCOMING_MESSAGE_BUFFER_SIZE we just report an error. */
                if (recv(handler, buffer, MESSAGE_BUFFER_SIZE, 0) > 0) {
                    error_occurred = regexec(&header_regex, buffer, 2, capture_groups, 0);
                } else {
                    error_occurred = 1;
                }

                if (!error_occurred) {
                    ip_start = capture_groups[1].rm_so;
                    ip_end = capture_groups[1].rm_eo;
                    strncpy(ip, &buffer[ip_start], ip_end - ip_start);
                } else {
                    strcpy(ip, http_request_size_error);
                }
            }

            /* Option B: Use the TCP tuple. */
            else {
                if (client.sin_family == AF_INET) {
                    inet_ntop(AF_INET, &client.sin_addr, ip, INET_ADDRSTRLEN);
                } else {
                    inet_ntop(AF_INET6, &client.sin_addr, ip, INET6_ADDRSTRLEN);
                }
            }

            ip_length = strlen(ip);
            sprintf(cache + offset, "%lu", ip_length);

            /* If the IP address length contains two digits, we must move everything
             * with one character to the right. This is represented using "shift" */
            int shift = ip_length < 10 ? 0 : 1;
            *(cache + offset + 1 + shift) = '\n';
            *(cache + offset + 2 + shift) = '\n';
            strcpy(cache + offset + 3 + shift, ip);
            *(cache + offset + 3 + shift + ip_length) = '\0';

            /* Write data to the socket. */
            amount_sent = 0;
            amount_to_send = strlen(cache);
            while (amount_sent < amount_to_send) {
                result = send(handler, cache + amount_sent, amount_to_send - amount_sent, 0);
                if (result != -1) {
                    amount_sent += result;
                } else {
                    error_occurred = 1;
                    perror("ERROR");
                    break;
                }
            }

            /* Close the TCP connection and corresponding socket. */
            result = close(handler); // TODO: this may block
            if (result == -1) {
                error_occurred = 1;
                perror("ERROR");
            }

            /* Log IP address and time to stdout, if desired. */
            if (verbose_flag) {
                time(&current_time);
                time_info = localtime(&current_time);
                strftime(datetime, 80, "%FT%T%z", time_info); // ISO 8601
                printf("%s\t%s\t%s\n", datetime, error_occurred ? "ERROR" : "OK\t", ip);
            }

        } else {
            perror("ERROR");
        }
    }
}

int main(int argc, char ** argv)
{
    int socket_identifier, thread_count = THREAD_COUNT_DEFAULT, done;
    struct sockaddr_in server;
    pthread_t thread_id;

    /* These values define which switches are legal. */
    char options[] = "vc:hp:f";
    struct option long_options[] = {
        {"verbose",   no_argument,       &verbose_flag,  1  },
        {"port",      required_argument, NULL,          'p' },
        {"count",     required_argument, NULL,          'c' },
        {"forwarded", no_argument,       &forward_flag, 'f' },
        {NULL,        0,                 NULL,           0  }
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
            case 'v':
                verbose_flag = 1;
                break;
            case 'f':
                forward_flag = 1;
                break;
            case 'h':
                printf("%s", help_message);
                exit(-1);
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                printf("Unknown parameter\n");
                exit(-1);
        }
    }

    if (forward_flag) {
        try_or_exit(regcomp(&header_regex, REGEX_HTTP_HEADER, REG_EXTENDED));
        printf("Basing response on header \"X-Forwarded-For\"\n");
    }

    if (verbose_flag) {
        printf("Listening on port: %d\nThread count: %d\n\n", port, thread_count);
    }

    /* Configure the socket. */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    /* Create the socket. */
    socket_identifier = socket(PF_INET, SOCK_STREAM, 0);
    try_or_exit(socket_identifier);

    /* Bind to the socket. */
    try_or_exit(bind(socket_identifier, (const struct sockaddr *) &server, sizeof(server)));

    /* Set the program to start listening. */
    try_or_exit(listen(socket_identifier, BACKLOG_SIZE));

    /* Spawn worker threads. The current thread is the zeroth, so i = 1. */
    for (int i = 1; i < thread_count; ++i) {
        try_or_exit(pthread_create(&thread_id, NULL, handle_request, &socket_identifier));
    }

    /* If the main thread exits, the program exits as a whole. Instead of
     * yielding the main thread, we might as well do something with it. */
    handle_request(&socket_identifier);
}
