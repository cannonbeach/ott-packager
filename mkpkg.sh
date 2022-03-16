#!/bin/bash

latest=$(git tag -l --merged master --sort='-*authordate' | head -n1)
semver_parts=(${latest//./ })
major=1
minor=${semver_parts[1]}
version=${major}.$((minor+1))

echo "current version ${version}"

echo "Building fillet"
make clean
make -f Makefile
if [ -f fillet ]
then
    echo "fillet build completed successfully!"
else
    echo "fillet did not build! please check!"
    exit 1
fi

cp fillet ./docker
cd ./docker
sudo docker build -t dockerfillet .
sudo docker save -o dockerfillet.tar dockerfillet
cd ..
sudo cp ./docker/dockerfillet.tar .

rm -rf build
cp -a distribution build
cp -a ./webapp ./build/var/app
find build -type f -name ".gitignore" -exec rm -rf {} \;
cp fillet build/usr/bin
sudo cp dockerfillet.tar build/dockerimages
sudo chmod 775 build/dockerimages/dockerfillet.tar

cd build
echo "Package: fillet" > DEBIAN/control
echo "Version: $((minor+1))" >> DEBIAN/control
echo "Section: base" >> DEBIAN/control
echo "Priority: optional" >> DEBIAN/control
echo "Architecture: amd64" >> DEBIAN/control
echo "Depends:" >> DEBIAN/control
echo "Maintainer: cannonbeachgoonie@gmail.com" >> DEBIAN/control
echo "Description: OTT-packager for DASH and HLS" >> DEBIAN/control
cd ..

deb_name=fillet-${version}.deb
dpkg-deb --build build $deb_name
