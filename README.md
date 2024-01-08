# ott-streaming-packager

![Optional Text](../master/images/content_publishing_media_gateway_v2.jpg)

OTT live streaming encoder and packager (or independent packaging) supporting ABR streaming for HLS and DASH

This application is intended to serve as a reliable and scalable OTT streaming repackager (with optional transcoding) to deliver content as part of an overall media streaming platform. There are two key variations of OTT streaming technologies that this software accommodates:

    HLS (HTTP Live Streaming) - Transport Stream HLS and Fragmented MP4 HLS (CMAF style)
    DASH (Dynamic Adaptive Streaming over HTTP) - Fragmented MP4

With this application, you can ingest *live* MPEG2 transport streams carried over UDP (Multicast or Unicast) for transcoding and/or repackaging into HTTP Live Streaming (HLS) (both TS and MP4) and DASH output container formats.  The application can optionally transcode or just simply repackage.  If you are repackaging then the source streams need to be formatted as MPEG2 transport containing H264/HEVC and AAC audio, however if you are transcoding then you can ingest a MPEG2 transport stream containing other formats as well.

There are two ways to use this application.  The first and simplest method is use to the command version of the application.  You can quickly clone the repository, compile and easily start streaming.  The Quickstart for the web application is further down in the README and is a bit more involved to get setup and running, but provides a scriptable API as well as a nice clean interface with thumbnails and other status information in the transcoding mode.  The web application is still in the early stages and I will continually be adding features for managing these types of streaming services.

I would also appreciate any funding support, even if it is a one time donation.  I only work on this project in my spare time.  If there are specific features you would like to see, a funding donation goes a long way in making it happen.  I can also offer support services for deployment to address any devops type of issues, troubleshoot hardware (or software issues), or just offer general advice.
<br>

## Quickstart (NodeJS Web Application - Ubuntu 20.04 Server/Desktop)

If something doesn't work here for you, then please post a bug in GitHub.

```
Please follow the directions below *very* closely:

cannonbeach@insanitywave:$ sudo apt install git
cannonbeach@insanitywave:$ sudo apt install build-essential
cannonbeach@insanitywave:$ sudo apt install libz-dev
cannonbeach@insanitywave:$ git clone https://github.com/cannonbeach/ott-packager.git
cannonbeach@insanitywave:$ cd ott-packager

*IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT*
(VERY IMPORTANT: Please advise - if you are planning to run on a NVIDIA GPU system, you need to make sure that prior to running setuptranscode.sh that the cudainclude and cudalib directories are set correctly in the script, otherwise it will fail to setup properly).  Please also make sure that MakefileTranscode also has the correct paths.

You can get updated CUDA deb packages from here: https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/
You can get updated NVIDIA drivers here: https://www.nvidia.com/download/index.aspx
You can get updated NVIDIA patch here: https://github.com/keylase/nvidia-patch
*IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT*

cannonbeach@insanitywave:$ chmod +x setuptranscode.sh
cannonbeach@insanitywave:$ ./setuptranscode.sh

*IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT*
(VERY IMPORTANT: If you are not compiling on an NVIDIA GPU system, when you get to the x265 setup (which is towards the end of the script execution, please set ENABLE_SHARED to OFF and set ENABLE_ASSEMLBY to ON, then hit the letter 'c' for configuration and then hit 'g' for generate and exit)
*IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT* *IMPORTANT*

cannonbeach@insanitywave:$ chmod +x setupsystem.sh
cannonbeach@insanitywave:$ ./setupsystem.sh
cannonbeach@insanitywave:$ ./mkpkg.sh
cannonbeach@insanitywave:$ sudo dpkg -i fillet-1.1.deb

Then point web browser to port 8080, for example: http://10.0.0.200:8080 and the web application will come up.  If for some reason, it does not come up, you need to review the steps above to make sure you followed everything correctly.

You will notice that the Apache web server was also installed.  It allows you to easily serve content directly off the same system.  The content will be available from the directories that you specified in your configurations.
```

![Optional Text](../master/images/mediagateway2.jpg)
![Optional Text](../master/images/mediagateway3.jpg)

<br>

## More advanced quickstart (Command Line Packager or Encoder Mode)

The software install guide here is for Ubuntu 20.04 server only, however, you can run this on older/newer versions of Ubuntu as well as in Docker containers for AWS/Google cloud based deployments.  I do not maintain a CentOS installation guide.

```
```
There are now two versions of the application that get built.  The transcode/package (fillet_transcode) and the independent packager (fillet_repackage).  <br>

```
The fillet application must be run as a user with *root* privileges, otherwise it will *not* work.

usage: fillet_repackage [options]

INPUT PACKAGING OPTIONS (AUDIO AND VIDEO CAN BE SEPARATE STREAMS)
       --vsources      [NUMBER OF VIDEO SOURCES - TO PACKAGE ABR SOURCES: MUST BE >= 1 && <= 10]
       --asources      [NUMBER OF AUDIO SOURCES - TO PACKAGE ABR SOURCES: MUST BE >= 1 && <= 10]

INPUT OPTIONS (when --type stream)
       --vip           [IP:PORT,IP:PORT,etc.] (THIS MUST MATCH NUMBER OF VIDEO SOURCES)
       --aip           [IP:PORT,IP:PORT,etc.] (THIS MUST MATCH NUMBER OF AUDIO SOURCES)
       --interface     [SOURCE INTERFACE - lo,eth0,eth1,eth2,eth3]
                       If multicast, make sure route is in place (route add -net 224.0.0.0 netmask 240.0.0.0 interface)

OUTPUT PACKAGING OPTIONS
       --window        [WINDOW IN SEGMENTS FOR MANIFEST]
       --segment       [SEGMENT LENGTH IN SECONDS]
       --manifest      [MANIFEST DIRECTORY "/var/www/hls/"]
       --identity      [RUNTIME IDENTITY - any number, but must be unique across multiple instances of fillet]
       --hls           [ENABLE TRADITIONAL HLS TRANSPORT STREAM OUTPUT - NO ARGUMENT REQUIRED]
       --dash          [ENABLE FRAGMENTED MP4 STREAM OUTPUT (INCLUDES DASH+HLS FMP4) - NO ARGUMENT REQUIRED]
       --manifest-dash [NAME OF THE DASH MANIFEST FILE - default: masterdash.mpd]
       --manifest-hls  [NAME OF THE HLS MANIFEST FILE - default: master.m3u8]
       --manifest-fmp4 [NAME OF THE fMP4/CMAF MANIFEST FILE - default: masterfmp4.m3u8]
       --webvtt        [ENABLE WEBVTT CAPTION OUTPUT]
       --cdnusername   [USERNAME FOR WEBDAV ACCOUNT]
       --cdnpassword   [PASSWORD FOR WEBDAV ACCOUNT]
       --cdnserver     [HTTP(S) URL FOR WEBDAV SERVER]

PACKAGING AND TRANSCODING OPTIONS CAN BE COMBINED

And for the transcode/package, usage is follows:

usage: fillet_transcode [options]


INPUT TRANSCODE OPTIONS (AUDIO AND VIDEO MUST BE ON SAME TRANSPORT STREAM)
       --sources      [NUMBER OF SOURCES - TO PACKAGE ABR SOURCES: MUST BE >= 1 && <= 10]

INPUT OPTIONS (when --type stream)
       --ip            [IP:PORT,IP:PORT,etc.] (THIS MUST MATCH NUMBER OF SOURCES)
       --interface     [SOURCE INTERFACE - lo,eth0,eth1,eth2,eth3]
                       If multicast, make sure route is in place (route add -net 224.0.0.0 netmask 240.0.0.0 interface)


OUTPUT PACKAGING OPTIONS
       --window        [WINDOW IN SEGMENTS FOR MANIFEST]
       --segment       [SEGMENT LENGTH IN SECONDS]
       --manifest      [MANIFEST DIRECTORY "/var/www/hls/"]
       --identity      [RUNTIME IDENTITY - any number, but must be unique across multiple instances of fillet]
       --hls           [ENABLE TRADITIONAL HLS TRANSPORT STREAM OUTPUT - NO ARGUMENT REQUIRED]
       --dash          [ENABLE FRAGMENTED MP4 STREAM OUTPUT (INCLUDES DASH+HLS FMP4) - NO ARGUMENT REQUIRED]
       --manifest-dash [NAME OF THE DASH MANIFEST FILE - default: masterdash.mpd]
       --manifest-hls  [NAME OF THE HLS MANIFEST FILE - default: master.m3u8]
       --manifest-fmp4 [NAME OF THE fMP4/CMAF MANIFEST FILE - default: masterfmp4.m3u8]
       --webvtt        [ENABLE WEBVTT CAPTION OUTPUT]
       --cdnusername   [USERNAME FOR WEBDAV ACCOUNT]
       --cdnpassword   [PASSWORD FOR WEBDAV ACCOUNT]
       --cdnserver     [HTTP(S) URL FOR WEBDAV SERVER]

OUTPUT TRANSCODE OPTIONS
       --transcode   [ENABLE TRANSCODER AND NOT JUST PACKAGING]
       --gpu         [GPU NUMBER TO USE FOR TRANSCODING - defaults to 0 if GPU encoding is enabled]
       --select      [PICK A STREAM FROM AN MPTS- INDEX IS BASED ON PMT INDEX - defaults to 0]
       --outputs     [NUMBER OF OUTPUT LADDER BITRATE PROFILES TO BE TRANSCODED]
       --vcodec      [VIDEO CODEC - needs to be hevc or h264]
       --resolutions [OUTPUT RESOLUTIONS - formatted as: 320x240,640x360,960x540,1280x720]
       --vrate       [VIDEO BITRATES IN KBPS - formatted as: 800,1250,2500,500]
       --acodec      [AUDIO CODEC - needs to be aac, ac3 or pass]
       --arate       [AUDIO BITRATES IN KBPS - formatted as: 128,96]
       --aspect      [FORCE THE ASPECT RATIO - needs to be 16:9, 4:3, or other]
       --scte35      [PASSTHROUGH SCTE35 TO MANIFEST (for HLS packaging)]
       --stereo      [FORCE ALL AUDIO OUTPUTS TO STEREO- will downmix if source is 5.1 or upmix if source is 1.0]
       --quality     [VIDEO ENCODING QUALITY LEVEL 0-3 (0-BASIC,1-STREAMING,2-BROADCAST,3-PROFESSIONAL)
                      LOADING WILL AFFECT CHANNEL DENSITY-SOME PLATFORMS MAY NOT RUN HIGHER QUALITY REAL-TIME

H.264 SPECIFIC OPTIONS (valid when --vcodec is h264)
       --profile     [H264 ENCODING PROFILE - needs to be base,main or high]


```
Simple Repackaging Command Line Example Usage:<br>
```
cannonbeach@insanitywave:$ sudo ./fillet_repackage --vsources 2 --vip 0.0.0.0:20000,0.0.0.0:20001 --asources 2 --aip 0.0.0.0:20002,0.0.0.0:20003 --interface eno1 --window 10 --segment 2 --hls --manifest /var/www/html/hls
```
<br>
This command line tells the application that there are two unicast sources that contain audio and video on the loopback interface. The manifests and output files will be placed into the /var/www/html/hls directory. If you are using multicast, please make sure you have multicast routes in place on the interface you are using, otherwise you will *not* receive the traffic.

This will write the manifests into the /var/www/html/hls directory (this is a common Apache directory).

<br>
<br>

```
cannonbeach@insanitywave:$ sudo route add -net 224.0.0.0 netmask 240.0.0.0 dev eth0
```
<br>
In addition to having the multicast route in place, you may also need to turn off the reverse-path filter for multicast traffic:
In /etc/sysctl.conf, there are multiple entries that control reverse-path filtering. In some instances depending on how your network is setup and where the source is coming from, you may have to disable reverse-path filtering.  Older variations of Linux had this enabled by default, but it can cause issues with multicast coming from a different subnet.

<br>

```
net.ipv4.conf.default.rp_filter = 0
net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.eth0.rp_filter = 0
and so on...

```

After you've made those changes, please run the following for the changes to take effect

```
sudo sysctl -p
````

<br>
You should also be aware that the fillet application creates a runtime cache file in the /var/tmp directory for each instance that is run. The cache file is uniquely identified by the "--identity" flag provided as a parameter. It follows the format of: /var/tmp/hlsmux_state_NNNN where NNNN is the identifier you provided. If you want to start your session from a clean state, then you should remove this state file. All of the sequence numbering will restart and all statistics will be cleared as well. It is also good practice when updating to a new version of the software to remove that file. I do add fields to this file and I have not made it backwards compatible.  I am working on making this more configurable such that it can be migrated across different Docker containers.<br>
<br>

## Packager Operation

The packager has several different optional modes:
- Standard Packaging (H.264/AAC and/or HEVC/AAC)
- Transcoding+Packaging Mode 1 (H.264/AAC) - HLS(TS,fMP4) and DASH(fMP4)
- Transcoding+Packaging Mode 2 (HEVC/AAC) - HLS(fMP4) and DASH(fMP4) - NO TS MODE FOR HEVC
In the transcoding modes, the packaging is bundled together with the transcoding, but I am exploring options to separate the two for a post v1.0 release.

This is a budget friendly packaging/transcoding solution with the expectation of it being simple to setup, use and deploy.  The solution is very flexible and even allows you to run several instances of the application with different parameters and output stream combinations (i.e., have a mobile stream set and a set top box stream set).  If you do run multiple instances using the same source content, you will want to receive the streams from a multicast source instead of unicast.  The simplicity of the deployment model also provides a means for fault tolerant setups.

A key value add to this packager is that source discontinuities are handled quite well (in standard packaging mode as well as the transcoding modes).  The manifests are setup to be continuous even in periods of discontinuity such that the player experiences as minimal of an interruption as possible.  The manifest does not start out in a clean state unless you remove the local cache files during the fast restart (located in /var/tmp/hlsmux...).  This applies to both HLS (handled by discontinuity tags) and DASH outputs (handled by clever timeline stitching the manifest).  Many of the other packagers available on the market did not handle discontinuties well and so I wanted to raise the bar with regards to handling signal interruptions (we don't like them, but yes they happen and the better you handle them the happier your customers will be).

Another differentiator (which is a bit more common practice now) is that the segments are written out to separate audio and video files instead of a single multiplexed output file containing both audio and video.  This provides additional degrees of freedom when selecting different audio and video streams for playback (it does make testing a bit more difficult though).

<br>

## H.264 Transcoding Example
```
cannonbeach@insanitywave:$ ./fillet_transcode --sources 1 --ip 0.0.0.0:5000 --interface eth0 --window 20 --segment 2 --identity 1000 --hls --dash --transcode --outputs 2 --vcodec h264 --resolutions 320x240,960x540 --manifest /var/www/html/hls --vrate 500,2500 --acodec aac --arate 128 --aspect 16:9 --scte35 --quality 0 --profile base --stereo

```

<br>

## HEVC Transcoding Example

```
cannonbeach@insanitywave:$ ./fillet_transcode --sources 1 --ip 0.0.0.0:5000 --interface eth0 --window 20 --segment 2 --identity 1000 --hls --dash --transcode --outputs 2 --vcodec hevc --resolutions 320x240,960x540 --manifest /var/www/html/hls --vrate 500,1250 --acodec aac --arate 128 --aspect 16:9 --quality 0 --stereo

````

<br>

### Programmable/Scriptable API (Requires the NodeJS Web Application)

```
Get Detailed Service Status:
http://127.0.0.1:8080/api/v1/get_service_status/##

Get Service Count:
http://127.0.0.1:8080/api/v1/get_service_count

Get Service List (A list of the current services and high level status but not a lot of details):
http://127.0.0.1:8080/api/v1/list_services

Get System Information (CPU Load, Memory, Temperature, etc.):
http://127.0.0.1:8080/api/v1/system_information

(see Wiki for a use case for the transcoding API)

```

The application will also POST event messages to a third party client (or log) for the following events.  The Winston logging system is being used now within the NodeJS framework, so it is quite easy to extend this to meet your own needs.  The default log will be /var/log/eventlog.log.
It is recommended that you add it to the system logrotate.

```
- Start Service (Container Start)
- Stop Service (Container Stop)
- No Source Signal
- Docker Container Restart
- SCTE35 begin/end
- Segment Published Upload
- Segment Published Failed Upload
- High CPU Usage
- Low Drive Space
- Service Added
- Service Removed
- Silence Inserted
- Frame Dropped
- Frame Repeated
- High Source Errors Over Period of Time (threshold TBD/ms)
```

And instead of building a full dashboard monitoring system, I've been looking at other open source services to have a nice interface for tracking the health of the systems and generated streams.

### Troubleshooting

There is nothing more frustrating when you clone an open source project off of GitHub and can't get it to compile or work!  I do my best to make sure everything works within the context of the resources I have available to me.  I am not doing nightly builds and do not have a complicated autotest framework.  I work on this in my spare time so it's possible something may slip by.   *If something doesn't work, then please reach out to me or post a bug in the "Issues" section.*  I know the instructions and setup scripts are a bit extensive and detailed, but if you follow them line by line they *do* work.

I am currently using Winston to log messages back through the NodeJS interace.  Here is a sample of the logging information provided which is currently logged to /var/log/eventlog.log on the "Host" system.  I have not finalized this format because I'd like to add the docker container identifier to this along with some additional supporting data (like time of day, IP address, etc).

Some troubleshooting tips:
1. You can run the webapp through the command line and see messages on the console:
```

cd /var/app
sudo node server.js

```
2. You can run the fillet app *outside* of the Docker container
3. You can use tools like *tcpdump* and *ffprobe* to check for incoming signals on different interfaces

```

sudo tcpdump -n udp -i eth0
this will quickly tell if you are receiving content

or

ffprobe udp://@:5000
that'll quickly identify if something is on that port, etc.

```
4. If the webapp is not responding....

```
Check inside /var/tmp/status for .lock files.  If the Docker container got out of sync from the webapp, then you may need to manaully delete the .lock file for the specific configuration you are having problems with.
The configuration files are also stored in /var/tmp/configs.
```

5. If you need to change a config setting....
```
You can change config settings manually by editing the .json files in /var/tmp/configs
```

I suggest you be resourceful and try to debug things.  These types of systems are not always easy to setup.

While running the webapp, you can do a "tail -f /var/log/eventlog.log".  You should also add the eventlog.log to the logrotate.conf on your Ubuntu system to prevent your drive from filling up.  I'll include instructions on this after I finalize the logging format.

```
{"time":"2019-10-25T21:34:37Z","id":1571765102,"status":"success","message":"manifest written","filename":"/var/www/html/nbc/video3fmp4.m3u8","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"warning","message":"silence insert (Inserting Silence To Maintain A/V Sync)","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"warning","message":"silence insert (Inserting Silence To Maintain A/V Sync)","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"warning","message":"silence insert (Inserting Silence To Maintain A/V Sync)","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"warning","message":"silence insert (Inserting Silence To Maintain A/V Sync)","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"success","message":"segment written","filename":"/var/www/html/nbc/video_stream0_96.ts","level":"info"}
{"time":"2019-10-25T21:34:38Z","id":1571765102,"status":"success","message":"segment written","filename":"/var/www/html/nbc/video0/segment8378368.mp4","level":"info"}
....
{"time":"2019-10-25T21:35:05Z","id":1571765102,"status":"success","message":"input signal locked","source":"0.0.0.0:9500:eth0","level":"info"}
....
{"time":"2019-10-25T21:40:41Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:41:14Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:41:29Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:41:29Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:41:47Z","id":1571765102,"status":"error","message":"decode error (Audio Decode Error)","level":"info"}
{"time":"2019-10-25T21:41:58Z","status":"fatal error","message":"(Unrecoverable Discontinuity Detected - Restarting Service)","level":"info"}
```

<br>

### Current Status

(01/02/24) Happy New Year!

If anyone needs SRT support on the input side of the ott-packager, please use my other project opensrthub.  https://github.com/cannonbeach/opensrthub.git
And as usual, if anyone needs something, send me an email.

(09/26/23) Ok, ok, ok....the weather is getting colder and I am not ready for winter

I figured it was time to come back to this project and do some things.  I added webapp support for packaging, so you can now add a packaging service or a transcoding service using the webapp.  I also updated NodeJS from 12 to 18, and made the transcode a separate compile from the repackager.  You must follow the new set of instructions to get everything up and running and you no longer have a choice to build one or the other (at least with the scripts I am providing).  Reach out if there is an issue or a question.  I'd love to hear from you.

(03/15/22) It's been awhile....

I've been off doing other projects and have been meaning to come back to this project and give it some much needed attention!  I pushed up some small timestamp fixes along with initial support for nvidia based gpu encoding.  I did not fully update the quickstart instructions above but will do that in the coming days.  I have lots of build combinations to test and need to setup a clean system to make sure things are working as intended.  Send me an email if you get stuck in the meantime.

(11/10/20) Small update

- Small update to Ubuntu 20.04

(07/31/20) It's been awhile....

- I finally pushed up the changes for MPTS support along with some additional webapp changes.  I still need to update the README.md to reflect the new command line changes as well as some of the webapp features (I've attempted to add a "scan" button where you enter your source information and it lets you pick a source).
- I have also started a .deb packager script to make things easier to build and deploy.  It is not entirely done yet, but feel free to try it out if you must.  I can't guarantee that it'll work since I am *slowly* working through the details.  I have to still go back and verify that everything works from a clean Ubuntu install and that I didn't forget to include a package (or some other random detail that I missed).

(10/25/19) It's almost Halloween!  Trick r' Treat!

- I pushed some changes up today that fix a performance issue that some of you have come across when running on lower clock speed CPU platforms (higher core and lower clock speed) along with having a high number of multi-resolution output profiles.  I had originally put the deinterlacer into the same thread as the scalers and it was just too much work for one thread.  I went ahead and separated the work across multiple threads now.  This also sets things up for me to implement frame rate conversion across output streams.
- I am also planning on sharing some benchmarks based on some older Intel platforms so that'll give you guys an idea on how to provision things accordingly.  I've also done a lot of testing on Google Cloud and AWS but the tests are tricky there because it's difficult for me to stream higher bitrate content to the cloud with my current internet connection.
- I do have some additional changes on the webapp, but I've been slow to get things implemented.  I know a lot of you are not using the webapp (or may have added your own customizations).  I've seen a couple of variations that have been customized and would love to get some of those changes pushed upstream.

(07/25/19) Short update

- I've been away for awhile since my last update but I have been actively developing some new features (webdav publishing, experimenting with SRT and some cloud applications (hybrid transcoding and some on the fly experiments - more on this later) along with trying to finish the webapp for pure packaging mode).  I really haven't had a chance to get these features fully tested yet!  I have been working on some other things that "keep the lights on", so hopefully once I wrap up those other projects, I can spend more time focusing on this again.  If you have support questions, or post something in the bug tracker, it might be a slower than usual response.  If anyone ever does want to fund some of this development then please send me a message.

(04/29/19) Web application development

- I've started pushing up the initial implementation for the web application based on NodeJS.  It is not entirely usable yet, but some of the basic functionality is starting to come together.  Please see the comments on the push for more details.  I will provide setup scripts and a configuration along with a detailed tutorial on how to use the NodeJS web application as soon as a first version is ready.

(04/15/19) Another short update

- Started to build a web application based on NodeJS for status, control and provisioning.  I pushed up the start of it but the web application is not useful yet.  I would like to include this as part of the v1.0 release if possible since I think it'll be a very useful feature.  I'll post some screen shots and how to run it once I get the basic functionality in place.

(03/14/19) Short update on things

- Added initial framework for webvtt caption decoding (creating the small .vtt files now) - (--webvtt)
- Improved frame a/v sync code and made it more robust against some rare situations
- Added a way to select the number of input audio streams (--astreams #)

(03/04/19) Project is still in active development.  I am still pushing for a v1.0 in the next couple of months.  I pushed up a small update today to clean up a few minor issues:

- MPEG audio decoding
- Fix for audio switching from 5.1 to 2.0 back to 5.1 during commercials.  This is working now, but the audio levels are quieter on these commercials so I am looking at the levels to see if the main downmix can be adjusted to match.
- Fixed a few small minor code issues (you could call this cleanup)

(02/20/19) As I mentioned in earlier posts, the application is still in active development, but I am getting closer to a v1.0 release.  This most recent update has included some significant transcoding feature improvements.

- 5.1 to stereo downconversion and mono to stereo upconversion added
- SCTE35 passthrough for HLS H.264 output mode with trancoding (added CUE-IN/CUE-OUT to manifest).  Scheduled splices with duration are supported (immediate splices are not implemented yet and will do this with API support)
- Fixed bug where DASH manifest would not output if HLS output mode was not selected
- Finished most of the HEVC encoding path (some minor fixes still needed in the output manifest files for profile/level reporting)
- Fixed DASH a/v sync issue
- Added profile selector for H264 encoding (base,main,high)
- Added quality modes for H264 encoding (low,med,high,crazy)
- Added notification when encoder is unable to make realtime encoding due to limited CPU resources
- Increased internal buffer limits to support a larger number of streams being repackaged

And finally, I am also thinking of putting together a "Pro" version to help me fund the development of this project.  It'll be based on a reasonable yearly fee and provide access to an additional repository that contains a full NodeJS web interface, a more complete Docker integration, benchmarks, cloud deployment examples, deployment/installation scripts, priority support, fully documented API (along with scripts), SNMP traps, and active/passive failover support.

But for those of you that don't wish to take advantage of things like support, the source code for the core application will remain available in the existing repository.

I also plan to start adapting this current solution over to file version after the v1.0 has been finished and released.

(01/12/19) This application is still in active development and I am hoping to have an official v1.0 release in the next couple of months.  I still need to tie up some loose ends on the packaging as well as complete the basic H.264 and HEVC transcoding modes.  The remaining items will be tagged in the "Issues" section.

I do offer fee based consulting so please send me an email if you are interested in retaining me for any support issues or feature development.  I have several support models available and can provide more details upon request.  You can reach me at: cannonbeachgoonie@gmail.com

See the WIKI page for more information:
https://github.com/cannonbeach/ott-packager/wiki
<br>
