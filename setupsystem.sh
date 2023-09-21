#!/bin/bash

echo "STATUS- creating /var/tmp/configs directory"
if [ ! -d "/var/tmp/configs" ]; then
    sudo mkdir /var/tmp/configs
else
    echo "STATUS- /var/tmp/configs already exists"
fi

echo "STATUS- creating /var/tmp/status directory"
if [ ! -d "/var/tmp/status" ]; then
    sudo mkdir /var/tmp/status
else
    echo "STATUS- /var/tmp/status already exists"
fi

echo "STATUS- creating /var/tmp/scan directory"
if [ ! -d "/var/tmp/scan" ]; then
    sudo mkdir /var/tmp/scan
else
    echo "STATUS- /var/tmp/scan already exists"
fi

echo "STATUS- creating /var/app directory"
if [ ! -d "/var/app" ]; then
    sudo mkdir /var/app
else
    echo "STATUS- /var/app already exists"
fi

echo "STATUS- creating /var/app/public directory"
if [ ! -d "/var/app/public" ]; then
    sudo mkdir /var/app/public
else
    echo "STATUS- /var/app/public already exists"
fi

#echo "STATUS- checking for ./webapp/server.js"
#if [ -f "./webapp/server.js" ]; then
#    echo "STATUS- found server.js- installing"
#    sudo cp ./webapp/server.js /var/app
#else
#    echo "STATUS- unable to find server.js - aborting!!!"
#    echo "STATUS- please make sure you are running this script from the cloned project directory"
#    exit
#fi
#
#echo "STATUS-checking for ./webapp/public/client.js"
#if [ -f "./webapp/public/client.js" ]; then
#    echo "STATUS- found client.js- installing"
#    sudo cp ./webapp/public/client.js /var/app/public
#else
#    echo "STATUS- unable to find client.js - aborting!!!"
#    echo "STATUS- please make sure you are running this script from the cloned project directory"
#    exit
#fi
#
#echo "STATUS-checking for ./webapp/public/index.html"
#if [ -f "./webapp/public/index.html" ]; then
#    echo "STATUS- found index.html- installing"
#    sudo cp ./webapp/public/index.html /var/app/public
#else
#    echo "STATUS- unable to find index.html - aborting!!!"
#    echo "STATUS- please make sure you are running this script from the cloned project directory"
#    exit
#fi
#
#echo "STATUS-checking for ./webapp/package.json"
#if [ -f "./webapp/package.json" ]; then
#    echo "STATUS- found package.json- installing"
#    sudo cp ./webapp/package.json /var/app
#else
#    echo "STATUS- unable to find package.json - aborting!!!"
#    echo "STATUS- please make sure you are running this script from the cloned project directory"
#    exit
#fi

echo "STATUS-performing global Ubuntu update"
sudo apt-get update -y
echo "STATUS-performing global Ubuntu ugrade of packages"
sudo apt-get upgrade -y
echo "STATUS-installing build-essential packages"
sudo apt-get install build-essential -y
echo "STATUS-intallling libssl-dev packages"
sudo apt-get install libssl-dev -y
echo "STATUS-installing curl packages"
sudo apt-get install curl -y
echo "STATUS-installing ca-certificates"
sudo apt-get install -y ca-certificates
echo "STATUS-installing gnupg"
sudo apt-get install -y gnupg
echo "STATUS-making keyrings directory /etc/apt/keyrings for Nodesource GPG key"
sudo mkdir -p /etc/apt/keyrings
echo "STATUS-importing Nodesource GPG key"
curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg

NODE_MAJOR=18
echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_$NODE_MAJOR.x nodistro main" | sudo tee /etc/apt/sources.list.d/nodesource.list

sudo apt-get update
sudo apt-get install -y nodejs

echo "STATUS- check to see if cleanup.sh installed in crontab"
CRON_TAB_DUMP=`crontab -l > checkcrontab.txt`
CLEANUP_ACTIVE=`grep -r 'cleanup.sh' checkcrontab.txt`
if [ -n "$CLEANUP_ACTIVE" ]; then
    echo "cleanup.sh already in crontab"
else
    echo "including cleanup.sh in crontab"
    ADD_CLEANUP_TO_CRON=`(crontab -l 2>/dev/null; echo "0 * * * * /usr/bin/cleanup.sh") | crontab -`
fi
echo "STATUS- copying cleanup.sh to /usr/bin"
sudo cp cleanup.sh /usr/bin
sudo chmod +x /usr/bin/cleanup.sh

#sudo chmod +x nodesource_setup.sh
#echo "STATUS- running nodesource_setup.sh"
#sudo ./nodesource_setup.sh
#echo "STATUS- installing nodejs- full package from local repository"
#sudo apt-get install -y nodejs
echo "STATUS- installing pm2"
sudo npm install -g pm2
pushd /var/app
echo "STATUS- installing express (global)"
sudo npm install -g express
echo "STATUS- installing body-parser (global)"
sudo npm install -g body-parser
echo "STATUS- installing fs (global)"
sudo npm install -g fs
echo "STATUS- install archiver (global)"
sudo npm install -g archiver
echo "STATUS- install winston logger (global)"
sudo npm install -g winston
echo "STATUS- install read-last-lines file reader (global)"
sudo npm install -g read-last-lines
echo "STATUS- install npm-nvidia-smi (global)"
sudo npm install -g node-nvidia-smi
echo "STATUS- creating node_modules symbolic link to current directory"
sudo ln -s /usr/lib/node_modules ./node_modules
popd

echo "STATUS- installing Docker"
sudo apt-get install docker.io -y
echo "STATUS- installing tcpdump"
sudo apt-get install tcpdump -y
echo "STATUS- installing ifstat"
sudo apt-get install ifstat -y
echo "STATUS- installing zip"
sudo apt-get install zip -y
echo "STATUS- installing unzip"
sudo apt-get install unzip -y
echo "STATUS- installing apache"
sudo apt-get install apache2 -y

#https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html#installing-on-ubuntu-and-debian
echo "STATUS- updating pm2 (if needed)"
sudo npm install pm2@latest -g
sudo pm2 update
echo ""
echo "Please make sure you also run setuptranscode.sh!!"
echo ""
