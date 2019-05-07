console.log('Client-side code running');
document.getElementById("newtranscodesourcepage").style.display = "none";
document.getElementById("newrepackagesourcepage").style.display = "none";

var transcode_button = document.getElementById('transcode_button');
var submit_button_transcode = document.getElementById('submit_button_transcode');
var abort_button_transcode = document.getElementById('abort_button_transcode');
var backup_button = document.getElementById('backup_button');

function strncmp(str1, str2, lgth)
{
    var s1 = (str1+'').substr(0, lgth);
    var s2 = (str2+'').substr(0, lgth);

    return ((s1 == s2) ? 0 : ((s1 > s2) ? 1 : -1));
}

document.addEventListener('click',function(e){
    console.log('button clicked: ', e.target.id);

    var buttonString = e.target.id;
    var button_number = 0;
    
    const digits = buttonString.split('').filter(item => !isNaN(item));
    
    if (digits.length > 0) {
	if (digits.length > 2) {
	    button_number = digits[digits.length-3]+digits[digits.length-2]+digits[digits.length-1];
	} else if (digits.length > 1) {
	    button_number = digits[digits.length-2]+digits[digits.length-1];
	} else {
	    button_number = digits[digits.length-1];
	}
    }
    console.log(button_number);

    if (strncmp(buttonString,'start',5) == 0) {
	console.log('the start button was pressed');
	var currentButton = document.getElementById(buttonString);
	var clickedButton = '/api/v1/start_clicked/'+button_number;
	var resetbuttonString = 'reset_button'+button_number;
	var stopbuttonString = 'stop_button'+button_number;
	var resetButton = document.getElementById(resetbuttonString);
	var stopButton = document.getElementById(stopbuttonString);
	currentButton.disabled = true;
	resetButton.disabled = true;
	stopButton.disabled = true;

	fetch(clickedButton,{method: 'POST'})
	    .then(function(response) {
		if (response.ok) {
		    console.log('start clicked confirmed');
		    return;
		}
		throw new Error('Start request failed.');
	    })
	    .catch(function(error) {
		console.log(error);
	    });		
    } else if (strncmp(buttonString,'stop',4) == 0) {
	console.log('the stop button was pressed');
	var currentButton = document.getElementById(buttonString);
	var clickedButton = '/api/v1/stop_clicked/'+button_number;	
	var resetbuttonString = 'reset_button'+button_number;
	var startbuttonString = 'start_button'+button_number;
	var resetButton = document.getElementById(resetbuttonString);
	var startButton = document.getElementById(startbuttonString);
	currentButton.disabled = true;	
	resetButton.disabled = true;
	startButton.disabled = true;

	fetch(clickedButton,{method: 'POST'})
	    .then(function(response) {
		if (response.ok) {
		    console.log('stop clicked confirmed');
		    return;
		}
		throw new Error('Stop request failed.');
	    })
	    .catch(function(error) {
		console.log(error);
	    });	
    } else if (strncmp(buttonString,'reset',5) == 0) {
	console.log('the reset button was pressed');
	var currentButton = document.getElementById(buttonString);
	currentButton.disabled = true;
	var clickedButton = '/api/v1/reset_clicked/'+button_number;
	fetch(clickedButton,{method: 'POST'})
	    .then(function(response) {
		if (response.ok) {
		    console.log('reset clicked confirmed');
		    return;
		}
		throw new Error('Reset request failed.');
	    })
	    .catch(function(error) {
		console.log(error);
	    });	
    } else if (strncmp(buttonString,"remove",6) == 0) {
	console.log('the remove button was pressed');
	var currentButton = document.getElementById(buttonString);
	currentButton.disabled = true;
	var clickedButton = '/api/v1/removesource/'+button_number;

	var result = confirm("Are you sure you want to remove this service?");
	if (result) {
	    fetch(clickedButton,{method: 'POST'})
		.then(function(response) {
		    if (response.ok) {
			console.log('remove clicked confirmed');
			window.location.reload(true);
			return;
		    }
		    throw new Error('Remove request failed.');
		})
		.catch(function(error) {
		    console.log(error);
		});
	}
    } else if (strncmp(buttonString,"backup",6) == 0) {
	console.log('the backup button was pressed');
	var currentButton = document.getElementById(buttonString);
	var clickedButton = '/api/v1/backup_services';

	fetch(clickedButton,{method: 'GET'})
	    .then(response => {
		if (response.ok) {
		    return response.text();
		} else {
		    return Promise.reject('error: unable to get backup of configurations');
		}
	    })
	
	    .then(data => {
		// data?
		console.log('need to write out zip file here-- saveas');
		saveAs(data, "backups.zip");
            })    
    }
});

if (transcode_button) {
    transcode_button.addEventListener('click', function(e) {
	console.log('new button was clicked');
	var button = document.getElementById('transcode_button');
	button.disabled = true;
	
	document.getElementById("controlpage").style.display = "none";
	document.getElementById("statuspage").style.display = "none";
	document.getElementById("systempage").style.display = "none";
	document.getElementById("newtranscodesourcepage").style.display = "block";
	
	button.disabled = false;
    });
}

submit_button_transcode.addEventListener('click', function(e) {
    console.log('submit button was clicked');

    var sourcename = document.getElementById("sourcename").value;
    var ipaddr_primary = document.getElementById("ipaddr_primary").value;
    var inputinterface1 = document.getElementById("inputinterface1").value;    
    var ipaddr_backup = document.getElementById("ipaddr_backup").value;
    var inputinterface2 = document.getElementById("inputinterface2").value;
    var enablehls = document.getElementById("enablehls").value;
    var enabledash = document.getElementById("enabledash").value;
    var windowsize = document.getElementById("windowsize").value;
    var segmentsize = document.getElementById("segmentsize").value;
    var enablescte35 = document.getElementById("enablescte35").value;
    var videocodec = document.getElementById("videocodec").value;
    var videoquality = document.getElementById("videoquality").value;
    var audiosources = document.getElementById("audiosources").value;
    var enablestereo = document.getElementById("enablestereo").value;
    var audiobitrate = document.getElementById("audiobitrate").value;
    
    var outputenabled1 = document.getElementById("outputenabled1").value;
    var outputresolution1 = document.getElementById("outputresolution1").value;
    var video_bitrate1 = document.getElementById("video_bitrate1").value;
    
    var outputenabled2 = document.getElementById("outputenabled2").value;    
    var outputresolution2 = document.getElementById("outputresolution2").value;
    var video_bitrate2 = document.getElementById("video_bitrate2").value;
    
    var outputenabled3 = document.getElementById("outputenabled3").value;
    var outputresolution3 = document.getElementById("outputresolution3").value;
    var video_bitrate3 = document.getElementById("video_bitrate3").value;
    
    var outputenabled4 = document.getElementById("outputenabled4").value;
    var outputresolution4 = document.getElementById("outputresolution4").value;
    var video_bitrate4 = document.getElementById("video_bitrate4").value;

    var manifestdirectory = document.getElementById("manifestdirectory").value;
    var hlsmanifest = document.getElementById("hlsmanifest").value;
    var fmp4manifest = document.getElementById("fmp4manifest").value;
    var dashmanifest = document.getElementById("dashmanifest").value;

    var managementserverip = document.getElementById("managementserverip").value;
    
    var publishpoint1 = document.getElementById("publishpoint1").value;
    var cdnusername1 = document.getElementById("cdnusername1").value;
    var cdnpassword1 = document.getElementById("cdnpassword1").value;
    
    var safe = 1;
    
    // check that fields are alright before continuing
    var windowsize_int = parseInt(windowsize);   
    if (isNaN(windowsize_int)) {
	safe = 2;
	console.log('windowsize is NaN');
    } else if (windowsize_int <= 0 || windowsize_int >= 30) {
	safe = 2;
	console.log('windowsize is outside of limits');
    }

    if (sourcename == "") {
	safe = 3;
	console.log('invalid service name');
    }   
    
    if (safe == 1) {
	document.getElementById("controlpage").style.display = "block";
	document.getElementById("statuspage").style.display = "block";
	document.getElementById("systempage").style.display = "block";	
	document.getElementById("newtranscodesourcepage").style.display = "none";
	
	var obj = new Object();
	obj.filletmode = "transcode";
	obj.sourcename = sourcename;	
	obj.ipaddr_primary = ipaddr_primary;
	obj.inputinterface1 = inputinterface1;
	obj.ipaddr_backup = ipaddr_backup;
	obj.inputinterface2 = inputinterface2;
	
	obj.enablehls = enablehls;
	obj.enabledash = enabledash;
	obj.windowsize = windowsize;
	obj.segmentsize = segmentsize;

	obj.enablescte35 = enablescte35;
	obj.videocodec = videocodec;
	obj.videoquality = videoquality;
	obj.audiosources = audiosources;
	obj.enablestereo = enablestereo;
	obj.audiobitrate = audiobitrate;

	obj.outputenabled1 = outputenabled1;
	obj.outputresolution1 = outputresolution1;
	obj.video_bitrate1 = video_bitrate1;

	obj.outputenabled2 = outputenabled2;
	obj.outputresolution2 = outputresolution2;
	obj.video_bitrate2 = video_bitrate2;

	obj.outputenabled3 = outputenabled3;
	obj.outputresolution3 = outputresolution3;
	obj.video_bitrate3 = video_bitrate3;

	obj.outputenabled4 = outputenabled4;
	obj.outputresolution4 = outputresolution4;
	obj.video_bitrate4 = video_bitrate4;

	obj.manifestdirectory = manifestdirectory;
	obj.hlsmanifest = hlsmanifest;
	obj.fmp4manifest = fmp4manifest;
	obj.dashmanifest = dashmanifest;

	obj.managementserverip = managementserverip;
	
	obj.publishpoint1 = publishpoint1;
	obj.cdnusername1 = cdnusername1;
	obj.cdnpassword1 = cdnpassword1;
	
	var postdata = JSON.stringify(obj);
	
	console.log(JSON.parse(postdata));

	const url = "/api/v1/newsource";
	const requestinfo = {
	    method: 'POST',	    
	    headers: {
		'content-type':'application/json'
	    },
	    body: postdata
	};

	fetch(url, requestinfo)
	    .then(function(data) {
		console.log('Request success: ', data);
		window.location.reload(true); 		
	    }).then(function(error) {
		console.log('Request failure: ', error);		
	    });
	
    } else {
	if (safe == 2) {
	    alert("Bad Window Size Specified!");
	} else if (safe == 3) {
	    alert("Invalid Service Name!");
	}
    }    
});

abort_button_transcode.addEventListener('click', function(e) {
    console.log('abort button was clicked');
    
    document.getElementById("controlpage").style.display = "block";
    document.getElementById("statuspage").style.display = "block";
    document.getElementById("newtranscodesourcepage").style.display = "none";    
    alert("Aborted!");    
});

function trigger_image_update()
{
    var images = document.getElementsByTagName('img');

    for (var i = 0; i < images.length; i++) {
	var img = images[i];

	if (img.src.length >= 0 & img.id != 'idImageNoTimestamp') {
	    var d = new Date;
	    var http = img.src;	    
	    if (http.indexOf("?=") != -1) {
		http = http.split("?=")[0];
	    }
	    img.src = http + '?=' + d.getTime();
	}
    }
}

var toHHMMSS = (secs) => {
    var sec_num = parseInt(secs, 10)
    var hours   = Math.floor(sec_num / 3600) % 24
    var minutes = Math.floor(sec_num / 60) % 60
    var seconds = sec_num % 60
    return [hours,minutes,seconds]
        .map(v => v < 10 ? "0" + v : v)
        .filter((v,i) => v !== "00" || i > 0)
        .join(":")
}

function request_service_status(service)
{
    var serviceQuery = '/api/v1/get_service_status/'+service;
    console.log('querying: ', serviceQuery);    
    fetch(serviceQuery,{method: 'GET'})
	.then(serviceresponse => {
	    if (serviceresponse.ok) {
		return serviceresponse.text();
	    } else {
		var elementname_active = 'active'+service;
		var elementname_uptime = 'uptime'+service;
		var stopbuttonString = 'stop_button'+service;
		var resetbuttonString = 'reset_button'+service;
		var startbuttonString = 'start_button'+service;		
		var stopButton = document.getElementById(stopbuttonString);
		var resetButton = document.getElementById(resetbuttonString);
		var startButton = document.getElementById(startbuttonString);		
		stopButton.disabled = true;
		resetButton.disabled = true;
		if (serviceresponse.status == 503) {
		    startButton.disabled = true;
		    document.getElementById(elementname_active).innerHTML = '<p style="color:red">WAITING</p>';
		    document.getElementById(elementname_uptime).innerHTML = '<p style="color:grey">N/A</p>';		    
		} else {		 // otherwise 404
		    startButton.disabled = false;
		    document.getElementById(elementname_active).innerHTML = '<p style="color:grey">INACTIVE</p>';
		    document.getElementById(elementname_uptime).innerHTML = '<p style="color:grey">N/A</p>';		    
		}
		return Promise.reject('error: unable to get service update: '+service);
	    }
	})
	.then(servicedata => {
	    var service_words = JSON.parse(servicedata);
	    var input_signal = service_words.input_signal;
	    var startbuttonString = 'start_button'+service;
	    var stopbuttonString = 'stop_button'+service;
	    var resetbuttonString = 'reset_button'+service;	    
	    var startButton = document.getElementById(startbuttonString);
	    var stopButton = document.getElementById(stopbuttonString);
	    var resetButton = document.getElementById(resetbuttonString);	    
	    var elementname_active = 'active'+service;
	    var elementname_uptime = 'uptime'+service;
	    var elementname_image = 'thumbnail'+service;
	    var elementname_source = 'input'+service;
	    var elementname_output = 'output'+service;
	    var elementname_event = 'event'+service;
	    var elementname_status = 'statusinfo'+service;
	    var video_bitrate = service_words.video_bitrate / 1000;
	    var transcoding = service_words.transcoding;
	    var video_codec = service_words.video_codec;
	    var video_profile = service_words.video_profile;
	    var video_quality = service_words.video_quality;
	    var video_frames = service_words.video_frames;
	    
	    if (input_signal == 1) {
		var active_string = '<p style="color:green">INGESTING</p>';
		if (transcoding) {
		    active_string += '<p style="color:blue">TRANSCODE</p>';
		} else {
		    active_string += '<p style="color:blue">REPACKAGE</p>';		    
		}
		document.getElementById(elementname_active).innerHTML = active_string;
		document.getElementById(elementname_uptime).innerHTML = '<p>'+toHHMMSS(service_words.uptime)+'</p>';
	    } else {
		document.getElementById(elementname_active).innerHTML = '<p style="color:red">NO SIGNAL</p>';
		document.getElementById(elementname_uptime).innerHTML = '<p>'+toHHMMSS(service_words.uptime)+'</p>';		
	    }
	    var input_string = '<p>Source IP is '+service_words.source_ip+'<br>Interface is '+service_words.input_interface+'<br><br>Input bitrate '+video_bitrate+' kbps</p>';
	    var fps = service_words.fpsnum / service_words.fpsden;
	    var fps2 = Math.round(fps*1000)/1000;
	    var videomediatype = '';
	    var audiomediatype0 = '';
	    var audiochannelsinput0 = service_words.audiochannelsinput0;
	    var audiochannelsinput1 = service_words.audiochannelsinput1;	    
	    var audiosamplerate0 = service_words.audiosamplerate0;
	    var audiosamplerate1 = service_words.audiosamplerate1;
	    var audiochannelsoutput0 = service_words.audiochannelsoutput0;
	    var audiochannelsoutput1 = service_words.audiochannelsoutput1;
	    
	    if (service_words.videomediatype == 0x10) {
		videomediatype = 'H264';		
	    } else if (service_words.videomediatype == 0x11) {
		videomediatype = 'HEVC';
	    } else if (service_words.videomediatype == 0x12) {
		videomediatype = 'MPEG2';
	    } else {
		videomediatype = 'UNKNOWN';
	    }

	    if (service_words.audiomediatype0 == 0x01) {
		audiomediatype0 = 'AAC';
	    } else if (service_words.audiomediatype0 == 0x02) {
		audiomediatype0 = 'AC3';
	    } else if (service_words.audiomediatype0 == 0x03) {
		audiomediatype0 = 'EAC3';
	    } else if (service_words.audiomediatype0 == 0x04) {
		audiomediatype0 = 'MPEG';
	    } else {
		audiomediatype0 = 'UNKNOWN';
	    }
	    
	    input_string += '<p>Video is '+videomediatype+' - '+service_words.source_width+'x'+service_words.source_height+' @ '+fps2+' fps<br>';
	    input_string += 'Audio is '+audiomediatype0+' @ '+audiochannelsinput0+' channels @ '+audiosamplerate0+' Hz </p>';
	    
	    document.getElementById(elementname_source).innerHTML = input_string;

	    //var event_string = '<p>Current Status</p>';
	    var event_string = '';
	    document.getElementById(elementname_event).innerHTML = event_string;

	    /*
	      from server.js
	    obj.source_width = words.data.source.width;
	    obj.source_height = words.data.source.height;
	    obj.fpsnum = words.data.source.fpsnum;
	    obj.fpsden = words.data.source.fpsden;
	    obj.aspectnum =words.data.source.aspectnum;
	    obj.aspectden =words.data.source.aspectden;
	    */

	    var i;
	    var output_string = '';

	    if (transcoding == 1) {
		var quality_string;
		if (video_quality == 0) {
		    quality_string = 'LOW';
		} else if (video_quality == 1) {
		    quality_string = 'MEDIUM';
		} else if (video_quality == 2) {
		    quality_string = 'HIGH';
		} else if (video_quality == 3) {
		    quality_string = 'MAX';
		} else {
		    quality_string = 'N/A';
		}
		if (video_codec == 0x02) {  // h264
		    output_string += '<p>Video is H264<br>Profile '+video_profile+'<br>Quality '+quality_string+'<br>';
		} else if (video_codec == 0x03) { // hevc
		    output_string += '<p>Video is HEVC<br>Quality '+quality_string+'<br>';
		}
		output_string += 'Audio is AAC @ '+audiochannelsoutput0+' channels </p>';
	    }
   
	    output_string += '<p>';
	    for (i = 0; i < service_words.outputs; i++) {
		var output_streams = service_words.output_streams;		
		var width = output_streams[i].width;
		var height = output_streams[i].height;
		var video_bitrate = output_streams[i].video_bitrate;
		console.log("resolution: ", width, " x ", height, " rate: ", video_bitrate);

		if (i == service_words.outputs - 1) {
		    output_string += ' '+width+'x'+height+' @ '+video_bitrate+'kbps';
		} else {
		    output_string += ' '+width+'x'+height+' @ '+video_bitrate+'kbps<br>';
		}
	    }
	    output_string += '</p>';
	    output_string += '<p>Window is '+service_words.window_size+' segments <br>Segment size '+service_words.segment_length+' seconds</p>';
	    
	    document.getElementById(elementname_output).innerHTML = output_string;

	    var status_string;

	    status_string = '<p>Detected '+service_words.source_interruptions+' source interruptions<br>';
	    status_string += 'Detected '+service_words.source_errors+' source errors<br>';
	    status_string += 'Processed '+service_words.video_frames+' video frames<p>';
	    document.getElementById(elementname_status).innerHTML = status_string;
	    /*
	      from server.js
	    obj.window_size = words.data.system["window-size"];
	    obj.segment_length = words.data.system["segment-length"];
	    obj.hls_active= words.data.system["hls-active"];
	    obj.dash_active= words.data.system["dash-fmp4-active"];
	    obj.source_interruptions = words.data.system["source-interruptions"];
	    obj.source_errors = words.data.system["source-errors"];
	    obj.transcoding = words.data.system.transcoding;
	    obj.scte35 = words.data.system.scte35;
	    */	    
	    
	    trigger_image_update();
	    
	    startButton.disabled = true;
	    stopButton.disabled = false;
	    resetButton.disabled = false;
	})    
}

function get_service_info(services)
{    
    console.log('services waited ', services);

    var i;    
    for (i = 0; i < services; i++) {
	var currentService = i + 1;
	request_service_status(currentService);
    }    
}

function update_service_status()
{
    console.log('calling update_service_status()');

    var services = 0;
    
    fetch('/api/v1/get_service_count',{method: 'GET'})
	.then(response => {
	    if (response.ok) {
		return response.text();
	    } else {
		return Promise.reject('error: unable to get service count');
	    }
	})
    
	.then(data => {
	    var words = JSON.parse(data);	    
	    console.log('services active: ', words.services);
	    services = words.services;
	    
	    get_service_info(services);
        })

    fetch('/api/v1/system_information',{method: 'GET'})
	.then(response => {
	    if (response.ok) {
		return response.text();
	    } else {
		return Promise.reject('error: unable to get system information');
	    }
	})
        .then(data => {
	    var words = JSON.parse(data);
	    var cpus;
	    var cpuload_total = 0;
	    var cpuload_avg;
	    console.log(words.length);
	    for (cpus = 0; cpus < words.length; cpus++) {
		console.log(words[cpus].percent);
		cpuload_total += words[cpus].percent;
	    }
	    if (words.length > 0) {
		cpuload_avg = cpuload_total / words.length;
	    } else {
		cpuload_avg = 0;
	    }
	    cpuload_avg = Math.round(cpuload_avg*100)/100;
	    var cpucount = words.length;
	    var cpustring = '<p>'+cpucount+' cores</p>';
	    var elementname_cpucount = 'cpucount';
	    document.getElementById(elementname_cpucount).innerHTML = cpustring;
	    
	    var loadstring = '<p>Avg '+cpuload_avg+'%</p>';
	    var elementname_cpuload = 'cpuload';
	    document.getElementById(elementname_cpuload).innerHTML = loadstring;	    
	})      
}

setInterval(update_service_status, 1000);




