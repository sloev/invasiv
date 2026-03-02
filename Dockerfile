# Stage 1: Base - System Dependencies and openFrameworks Core
# Pinning to specific digest for absolute reproducibility
FROM ubuntu:24.04@sha256:72297848457d5d37d126263012759e6d39d167305d369798ed3a2b07e14562fa AS of-base

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Berlin

# Install core tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    make git curl wget xz-utils gcc g++ ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Download and Extract openFrameworks 0.12.1
ARG OF_VERSION=0.12.1
RUN wget -qO /of.tar.gz https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/of_v${OF_VERSION}_linux64_gcc6_release.tar.gz \
    && mkdir /of \
    && tar -xf /of.tar.gz -C /of --strip-components=1 \
    && rm /of.tar.gz

# Install oF system dependencies via its own script
# This is a heavy layer, we do it before adding any custom code
RUN /of/scripts/linux/ubuntu/install_dependencies.sh -y \
    && apt-get install -y --no-install-recommends libmpv-dev \
    && rm -rf /var/lib/apt/lists/*

# Pre-compile openFrameworks core (The longest step)
RUN cd /of/scripts/linux && ./compileOF.sh -j$(nproc)

# Stage 2: Addons - Stable external dependencies
FROM of-base AS addons

# Pin Addon SHAs for stability
ARG OFXIMGUI_SHA=49318f0
RUN git clone https://github.com/jvcleave/ofxImGui /of/addons/ofxImGui \
    && cd /of/addons/ofxImGui \
    && git checkout ${OFXIMGUI_SHA}

# Stage 3: App Builder - Application-specific compilation
FROM addons AS builder

ARG VERSION_NAME=dev

# Add only the application source. 
# Changes here will NOT trigger oF re-compilation.
COPY ./invasiv_app /of/apps/myApps/invasiv

# Generate Project and Build Release
RUN /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"/of" /of/apps/myApps/invasiv \
    && cd /of/apps/myApps/invasiv \
    && make Release -j$(nproc) PROJECT_CFLAGS="-DVERSION_NAME='\"${VERSION_NAME}\"'"

# Stage 4: Runtime - Minimal image for deployment/testing
FROM ubuntu:24.04@sha256:72297848457d5d37d126263012759e6d39d167305d369798ed3a2b07e14562fa AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libmpv-dev libgl1-mesa-dri libgl1-mesa-glx libpulse0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /of/apps/myApps/invasiv/bin /app
WORKDIR /app
ENTRYPOINT ["./invasiv"]
