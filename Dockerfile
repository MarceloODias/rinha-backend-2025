FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV DOCKER_ENV=1

RUN rm -rf /var/lib/apt/lists/* && \
    apt-get clean && \
    apt-get update --allow-releaseinfo-change --fix-missing && \
    apt-get install -y \
    pkg-config \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    libssl-dev \
    uuid-dev \
    zlib1g-dev \
    libcurl4-openssl-dev \
    libasio-dev \
    libbz2-dev \
    libsnappy-dev \
    liblz4-dev \
    libzstd-dev \
    libgflags-dev \
    && rm -rf /var/lib/apt/lists/*


# Clone Restbed without dependencies
COPY restbed-docker/ /restbed/

# Build and manually install Restbed
RUN mkdir -p /restbed/build && \
    cd /restbed/build && \
    cmake -DBUILD_TESTS=OFF -DBUILD_IPC=YES -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native -flto -DNDEBUG" .. && \
    make && \
    make install && \
    cp -r ../distribution/include/* /usr/local/include/ && \
    cp -r ../distribution/library/* /usr/local/lib/ && \
    ldconfig

RUN strip /usr/local/lib/librestbed.a

WORKDIR /usr/src/app

COPY CMakeLists.txt ./
COPY src ./src
COPY include ./include

# Build the project
RUN mkdir -p build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native -flto -DNDEBUG" .. && \
    make

RUN strip /usr/src/app/build/payments-service

RUN mkdir /var/tmp/sockets
EXPOSE 8080

CMD ["./build/payments-service"]

