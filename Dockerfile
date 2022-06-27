FROM alpine:3.15.4 AS build
WORKDIR /app

# Install dependencies
RUN apk add gcc musl-dev cmake make

COPY ./CMakeLists.txt ./CMakeLists.txt
COPY ./src/yip.c ./src/yip.c

# Run GCC directly, skip CMake
WORKDIR /app/build
RUN cmake -DCMAKE_BUILD_TYPE=Release ..
RUN cmake --build .

FROM alpine:3.15.4 AS production
WORKDIR /app

ENV THREADS 4
ENV PORT 80
ENV FLAGS ""

EXPOSE ${PORT}/tcp

RUN apk add curl
COPY --from=build /app/build/yip ./yip
RUN chmod +x ./yip

HEALTHCHECK CMD curl --fail http://localhost:${PORT} -H "X-Forwarded-For: 0.0.0.0" || exit 1
ENTRYPOINT ./yip  --count ${THREADS} --port ${PORT} ${FLAGS}
