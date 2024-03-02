FROM nvidia/cuda:12.1.1-runtime-ubuntu20.04
LABEL maintainer="cannonbeachgoonie@gmail.com"
RUN apt-get update
RUN apt-get -y upgrade
RUN apt-get install -y apt-utils && apt-get install -y net-tools && apt-get install -y iputils-ping && apt-get install -y libnuma1 && apt-get install -y libssl-dev && apt-get install -y tcpdump
ADD fillet_transcode /usr/bin/fillet_transcode
