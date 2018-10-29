FROM ubuntu:16.04
LABEL maintainer="cannonbeachgoonie@gmail.com"
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y net-tools
RUN apt-get install -y iputils-ping
RUN apt-get install -y tcpdump
OPEN 4000
ADD fillet /usr/bin/fillet
#CMD ["fillet","--sources","1","--segment","5","--window","8","--ip","0.0.0.0:4000","--interface","eth0","--manifest","/var/www/html/hls","--identity","2000"]

