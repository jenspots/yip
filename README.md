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

### Motivation

I've grown tired of Googling "what's my IP address" since I don't like to rely on a GUI for such a trivial task, and I simply am unable to remember the domain name of that one service that works with an easy `cURL` query. Given how small the implementation would be, I figured it would be fun to try and write a C program using `pthread.h` and `socket.h`.

### A work in progress

Some things that still need to be done.

- [ ] Error handling
- [ ] Code cleanup
- [ ] Make the UX a bit nicer
- [ ] Inspect performance scaling when thread count increases

### Benchmarks

TODO
