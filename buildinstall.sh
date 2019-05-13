#!/bin/bash

sudo apt-get update -y
sudo apt-get upgrade -y
sudo apt-get install g++ -y
sudo apt-get install libz-dev -y

make clean
if [ -f "Makefile" ]; then
    echo "STATUS- Found Makefile- building application"
    make -j8
else
    echo "STATUS- Unable to find Makefile- aborting!!!"
    exit
fi

if [ -f "fillet" ]; then
    echo "STATUS- fillet application was successfully built!"
else
    echo "STATUS- fillet application was not successfully build!  aborting!!!"
    exit
fi

cp fillet ./docker
pushd ./docker
echo "STATUS- building Docker container image"
sudo docker build -t dockerfillet .
popd

pushd /var/app
sudo pm2 start server.js
popd

sudo pm2 status server
# check if server.js service started or not?

echo "STATUS- testing NodeJS API"
curl http://127.0.0.1:8080/api/v1/system_information
curl http://127.0.0.1:8080/api/v1/get_service_count
curl http://127.0.0.1:8080/api/v1/get_interfaces
curl http://127.0.0.1:8080/api/v1/get_service_status/0

sudo "STATUS- adding pm2 to systemd"
sudo pm2 startup systemd
sudo "STATUS- saving the active services"
sudo pm2 save









