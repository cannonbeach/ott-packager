#!/bin/bash

echo "STATUS- creating /var/tmp/configs directory"
if [ ! -d "/var/www/configs" ]; then
    sudo mkdir /var/tmp/configs
else
    echo "STATUS- /var/tmp/configs already exists"
fi

echo "STATUS- creating /var/tmp/status directory"
if [ ! -d "/var/www/status" ]; then
    sudo mkdir /var/tmp/status
else
    echo "STATUS- /var/tmp/status already exists"
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

echo "STATUS- checking for ./webapp/server.js"
if [ -f "./webapp/server.js" ]; then
    echo "STATUS- found server.js- installing"
    sudo cp ./webapp/server.js /var/app    
else
    echo "STATUS- unable to find server.js - aborting!!!"
    echo "STATUS- please make sure you are running this script from the cloned project directory"
    exit
fi

echo "STATUS-checking for ./webapp/public/client.js"
if [ -f "./webapp/public/client.js" ]; then
    echo "STATUS- found client.js- installing"    
    sudo cp ./webapp/public/client.js /var/app/public
else
    echo "STATUS- unable to find client.js - aborting!!!"
    echo "STATUS- please make sure you are running this script from the cloned project directory"    
    exit
fi

echo "STATUS-checking for ./webapp/public/index.html"
if [ -f "./webapp/public/index.html" ]; then
    echo "STATUS- found index.html- installing"
    sudo cp ./webapp/public/index.html /var/app/public
else
    echo "STATUS- unable to find index.html - aborting!!!"
    echo "STATUS- please make sure you are running this script from the cloned project directory"    
    exit
fi

echo "STATUS-checking for ./webapp/package.json"
if [ -f "./webapp/package.json" ]; then
    echo "STATUS- found package.json- installing"
    sudo cp ./webapp/package.json /var/app
else
    echo "STATUS- unable to find package.json - aborting!!!"
    echo "STATUS- please make sure you are running this script from the cloned project directory"    
    exit
fi

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
echo "STATUS-installing nodejs- grabbing setup script"
curl -sL https://deb.nodesource.com/setup_12.x -o nodesource_setup.sh
if [ -f "nodesource_setup.sh" ]; then
    echo "STATUS- downloading the nodesource_setup.sh script for NodeJS installation"
else
    echo "STATUS- unable to download nodesource_setup.sh script - aborting!!!"
    exit
fi

sudo chmod +x nodesource_setup.sh
echo "STATUS- running nodesource_setup.sh"
sudo ./nodesource_setup.sh
echo "STATUS- installing nodejs- full package from local repository"
sudo apt-get install -y nodejs
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
