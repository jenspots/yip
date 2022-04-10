FROM gcc:9.4.0 AS build
WORKDIR /app

# yip is a single C file.
COPY yip.c yip.c

# Run GCC directly, skip CMake
RUN gcc -o yip yip.c -O3 -lpthread

FROM debian:stable-slim AS production
ENV THREADS 1
EXPOSE 80/tcp
WORKDIR /app

COPY --from=build /app/yip ./yip
RUN chmod +x ./yip

ENTRYPOINT ./yip -c ${THREADS} --verbose
