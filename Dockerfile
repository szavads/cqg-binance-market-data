FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and runtime TLS certificates
# NOTE: libboost-all-dev intentionally omitted — Boost 1.83 comes from Conan
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    python3 \
    python3-pip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Conan
RUN pip3 install "conan>=2.0"

# Set working directory
WORKDIR /app

# Copy project files
COPY . .

# Create output directory for logs
RUN mkdir -p /app/logs

# Build
RUN conan profile detect --force && \
    conan install . --output-folder=build --build=missing -s build_type=Release && \
    cmake -S . -B build \
        -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF && \
    cmake --build build

# Run (config/config.json resolves relative to WORKDIR /app)
CMD ["./build/binance_service"]

