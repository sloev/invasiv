FROM ofxdocker_2204_of_0_12_0_invasiv

ADD ./invasiv/ of/apps/myApps/invasiv/

RUN cd of/ && /of/apps/projectGenerator/commandLine/bin/projectGenerator -r -o"." apps/myApps/invasiv/
RUN cd of/apps/myApps/invasiv/ && make