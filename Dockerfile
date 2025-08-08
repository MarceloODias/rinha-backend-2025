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
COPY restbed/ /restbed/

# Build and manually install Restbed
RUN mkdir -p /restbed/build && \
    cd /restbed/build && \
    cmake -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -march=skylake -mtune=skylake -pipe -fno-plt -ffast-math -flto" .. && \
    make && \
    make install && \
    cp -r ../distribution/include/* /usr/local/include/ && \
    cp -r ../distribution/library/* /usr/local/lib/ && \
    ldconfig

RUN strip /usr/local/lib/librestbed.a

# Clone & build RocksDB
RUN git clone --branch v10.4.2 --depth 1 https://github.com/facebook/rocksdb.git /opt/rocksdb && \
    cd /opt/rocksdb && \
    mkdir -p build && cd build && \
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -march=skylake -mtune=skylake -pipe -fno-plt -ffast-math" \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DWITH_GFLAGS=1 \
      -DWITH_SNAPPY=1 \
      -DWITH_ZLIB=1 \
      -DWITH_BZ2=1 \
      -DWITH_LZ4=1 \
      -DWITH_ZSTD=1 \
      -DROCKSDB_BUILD_TESTS=OFF \
      -DWITH_GFLAGS=OFF \
      -DBUILD_SHARED_LIBS=ON \
      -DWITH_MEMENV=ON \
      -DPORTABLE=1 \
      -DUSE_RTTI=1 && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /opt/rocksdb

WORKDIR /usr/src/app

COPY CMakeLists.txt ./
COPY src ./src
COPY include ./include

# Build the project
RUN mkdir -p build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -march=skylake -mtune=skylake -pipe -fno-plt -ffast-math -flto" .. && \
    make

RUN strip /usr/src/app/build/payments-service

EXPOSE 8080

CMD ["./build/payments-service"]

