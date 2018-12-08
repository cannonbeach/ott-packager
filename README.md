# ott-streaming-packager
OTT streaming packager supporting ABR streaming for HLS and DASH

This application is intended to serve as a reliable and scalable OTT streaming repackager to deliver content as part of an overall media streaming platform. There are two key variations of OTT streaming technologies that this software accommodates:

    HLS (HTTP Live Streaming) - most notably developed by Apple and very widely supported
    DASH (Dynamic Adaptive Streaming of HTTP) - developed more traditionally by a consortium


HLS is probably the most widely used variation and is supported by an extremely large ecosystem of devices. It works in browsers, set top boxes, phones/tablets, etc. DASH is also used but has a much more limited deployment footprint. It is also much more feature rich and is difficult to easily get comprehensive player interoperability. DASH is still not at the same level of maturity as HLS, but it is slowly gaining ground.

HLS started out as a transport stream based format that leveraged the traditional MPEG based broadcast standards. This made a lot of sense when Apple first deployed this model since a lot of content was already packaged in transport streams, analysis tools were readily available and it is very well understood by a large audience. It has been in use for a very long time! DASH has taken a slightly different approach by adopting the fragmented MP4 file format that I believe was originally developed by Apple (DASH does specify that you can use transport stream, but I don't think anyone actually does?). Having two different standards can really complicate deployment models, especially when some devices support one type and other devices support the other. It can get even more complicated when DRM and encryption are involved. Over the course of the last year or so, Apple has also announced support for the fragmented MP4 file format as part of their HLS specification. This should hopefully simplify things. There is an industry initiative known as the Common Media Application Format (CMAF) that intends to put some common ground between HLS and DASH, but full scale adoption of it is still not there. At least by standardizing on one common file format that works for both HLS and DASH it helps to minimize the required resources.

The most widely supported combination of protocols and codecs is transport stream based HLS with H.264 video codec and AAC audio codec. There are some devices supporting the newer HEVC video codec (mostly targeting 4K video!) and some devices that support the AC3 audio codec as well. VP9 and AV1 are also excellent alternative video codecs, but support for these in different end user devices is hit/miss. HLS with H.264 video and AAC audio is the best approach to reach the largest audience and should be your primary focus for deploying a service. Additional modes using DASH with other codecs can be included as supplemental media streams to target different devices and/or operational models, but should only be included if needed.

With this application, you can ingest *live* MPEG2 transport streams (containing H264/HEVC video and AAC audio) carried over UDP (Multicast or Unicast) for repackaging into HTTP Live Streaming (HLS) (both TS and MP4) and DASH output container formats. The application serves only as a repackaging solution and not as a full origin server or transcoder (at least for now!).

An OTT streaming platform typically has four key system components:

**1. Encoder/Transcoder (ffmpeg or commercial encoding solution)**
The encoder/transcoder will convert the input stream into a set of output streams compatible with HLS- H.264 video streams and AAC audio streams in transport streams.

**2. Packager (fillet - this application)**
The packager will be responsible for fragmenting/segmenting the source stream into smaller time/frame aligned chunks of content that can easily be served through a HTTP based web server for delivery to end devices.

**3. Origin Server (apache or nginx)**
This server is responsible for caching all of the content that the packager produces and makes it readily available for the edge. Archiving of content can be done here along with advanced PVR capabilities.

**4. Edge Server (apache or nginx)**
This server is responsible for caching and delivering all of the content to the end devices/players. Ad insertion is typically done here since it can be done on a per device/user level if needed. It is also possible to do ad insertion upstream as well, but it'll be less targeted/customized.

If you require an origin server for your deployment, examples of basic integration with the Nginx and Apache based web servers will be provided, but full configuration of those servers are outside the scope of these instructions (at least for now). HLS and DASH are both HTTP based and it is recommended that your service be deployed using HTTPS for best security. ffmpeg is a free and widely used encoder/transcoder that is more than capable of producing high quality streams that can be used with fillet. In addition, ffmpeg does have some packaging capabilities builtin and can be used for HLS based streaming/segmenting independently.
