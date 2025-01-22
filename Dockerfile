FROM ubuntu:22.04

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
    libpulse0 libasound2 libasound2-plugins alsa-base \
      && \
    # and clean up the apt cache
    rm -rf /var/lib/apt/lists/*


RUN curl -fsSL https://github.com/b1f6c1c4/git-get/releases/latest/download/git-get.tar.xz | /bin/tar -C /usr -xJv


ARG OF_VERSION=0.12.0

RUN wget -O /of.tar.gz https://github.com/openframeworks/openFrameworks/releases/download/${OF_VERSION}/of_v${OF_VERSION}_linux64gcc6_release.tar.gz
RUN cd / && tar -xf of.tar.gz && rm of.tar.gz && mv of_v* of

# install of openframeworks dependencies
RUN of/scripts/linux/ubuntu/install_dependencies.sh
RUN rm -rf /var/lib/apt/lists/*


# compile openframeworks
RUN cd of/scripts/linux && ./compileOF.sh -j3
RUN cd of/scripts/linux && ./compilePG.sh 
RUN of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"./of" examples

ARG OFXLUA_SHA=df42773

RUN git gets -H -v -Y  https://github.com/danomatika/ofxLua/commit/${OFXLUA_SHA} -o /of/addons/ofxLua
RUN cp -r of/addons/ofxLua/luaExample/ of/apps/myApps/luaExample/

RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/luaExample/
RUN cd of/apps/myApps/luaExample/ && make

ARG OFXPIMAPPER_SHA=bb05314

RUN git gets -H -v -Y  https://github.com/kr15h/ofxPiMapper/commit/${OFXPIMAPPER_SHA} -o /of/addons/ofxPiMapper
RUN cp -r of/addons/ofxPiMapper/example_basic/ of/apps/myApps/ofxPiMapper_example_basic/

RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/ofxPiMapper_example_basic/
RUN cd of/apps/myApps/ofxPiMapper_example_basic/ && make



# WORKDIR /of/examples