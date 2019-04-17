console.log('Client-side code running');
document.getElementById("newsourcepage").style.display = "none";

var new_button = document.getElementById('new_button');
var start_button = document.getElementById('start_button1');
var reset_button = document.getElementById('reset_button1');
var stop_button1 = document.getElementById('stop_button1');
var submit_button = document.getElementById('submit_button');
var abort_button = document.getElementById('abort_button');

if (new_button) {
    new_button.addEventListener('click', function(e) {
	console.log('new button was clicked');
	var button = document.getElementById('new_button');
	button.disabled = true;
	
	document.getElementById("controlpage").style.display = "none";
	document.getElementById("statuspage").style.display = "none";
	document.getElementById("newsourcepage").style.display = "block";
	
	button.disabled = false;
    });
}

if (reset_button) {
    reset_button.addEventListener('click', function(e) {
	console.log('reset button was clicked');
	var button = document.getElementById('reset_button');
	button.disabled = true;
	
	fetch('/reset_clicked',{method: 'POST'})
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
	
    });
}

if (stop_button1) {
    stop_button1.addEventListener('click', function(e) {
	console.log('stop button was clicked');
	var button = document.getElementById('stop_button1');
	button.disabled = true;
	
	fetch('/stop_clicked',{method: 'POST'})
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
