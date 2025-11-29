FROM ubuntu:24.04

RUN apt-get update -y && apt-get install -y software-properties-common && add-apt-repository universe
# add missing dependencies needed by openframeworks
RUN apt-get update -y
RUN \
    # tzdata is installed later in install_dependencies script
    # but with interactive choices, so we configure it here correct
    DEBIAN_FRONTEND=noninteractive TZ=Europe/Berlin apt-get -y install \
        tzdata \
    # and also some other depedencies are missing
        lsb-release \
        make \
        git \
        curl \
        wget \
        bash \
        libluajit-5.1-dev \
        liblua5.1-dev \
        xz-utils \
        apt-utils \
    # and to use sound also we need some more
    && \
    # and clean up the apt cache
    rm -rf /var/lib/apt/lists/*


RUN curl -fsSL https://github.com/b1f6c1c4/git-get/releases/latest/download/git-get.tar.xz | /bin/tar -C /usr -xJv


ARG OF_VERSION=0.12.1

RUN wget -O /of.tar.gz https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/of_v${OF_VERSION}_linux64_gcc6_release.tar.gz
RUN cd / && tar -xf of.tar.gz && rm of.tar.gz && mv of_v* of

# install of openframeworks dependencies
# RUN of/scripts/linux/download_libs.sh
RUN of/scripts/linux/ubuntu/install_dependencies.sh
RUN rm -rf /var/lib/apt/lists/*


# compile openframeworks
RUN cd of/scripts/linux && ./compileOF.sh -j3
RUN cd of/scripts/linux && ./compilePG.sh 
RUN of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"./of" examples

ARG OFXLUA_SHA=4a43956

RUN git gets -H -v -Y  https://github.com/danomatika/ofxLua/commit/${OFXLUA_SHA} -o /of/addons/ofxLua
RUN cp -r of/addons/ofxLua/luaExample/ of/apps/myApps/luaExample/

RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/luaExample/
RUN cd of/apps/myApps/luaExample/ && make

ARG OFXZMQ_SHA=b46c8cd

RUN git gets -H -v -Y  https://github.com/funatsufumiya/ofxZmq/commit/${OFXZMQ_SHA} -o /of/addons/ofxZmq
RUN cp -r of/addons/ofxZmq/example-basic/ of/apps/myApps/zmqExample/

RUN apt-get install gcc
RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/zmqExample/
RUN cd of/apps/myApps/zmqExample/ && make

RUN apt-get update -y && apt-get install -y swig

# ADD ./loaf of/apps/myApps/loaf

# # ARG LOAF_SHA=3ffce08

# # RUN git gets -H -v -Y  https://github.com/danomatika/loaf/commit/${LOAF_SHA} -o of/apps/myApps/loaf
# # RUN rm -rf of/apps/myApps/loaf/src/bindings/macos/*



# RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/loaf/
# RUN cd of/apps/myApps/loaf/ && ./scripts/generate_bindings.sh
# RUN cd of/apps/myApps/loaf/ && make



ARG OFXIMGUI_SHA=49318f0

RUN git gets -H -v -Y  https://github.com/jvcleave/ofxImGui/commit/${OFXIMGUI_SHA} -o /of/addons/ofxImGui
RUN cp -r of/addons/ofxImGui/example-simple/ of/apps/myApps/ofxImGui/

RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/ofxImGui/
RUN cd of/apps/myApps/ofxImGui/ && make


ADD ./invasiv of/apps/myApps/test
RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/test/
RUN cd of/apps/myApps/test/ && make Debug


# # WORKDIR /of/examples