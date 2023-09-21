FROM ubuntu:20.04
LABEL maintainer="cannonbeachgoonie@gmail.com"
#RUN apt-get install -y apt-utils
RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get install -y net-tools && apt-get install -y iputils-ping && apt-get install -y libnuma1 && apt-get install -y libssl-dev && apt-get install -y tcpdump
ADD fillet_repackage /usr/bin/fillet_repackage
