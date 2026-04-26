FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libssl-dev \
    libsodium-dev \
    libjsoncpp-dev \
    libfmt-dev \
    locales \
    ca-certificates \
    curl \
    && locale-gen en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

WORKDIR /app

# Pre-download cpp-httplib to avoid FetchContent locale extraction bug
RUN mkdir -p /tmp/httplib && \
    curl -sL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.42.0.tar.gz \
    | tar xz -C /tmp/httplib --strip-components=1

# Copy source
COPY . .

# Build with all features enabled, using pre-downloaded httplib
RUN cmake -B build_docker -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_BLOCKCHAIN=ON \
    -DENABLE_MQTT=ON \
    -DENABLE_COAP=ON \
    -DENABLE_HTTP=ON \
    -DENABLE_WEBSOCKET=ON \
    -DHTTPLIB_SOURCE_DIR=/tmp/httplib \
    && cmake --build build_docker --parallel $(nproc)

# Run tests
FROM builder AS tester
WORKDIR /app
CMD ["ctest", "--test-dir", "build_docker", "--output-on-failure", "-V"]
