FROM ubuntu:16.04
LABEL maintainer="cannonbeachgoonie@gmail.com"
RUN apt-get update && apt-get upgrade -y && apt-get install -y apt-utils && apt-get install -y net-tools && apt-get install -y iputils-ping && apt-get install -y libnuma1 && apt-get install -y libssl-dev && apt-get install -y tcpdump
ADD fillet /usr/bin/fillet



