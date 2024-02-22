#!/bin/bash

whichnasm="/usr/bin/nasm"
whichyasm="/usr/bin/yasm"
yasmdownload="http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz"
yasmfile="yasm-1.3.0.tar.gz"
nasmdownload="https://www.nasm.us/pub/nasm/releasebuilds/2.14/nasm-2.14.tar.gz"
nasmfile="nasm-2.14.tar.gz"
x265download="http://ftp.videolan.org/pub/videolan/x265/x265_3.0.tar.gz"
nvidiacheck=`lspci -nn | egrep -i '3d|display|vga' | grep 'NVIDIA'`
yasmcheck=`yasm --version`

# IMPORTANT!!!
# Please make sure your CUDA directories are setup corrrectly
# This is how they are setup on my system.  Yours may be different.
cudainclude="/usr/local/cuda-11.6/include"
cudalib="/usr/local/cuda-11.6/lib64"

sudo apt-get update
sudo apt-get upgrade
sudo apt-get install build-essential -y
sudo apt-get install git -y
sudo apt-get install mercurial -y
sudo apt-get install cmake -y
sudo apt-get install cmake-curses-gui -y
sudo apt-get install gdb -y
sudo apt-get install g++ -y
sudo apt-get install libnuma-dev -y
sudo apt-get install pkg-config -y
sudo apt-get install autoconf -y
sudo apt-get install libtool-bin -y
sudo apt-get install libtool -y
sudo apt-get install zlib1g-dev -y

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing YASM"

if [ -f "$yasmfile" ]; then
    echo "YASM already downloaded"
else
    wget $yasmdownload
    if [ ! -f $yasmfile ]; then
        echo "ERROR: Unable to download $yasmdownload - check network connection!"
        exit
    fi
fi
tar xzf $yasmfile
cd yasm-1.3.0
./configure --prefix=/usr
make -j4
sudo make install
cd ..

if [ -e "$whichyasm" ]; then
    echo "SUCCESS: YASM compiled and installed"
else
    echo "ERROR: YASM did not compile and install - aborting installation!"
    exit
fi

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing NASM"
if [ -f "$nasmfile" ]; then
    echo "NASM already downloaded"
else
    wget $nasmdownload
    if [ ! -f $nasmfile ]; then
        echo "ERROR: Unable to download $nasmdownload - check network connection!"
        exit
    fi
fi
tar xzf $nasmfile
cd nasm-2.14
./configure --prefix=/usr
make -j4
sudo make install
cd ..

if [ -f "$whichnasm" ]; then
    echo "SUCCESS: NASM compiled and installed"
else
    echo "ERROR: NASM did not compile and install - aborting installation!"
    exit
fi

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing FFMPEG from cannonbeach fork"
git clone https://github.com/cannonbeach/FFmpeg.git ./cbffmpeg
cd cbffmpeg
if [ -z "$nvidiacheck" ]; then
    echo "No NVIDIA Hardware Found"
    ./configure --prefix=/usr --disable-encoders --enable-avresample --disable-iconv --disable-v4l2-m2m --disable-muxers --disable-vaapi --disable-vdpau --disable-videotoolbox --disable-muxers --disable-avdevice --enable-encoder=mjpeg
else
    echo "NVIDIA Hardware Found"
    echo $nvidiacheck
    echo "Configuring modules with NVENC support"
    echo "Looking for cuda include: $cudainclude and cuda lib: $cudalib"

    if [ ! -d "$cudainclude" ]; then
        echo "ERROR: Unable to find $cudainclude - aborting installation"
        exit
    fi

    if [ ! -d "$cudalib" ]; then
        echo "ERROR: Unable to find $cudalib - aborting installation"
        exit
    fi

    ./configure --prefix=/usr --enable-cuda --enable-cuvid --enable-nvenc --enable-nonfree --enable-libnpp --enable-avresample --disable-iconv --disable-v4l2-m2m --disable-vaapi --disable-vdpau --disable-videotoolbox --disable-avdevice --enable-encoder=mjpeg --extra-cflags=-I$cudainclude --extra-ldflags=-L$cudalib
fi
make -j8
echo "Installing ffprobe for source scanning"
if [ -f "ffprobe" ]; then
    echo "SUCCESS: ffprobe was compiled correctly"
else
    echo "ERROR: ffprobe was not compiled correctly - aborting installation!"
    exit
fi
sudo make install
cd ..

#----------------------------------------------------------------------------------------------------------------------------------------------

if [ -z "$nvidiacheck" ]; then
    echo "No NVIDIA Hardware Found"
    echo "Installing x264 from cannonbeach fork"
    git clone https://github.com/cannonbeach/x264.git ./cbx264
    cd cbx264
    ./configure --enable-static --disable-shared --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash
    make -j8
    if [ -f "libx264.a" ]; then
        echo "SUCCESS: libx264.a was compiled correctly"
    else
        echo "ERROR: libx264.a was not compiled correctly - aborting installation!"
        exit
    fi
    cd ..
else
    echo "Skipping x264 installation since GPU will be used"
fi

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing fdk-aac from cannonbeach fork"
git clone https://github.com/cannonbeach/fdk-aac.git ./cbfdkaac
cd cbfdkaac
./autogen.sh
if [ ! -f "configure" ]; then
    echo "ERROR: configure script was not generated - aborting installation!"
    exit
fi
./configure --prefix=/usr --enable-static --with-pic
make -j8
if [ -f "./.libs/libfdk-aac.a" ]; then
    echo "SUCCESS: libfdk-aac.a was compiled correctly"
else
    echo "ERROR: libfdk-aac.a was not compiled correctly - aborting installation!"
    exit
fi
cd ..

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing libcurl from cannonbeach fork"
git clone https://github.com/cannonbeach/curl.git ./cblibcurl
cd cblibcurl
./buildconf
if [ -f "configure" ]; then
    echo "SUCCESS: curl configure script generated"
else
    echo "ERROR: curl configure script not generated - aborting installation!"
    exit
fi
echo "Configuring curl for compilation"
./configure --prefix=/usr --enable-static --enable-pthreads --without-ssl --without-librtmp --without-libidn2 --without-nghttp2
make -j8
if [ -f "./lib/.libs/libcurl.a" ]; then
    echo "SUCCESS: libcurl.a was compiled correctly"
else
    echo "ERROR: libcurl.a was not compiled correctly - aborting installation!"
    exit
fi
cd ..

if [ -z "$nvidiacheck" ]; then
    echo "No NVIDIA Hardware Found"
    echo "Installing x265 from packaged source- please select static libs"
    #hg clone https://bitbucket.org/multicoreware/x265 ./headx265
    wget $x265download
    tar xzf x265_3.0.tar.gz
    cd x265_3.0
    cd build
    cd linux
    ./make-Makefiles.bash
    make -j8
else
    echo "Skipping x265 installation since GPU will be used"
fi

echo "Done!"
