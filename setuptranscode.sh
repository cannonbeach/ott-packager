whichnasm="/usr/bin/nasm"
whichyasm="/usr/bin/yasm"
yasmdownload="http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz"
nasmdownload="https://www.nasm.us/pub/nasm/releasebuilds/2.14/nasm-2.14.tar.gz"
x265download="http://ftp.videolan.org/pub/videolan/x265/x265_2.9.tar.gz"

sudo apt-get update
sudo apt-get upgrade
sudo apt-get install build-essential
sudo apt-get install git
sudo apt-get install mercurial
sudo apt-get install cmake
sudo apt-get install cmake-curses-gui
sudo apt-get install gdb
sudo apt-get install g++

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing YASM"
wget $yasmdownload
tar xzf yasm-1.3.0.tar.gz
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
wget $nasmdownload
tar xzf nasm-2.14.tar.gz
cd nasm-2.14
./configure --prefix=/usr
make -j4
sudo make install
cd ..

if [ -e "$whichnasm" ]; then
    echo "SUCCESS: NASM compiled and installed"
else
    echo "ERROR: NASM did not compile and install - aborting installation!"
    exit
fi

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing FFMPEG from cannonbeach fork"
git clone https://github.com/cannonbeach/FFmpeg.git ./cbffmpeg
cd cbffmpeg
./configure --prefix=/usr --disable-encoders --enable-avresample --disable-iconv --disable-v4l2-m2m --disable-muxers --disable-vaapi --disable-vdpau --disable-videotoolbox --disable-muxers --disable-avdevice
make -j8
cd ..

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing x264 from cannonbeach fork"
git clone https://github.com/cannonbeach/x264.git ./cbx264
cd cbx264
./configure --enable-static --disable-shared --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash
make -j8
cd ..

#----------------------------------------------------------------------------------------------------------------------------------------------

echo "Installing fdk-aac from cannonbeach fork"
git clone https://github.com/cannonbeach/fdk-aac.git ./cbfdkaac
cd cbfdkaac
./autogen.sh
# todo-check if configure script was generated
./configure --prefix=/usr --enable-static --with-pic
# todo-check to see if makefile generated
make -j8
cd ..

echo "Installing x265 from packaged source"
#hg clone https://bitbucket.org/multicoreware/x265 ./headx265
wget $x265download
tar xzf x265_2.9.tar.gz
pushd
cd x265_2.9
cd build
cd linux
./make-Makefiles.bash
make -j8
popd

echo "Done!"


