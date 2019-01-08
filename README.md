# ott-streaming-packager
OTT streaming packager supporting ABR streaming for HLS and DASH

This application is intended to serve as a reliable and scalable OTT streaming repackager (with optional transcoding) to deliver content as part of an overall media streaming platform. There are two key variations of OTT streaming technologies that this software accommodates:

    HLS (HTTP Live Streaming) - Transport Stream HLS and Fragmented MP4 HLS (CMAF style)
    DASH (Dynamic Adaptive Streaming over HTTP) - Fragmented MP4 

With this application, you can ingest *live* MPEG2 transport streams carried over UDP (Multicast or Unicast) for transcoding and/or repackaging into HTTP Live Streaming (HLS) (both TS and MP4) and DASH output container formats.  The application serves can optionally trancode or just repackage.  If you are repackaging then the source streams need to be formatted as MPEG2 transport containing H264/HEVC and AAC audio.

---Quickstart---

The software install guide here is for Ubuntu 16.04 server only, however, you can run this on older/newer versions of Ubuntu as well as in Docker containers for AWS/Google cloud based deployments.
<br>
cannonbeach@insanitywave:$ sudo apt install git<br>
cannonbeach@insanitywave:$ sudo apt install build-essential<br>
cannonbeach@insanitywave:$ sudo apt install libz-dev<br>
cannonbeach@insanitywave:$ git clone https://github.com/cannonbeach/ott-packager.git<br>
cannonbeach@insanitywave:$ cd ott-packager<br>
cannonbeach@insanitywave:$ make<br>
<br>
The above steps will compile the application (it is named "fillet"). Please ensure that you already have a basic development environment setup.<br>
<br>
The fillet application must be run as a user with *root* privileges, otherwise it will *not* work.<br>
<br>
usage: fillet [options]<br>
<br>
PACKAGING OPTIONS<br>
       --sources       [NUMBER OF ABR SOURCES - MUST BE >= 1 && <= 10]<br>
       --ip            [IP:PORT,IP:PORT,etc.] (Please make sure this matches the number of sources)<br>
       --interface     [SOURCE INTERFACE - lo,eth0,eth1,eth2,eth3]<br>
                       If multicast, make sure route is in place (see note below)<br>
       --window        [WINDOW IN SEGMENTS FOR MANIFEST]<br>
       --segment       [SEGMENT LENGTH IN SECONDS]<br>
       --manifest      [MANIFEST DIRECTORY "/var/www/html/hls/"]<br>
       --identity      [RUNTIME IDENTITY - any number, but must be unique across multiple instances of fillet]<br>
       --hls           [ENABLE TRADITIONAL HLS TRANSPORT STREAM OUTPUT - NO ARGUMENT REQUIRED]<br>
       --dash          [ENABLE FRAGMENTED MP4 STREAM OUTPUT (INCLUDES DASH+HLS FMP4) - NO ARGUMENT REQUIRED]<br>
       --manifest-dash [NAME OF THE DASH MANIFEST FILE - default: masterdash.mpd]<br>
       --manifest-hls  [NAME OF THE HLS MANIFEST FILE - default: master.m3u8]<br>
       --manifest-fmp4 [NAME OF THE fMP4/CMAF MANIFEST FILE - default: masterfmp4.m3u8]<br>
<br>
TRANSCODE OPTIONS (needs to be compiled with option enabled - see Makefile)<br>
       --transcode     [ENABLE TRANSCODER AND NOT JUST PACKAGING]<br>
       --outputs       [NUMBER OF OUTPUT PROFILES TO BE TRANSCODED]<br>
       --vcodec        [VIDEO CODEC - needs to h264]<br>
       --resolutions   [OUTPUT RESOLUTIONS - formatted as: 320x240,640x360,960x540,1280x720]<br>
       --vrate         [VIDEO BITRATES IN KBPS - formatted as: 800,1250,2500,500]<br>
       --acodec        [AUDIO CODEC - needs to be aac]<br>
       --arate         [AUDIO BITRATES IN KBPS - formatted as: 128,96]<br>
       --aspect        [FORCE THE ASPECT RATIO - needs to be 16:9, 4:3, or other]<br>
<br>                                                                               
Command Line Example Usage:<br>
<br>
cannonbeach@insanitywave:$ sudo ./fillet --sources 2 --ip 127.0.0.1:4000,127.0.0.1:4200 --interface lo --window 5 --segment 5 --manifest /var/www/html/hls --identity 1000<br>
<br>
<br>
This command line tells the application that there are two unicast sources that contain audio and video on the loopback interface. The manifests and output files will be placed into the /var/www/html/hls directory. If you are using multicast, please make sure you have multicast routes in place on the interface you are using, otherwise you will *not* receive the traffic.<br>
<br>
cannonbeach@insanitywave:$ sudo route add -net 224.0.0.0 netmask 240.0.0.0 dev eth0<br>
<br>
You should also be aware that the fillet application creates a runtime cache file in the /var/tmp directory for each instance that is run. The cache file is uniquely identified by the "--identity" flag provided as a parameter. It follows the format of: /var/tmp/hlsmux_state_NNNN where NNNN is the identifier you provided. If you want to start your session from a clean state, then you should remove this state file. All of the sequence numbering will restart and all statistics will be cleared as well. It is also good practice when updating to a new version of the software to remove that file. I do add fields to this file and I have not made it backwards compatible.  I am working on making this more configurable such that it can be migrated across different Docker containers.<br>
<br>
An initial restful API is also now available (still working on adding statistics)<br>
curl http://10.0.0.200:18000/api/v1/status<br>
<br>
