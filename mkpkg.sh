#!/bin/bash

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

rm -rf build
cp -a distribution build
find build -type f -name ".gitignore" -exec rm -rf {} \;
cp fillet build/usr/bin

cd build
echo "Package: fillet" > DEBIAN/control
echo "Version: 0.1" >> DEBIAN/control
echo "Section: base" >> DEBIAN/control
echo "Priority: optional" >> DEBIAN/control
echo "Architecture: amd64" >> DEBIAN/control
echo "Depends:" >> DEBIAN/control
echo "Maintainer: cannonbeachgoonie@gmail.com" >> DEBIAN/control
echo "Description: OTT-packager for DASH and HLS" >> DEBIAN/control
cd ..

deb_name=fillet-0.1.deb
dpkg-deb --build build $deb_name

make
