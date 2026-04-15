FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 \
    python3-pip \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Conan
RUN pip3 install conan

# Set working directory
WORKDIR /app

# Copy project files
COPY . .

# Build
RUN conan install . --output-folder=build --build=missing && \
    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/build/generators/conan_toolchain.cmake && \
    cmake --build build --config Release

# Run
CMD ["./build/binance_service"]
