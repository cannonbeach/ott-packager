console.log('Client-side code running');
document.getElementById("newsourcepage").style.display = "none";

var transcode_button = document.getElementById('transcode_button');
var submit_button = document.getElementById('submit_button');
var abort_button = document.getElementById('abort_button');
var stopping = 0;

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
    }
});

if (transcode_button) {
    transcode_button.addEventListener('click', function(e) {
	console.log('new button was clicked');
	var button = document.getElementById('transcode_button');
	button.disabled = true;
	
	document.getElementById("controlpage").style.display = "none";
	document.getElementById("statuspage").style.display = "none";
	document.getElementById("newsourcepage").style.display = "block";
	
	button.disabled = false;
    });
}

submit_button.addEventListener('click', function(e) {
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
    
    if (safe == 1) {
	document.getElementById("controlpage").style.display = "block";
	document.getElementById("statuspage").style.display = "block";
	document.getElementById("newsourcepage").style.display = "none";
	
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
	}
    }    
});

abort_button.addEventListener('click', function(e) {
    console.log('abort button was clicked');
    
    document.getElementById("controlpage").style.display = "block";
    document.getElementById("statuspage").style.display = "block";
    document.getElementById("newsourcepage").style.display = "none";    
    alert("Aborted!");    
});

function trigger_image_update()
{
    var images = document.getElementsByTagName('img');

    for (var i = 0; i < images.length; i++) {
	var dt = new Date();
	var img = images[i];

	if (img.src.length >= 0 & img.id != 'idImageNoTimestamp') {
	    img.src = img.src + "?" + dt.getTime();
	}
    }
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
		document.getElementById(elementname_active).innerHTML = '<p style="color:grey">INACTIVE</p>';
		document.getElementById(elementname_uptime).innerHTML = '<p style="color:grey">N/A</p>';
		stopButton.disabled = true;
		resetButton.disabled = true;
		startButton.disabled = false;
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
	    var video_bitrate = service_words.video_bitrate / 1000;
	    
	    if (input_signal == 1) {
		document.getElementById(elementname_active).innerHTML = '<p style="color:green">INGESTING</p>';
		document.getElementById(elementname_uptime).innerHTML = '<p>'+service_words.uptime+'</p>';
	    } else {
		document.getElementById(elementname_active).innerHTML = '<p style="color:red">NO SIGNAL</p>';
		document.getElementById(elementname_uptime).innerHTML = '<p>'+service_words.uptime+'</p>';		
	    }
	    var input_string = '<p>Source IP is '+service_words.source_ip+'<br>Interface is '+service_words.input_interface+'<br>Input bitrate '+video_bitrate+' kbps</p>';
	    var fps = service_words.fpsnum / service_words.fpsden;
	    var fps2 = Math.round(fps*1000)/1000;
	    var mediatype = '';
	    
	    if (service_words.mediatype = 0x10) {
		mediatype = 'H264';		
	    } else if (service_words.mediatype == 0x11) {
		mediatype = 'HEVC';
	    } else if (service_words.mediatype == 0x12) {
		mediatype = 'MPEG2';
	    } else {
		mediatype = 'UNKNOWN';
	    }

	    input_string += '<p>Input is '+mediatype+' - '+service_words.source_width+'x'+service_words.source_height+' @ '+fps2+' fps </p>';
	    document.getElementById(elementname_source).innerHTML = input_string;

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
	    var output_string = '<p>';
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
	    output_string += '<p>window is '+service_words.window_size+' segments <br>segment size '+service_words.segment_length+' seconds</p>';
	    document.getElementById(elementname_output).innerHTML = output_string;    
	    
	    /*
	      from server.js
	    obj.window_size = words.data.system["window-size"];
	    obj.segment_length = words.data.system["segment-length"];
	    obj.hls_active= words.data.system["hls-active"];
	    obj.dash_active= words.data.system["dash-fmp4-active"];
	    obj.source_interruptions = words.data.system["source-interruptions"];
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
}

setInterval(update_service_status, 1000);
    
/*function update_status()
{
    console.log('calling update_status()');
    fetch('/get_signal_status',{method: 'GET'})
	.then(response => {
	    if (response.ok) {
		return response.text();
	    } else {
		return Promise.reject('something went wrong!');
	    }
	})
    
	.then(data => {
	    var words = JSON.parse(data);
            document.getElementById('active1').innerHTML = JSON.parse(words.uptime);
        )}
}

setInterval(update_status, 2000);
*/




