#!/bin/bash

latest=$(git tag -l --merged master --sort='-*authordate' | head -n1)
semver_parts=(${latest//./ })
major=1
minor=${semver_parts[1]}
version=${major}.$((minor+1))

echo "current version ${version}"

echo "Building packager fillet"
make -f MakefileRepackage clean
make -f MakefileRepackage
if [ -f fillet_repackage ]
then
    echo "fillet_repackage build completed successfully!"
else
    echo "fillet_repackage did not build! please check that the development environment was properly setup!"
    exit 1
fi

cp fillet_repackage ./docker_repackage
pushd docker_repackage
sudo docker build -t dockerfillet_repackage .
sudo docker save -o dockerfillet_repackage.tar dockerfillet_repackage
popd
sudo cp ./docker_repackage/dockerfillet_repackage.tar .

echo "Building transcoder fillet"
make -f MakefileTranscode clean
make -f MakefileTranscode
if [ -f fillet_transcode ]
then
    echo "fillet_transcode build completed successfully!"
else
    echo "fillet_transcode did not build! please check that the development environment was properly setup!"
    exit 1
fi

cp fillet_transcode ./docker_transcode
pushd docker_transcode
sudo docker build -t dockerfillet_transcode .
sudo docker save -o dockerfillet_transcode.tar dockerfillet_transcode
popd
sudo cp ./docker_transcode/dockerfillet_transcode.tar .

rm -rf build
cp -a distribution build
mkdir -p ./build/var/app
mkdir -p ./build/usr/bin
mkdir -p ./build/dockerimages
cp -a ./webapp/* ./build/var/app
find build -type f -name ".gitignore" -exec rm -rf {} \;
cp fillet_repackage build/usr/bin
cp fillet_transcode build/usr/bin
sudo cp dockerfillet_repackage.tar build/dockerimages
sudo cp dockerfillet_transcode.tar build/dockerimages
sudo chmod 775 build/dockerimages/dockerfillet_repackage.tar
sudo chmod 775 build/dockerimages/dockerfillet_transcode.tar

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
