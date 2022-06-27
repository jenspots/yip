# yip

### About

In here you'll find a high performance, multithreaded HTTP server that only knows how to do one thing: return the client's IP address. It is extremely lightweight, and should be able to handle an immense amount of traffic when completed.

```
OPTIONS
  -h --help             Display this help page.
  -p --port  <u_int>    Choose port number.
  -c --count <u_int>    Specify number of threads.
  -v --verbose          Be more verbose.
  -f --forward          Use "X-Forwarded-For" header to determine IP.
```

#### Installation

#### Arch Based Systems

The development version of yip can be found in the Arch User Repositories.

```shell
$ yay -S yip-git
```

#### Manual

Assuming you have cloned the repository and navigated into it, run:

```shell
# Temporary new directory
mkdir build
cd build

# Generate build files
cmake ..

# Build yip and install
cmake --build .
cmake --install .
```

Note that, despite yip being fairly platform-agnostic, there might be inconsistencies across different operating systems. For example, in macOS, `socket.h` does not scale properly beyond a single core. At least not when used as in yip. The main target is modern Linux using the GCC compiler.

#### Usage

The program is mainly written for use with curl, although any typical HTTP client will do. A successful reply is always of the following form.

```
HTTP/1.1 200 OK
Connection: close
Content-length: 9

127.0.0.1
```

This allows the following curl query.

```shell
$ curl localhost
127.0.0.1 
```

If a server or connection issue were to arise, yip answers with status code 500. In this case, curl would return a non-zero code if used with the switch `--fail`.

```
HTTP/1.1 500 Internal Server Error
Connection: close
Content-Length: 0
```

### Motivation

I've grown tired of Googling "what's my IP address" since I don't like to rely on a GUI for such a trivial task, and I simply am unable to remember the domain name of that one service that works with an easy curl query. Given how small the implementation would be, I figured it would be fun to try and write a C program using `pthread.h` and `socket.h`.

### Benchmarks

TODO
