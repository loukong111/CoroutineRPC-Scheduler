FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    g++ \
    libhiredis-dev \
    libmysqlcppconn-dev \
    libprotobuf-dev \
    make \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libhiredis-dev \
    libmysqlcppconn-dev \
    libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --system --uid 10001 --no-create-home --shell /usr/sbin/nologin corpcron

WORKDIR /app
COPY --from=build /src/build/corpcron_server /app/corpcron_server
COPY --from=build /src/build/corpcron_worker /app/corpcron_worker
COPY --from=build /src/config/server.example.conf /app/config/server.conf
COPY --from=build /src/config/worker.conf /app/config/worker.conf

EXPOSE 8081 8181

USER corpcron

CMD ["/app/corpcron_server", "--config", "/app/config/server.conf"]
