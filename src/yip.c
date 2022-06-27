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
    BOLD(
    "yip: Lightweight, multi-threaded web server which echoes the client's "
    "IP address.\n"
    )
    "Copyright (c) by Jens Pots, 2022\n"
    "Licensed under AGPL-3.0-only\n"
    "\n"
    BOLD("OPTIONS\n")
    "  -h --help\t\tDisplay this help page.\n"
    "  -p --port  <u_int>\tChoose port number.\n"
    "  -c --count <u_int>\tSpecify number of threads.\n"
    "  -v --verbose\t\tBe more verbose.\n"
    "  -f --forward\t\tUse \"X-Forwarded-For\" header to determine IP.\n";

/* Generic error message for all faulty requests.. */
char* http_internal_server_error =
        "HTTP/1.1 500 Internal Server Error\n"
        "Connection: close\n"
        "Content-Length: 0\n\n";

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

/***
 * Sends a string over a socket to the client.
 * @param handler The active socket identifier.
 * @param message A null-terminated string to send over the socket.
 * @return 0 if successful, 1 otherwise.
 */
int transmit(int handler, char * message)
{
    unsigned long sent = 0;
    unsigned long total = strlen(message);
    while (sent < total) {
        long result = send(handler, message + sent, total - sent, 0);
        if (result != -1) {
            sent += result;
        } else {
            perror("ERROR");
            return 1;
        }
    }
    return 0;
}

/**
 * Function that handles incoming requests until the end of time, or when the
 * program halts, whichever comes first.
 * @param arg A pointer to the socket_identifier received in `main`.
 * @warning Never returns.
 */
noreturn void* handle_request(void * arg)
{
    /* Some buffers. */
    char read_buffer[MESSAGE_BUFFER_SIZE];
    char ip[MESSAGE_BUFFER_SIZE];

    /* The thread continues to reuse this single array to construct a reply. */
    char http_message[CACHE_SIZE] =
        "HTTP/1.1 200 OK\n"
         "Connection: close\n"
         "Content-length: ";

    /* This is where the IP needs to be written to inside the message. */
    char * http_message_ip = http_message + strlen(http_message);

    /* The void pointer contains the original socket number. */
    int socket_id = * (int*) arg;

    /* Required by `accept`. */
    struct sockaddr_in client;
    socklen_t address_len = sizeof(client);

    /* Respond with the address and close the handling socket. */
    while (1) {
        /* Attempt to accept an incoming connection. */
        int handler = accept(socket_id, (struct sockaddr *) &client, &address_len);
        if (handler == 0) {
            perror("ERROR");
            continue;
        }

        /* Disable TCP_WAIT. */
        int opt_value = 1;
        setsockopt(handler, IPPROTO_TCP, TCP_NODELAY, &opt_value, sizeof(int));

        /* Disable socket lingering, a.k.a. Nagle's Algorithm. */
        struct linger linger_opt = { .l_onoff = 1, .l_linger = 0 };
        setsockopt(handler, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));

        /* Option A: use HTTP headers to determine the IP address. */
        ip[0] = '\0';
        if (forward_flag) {
            if (recv(handler, read_buffer, MESSAGE_BUFFER_SIZE, 0) > 0) {
                regmatch_t captured[2];
                if (regexec(&header_regex, read_buffer, 2, captured, 0) == 0) {
                    long long ip_start = captured[1].rm_so;
                    long long ip_end = captured[1].rm_eo;
                    strncpy(ip, &read_buffer[ip_start], ip_end - ip_start);
                }
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

        /* When an IP address was found, strlen will return greater than 0. */
        if (strlen(ip) > 0) {
            /* If the IP address length contains two digits, we must move
             * everything one character to the right with "shift". */
            unsigned long ip_length = strlen(ip);
            sprintf(http_message_ip, "%lu", ip_length);
            int shift = ip_length < 10 ? 0 : 1;
            *(http_message_ip + 1 + shift) = '\n';
            *(http_message_ip + 2 + shift) = '\n';
            strcpy(http_message_ip + 3 + shift, ip);
            *(http_message_ip + 3 + shift + ip_length) = '\0';
            transmit(handler, http_message);
        } else {
            transmit(handler, http_internal_server_error);
        }

        /* Close the TCP connection and corresponding socket. */
        if (close(handler) != 0) {
            perror("ERROR");
        }

        /* Log IP address and time to stdout, if desired. */
        if (verbose_flag) {
            char datetime[CACHE_SIZE];
            time_t current_time = time(NULL);
            struct tm * time_info = localtime(&current_time);
            strftime(datetime, 80, "%FT%T%z", time_info); // ISO 8601
            printf("%s\t", datetime);
            printf("%s\t", strlen(ip) > 0 ? "OK\t" : "Error");
            printf("%s\n", strlen(ip) > 0 ? ip : "Unknown");
            fflush(stdout);
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
                if (thread_count <= 0) {
                    errno = EINVAL;
                    perror("ERROR");
                    exit(errno);
                }
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
                if (port <= 0) {
                    errno = EINVAL;
                    perror("ERROR");
                    exit(errno);
                }
                break;
            case '?':
                printf("Unknown parameter\n");
                exit(-1);
        }
    }

    if (verbose_flag && forward_flag) {
        try_or_exit(regcomp(&header_regex, REGEX_HTTP_HEADER, REG_EXTENDED));
        printf("Basing response on header \"X-Forwarded-For\"\n");
    }

    if (verbose_flag) {
        printf("Listening on port: %d\nThread count: %d\n\n", port, thread_count);
        printf("TIME\t\t\t\t\t\tSTATUS\tIP\n");
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

    /* TODO: Some Docker IO weirdness. */
    fflush(stdout);

    /* Spawn worker threads. The current thread is the zeroth, so i = 1. */
    for (int i = 1; i < thread_count; ++i) {
        try_or_exit(pthread_create(&thread_id, NULL, handle_request, &socket_identifier));
    }

    /* If the main thread exits, the program exits as a whole. Instead of
     * yielding the main thread, we might as well do something with it. */
    handle_request(&socket_identifier);
}
