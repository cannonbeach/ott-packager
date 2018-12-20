FROM ubuntu:16.04
LABEL maintainer="cannonbeachgoonie@gmail.com"
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y net-tools
RUN apt-get install -y iputils-ping
RUN apt-get install -y tcpdump
ADD fillet /usr/bin/fillet

