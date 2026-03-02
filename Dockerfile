# Stage 1: Base - System Dependencies and openFrameworks Core
FROM ubuntu:24.04 AS of-base

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Berlin

# Install core tools and all openFrameworks dependencies manually
RUN apt-get update && apt-get install -y --no-install-recommends \
    make git curl wget xz-utils gcc g++ ca-certificates pkg-config \
    freeglut3-dev libasound2-dev libxmu-dev libxxf86vm-dev \
    libgl1-mesa-dev libglu1-mesa-dev libraw1394-dev libudev-dev \
    libdrm-dev libgbm-dev libxrender-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libxml2-dev libssl-dev libpulse-dev \
    libx11-dev libxext-dev \
    libfontconfig1-dev libxkbcommon-dev \
    libusb-1.0-0-dev libopenal-dev libsndfile1-dev libmpg123-dev \
    librtaudio-dev libjack-jackd2-dev \
    libflac-dev libvorbis-dev libmpv-dev libgtk-3-dev \
    libglfw3-dev libgles2-mesa-dev libglew-dev \
    libfreeimage-dev libfreetype-dev libcurl4-openssl-dev \
    liburiparser-dev libpugixml-dev libassimp-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Download and Extract openFrameworks 0.12.1
ARG OF_VERSION=0.12.1
RUN wget -qO /of.tar.gz https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/of_v${OF_VERSION}_linux64_gcc6_release.tar.gz \
    && mkdir /of \
    && tar -xf /of.tar.gz -C /of --strip-components=1 \
    && rm /of.tar.gz

# Pre-compile openFrameworks core
RUN cd /of/scripts/linux && ./compileOF.sh -j$(nproc)

# Stage 2: Addons - Stable external dependencies
FROM of-base AS addons
ARG OFXIMGUI_SHA=49318f0
RUN git clone https://github.com/jvcleave/ofxImGui /of/addons/ofxImGui \
    && cd /of/addons/ofxImGui \
    && git checkout ${OFXIMGUI_SHA}

# Stage 3: App Builder - Application-specific compilation
# This stage can be built either FROM the 'addons' stage locally
# or FROM a pre-built image in GHCR
FROM addons AS builder
ARG VERSION_NAME=dev
COPY ./invasiv_app /of/apps/myApps/invasiv
RUN /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"/of" /of/apps/myApps/invasiv \
    && cd /of/apps/myApps/invasiv \
    && make Release -j$(nproc) PROJECT_CFLAGS="-DVERSION_NAME='\"${VERSION_NAME}\"'"

# Stage 4: Runtime
FROM ubuntu:24.04 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmpv2 libgl1-mesa-dri libgl1-mesa-glx libpulse0 \
    libglew2.2 libfreeimage3 libfreetype6 libasound2t64 libglfw3 \
    librtaudio6 libjack-jackd2-0 \
    libx11-6 libxext6 libxinerama1 libxcursor1 libxi6 \
    libxrandr2 libxrender1 libxxf86vm1 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=builder /of/apps/myApps/invasiv/bin /app
WORKDIR /app
ENTRYPOINT ["./invasiv"]
