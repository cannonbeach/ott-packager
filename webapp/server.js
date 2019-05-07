console.log('Server-side code running');

var exec = require('child_process').exec;
var os = require('os');
var networkInterfaces = os.networkInterfaces( );
const express = require('express');
var bodyParser = require('body-parser');
const fs = require('fs');
const app = express();
var path = require('path');

app.use(bodyParser.json());

// serve files from the public directory
app.use(express.static('public'));

const configFolder = '/var/tmp/configs';

function getExtension(filename) {
    var i = filename.lastIndexOf('.');
    return (i < 0) ? '' : filename.substr(i);
}

function seconds_since_epoch(){ return Math.floor( Date.now() / 1000 ) }

// start the express web server listening on 8080
app.listen(8080, () => {
    console.log('listening on 8080');
});

var activeconfigurations = 0;

fs.readdir(configFolder, (err, files) => {
    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    activeconfigurations++;
	}
    });    
});

cpuIAverage = function(i) {
    var cpu, cpus, idle, len, total, totalIdle, totalTick, type;
    totalIdle = 0;
    totalTick = 0;
    cpus = os.cpus();
    cpu = cpus[i];
    for (type in cpu.times) {
	totalTick += cpu.times[type];
    }
    totalIdle += cpu.times.idle;

    idle = totalIdle / cpus.length;
    total = totalTick / cpus.length;
    return {
	idle: idle,
	total: total
    };
};

cpuILoadInit = function() {
    var index=arguments[0];
    return function() {
	var start;
	start = cpuIAverage(index);
	return function() {
	    var dif, end;
	    end = cpuIAverage(index);
	    dif = {};
	    dif.cpu=index;
	    dif.idle = end.idle - start.idle;
	    dif.total = end.total - start.total;
	    dif.percent = 1 - dif.idle / dif.total;
	    dif.percent = Math.round(dif.percent*100*100)/100;
	    return dif;
	};
    };
};

cpuILoad = (function() {
    var info=[],cpus = os.cpus();
    for (i = 0, len = cpus.length; i < len; i++) {
	var a=cpuILoadInit(i)();
	info.push( a );
    }
    return function() {
	var res=[],cpus = os.cpus();
	for (i = 0, len = cpus.length; i < len; i++) {
	    res.push( info[i]() );
	}
	return res;
    }

})();

app.get('/api/v1/system_information', (req, res) => {

    
    res.send(cpuILoad());    
//    console.log(cpuILoad());
//    var os = require('os');       
//    console.log(os.cpus());
//    console.log(os.totalmem());
//    console.log(os.freemem())
    
});

app.get('/api/v1/backup_services', (req, res) => {
    var files = fs.readdirSync(configFolder);
    var archiver = require('archiver');
    var zip = archiver('zip');
    
    zip.on('error', function(err) {
	res.status(500).send({error: err.message});
    });

    zip.on('end', function() {
	console.log('zip file done - wrote %d bytes', zip.pointer());
    });

    res.attachment('backup.zip');   
    zip.pipe(res);
    
    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var fullfile = '/var/tmp/configs/'+file;
	    console.log('zipping ', fullfile);
	    zip.file(fullfile);
	}	
    });
    zip.finalize();
});

app.get('/api/v1/get_service_count', (req, res) => {
    var services;
		
    obj = new Object();
    var retdata;

    services = activeconfigurations;
    obj.services = services;

    retdata = JSON.stringify(obj);
    console.log(retdata);

    res.send(retdata);    
});

app.get('/api/v1/get_control_page', (req, res) => {
    var html = '';
    var i;
    var files = fs.readdirSync(configFolder);
    var listedfiles = 0;
    
    html += '<table>';
    html += '<thead>';
    html += '<tr class="header">';
    html += '<th>#<div>#</div></th>';
    html += '<th>Name<div>Name</div></th>';
    html += '<th>Control<div>Control</div></th>';
    html += '<th>State<div>State</div></th>';
    html += '<th>Uptime<div>Uptime</div></th>';
    html += '<th>Source<div>Source</div></th>';
    html += '<th>Output<div>Output</div></th>';
    html += '<th>Info<div>Info</div></th>';
    html += '<th>Thumbnail<div>Thumbnail</div></th>';
    html += '<th>Status<div>Status</div></th>';
    html += '</tr>';
    html += '</thead>';
    html += '<tbody>';

    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    var fullfile = '/var/tmp/configs/'+file;
	    var configdata = fs.readFileSync(fullfile, 'utf8');
	    var words = JSON.parse(configdata);
	    var fileprefix = path.basename(fullfile, '.json');
	    
	    listedfiles++;
	    console.log(listedfiles);

	    html += '<tr>';
	    html += '<td>' + configindex + '</td>';
	    html += '<td>' + words.sourcename + '</td>';
            html += '<td>';
	    html += '<button style="width:95%" id=\'start_button'+configindex+'\'>Start </button><br>';
	    html += '<button style="width:95%" id=\'stop_button'+configindex+'\'>Stop </button><br>';
	    html += '<button style="width:95%" id=\'reset_button'+configindex+'\'>Reset</button>';
	    html += '</td>';
	    html += '<td><div id=\'active'+configindex+'\'>';  
	    html += '</div></td>';	    
	    html += '<td><div id=\'uptime'+configindex+'\'>';
	    html += '</div></td>';
	    html += '<td><div id=\'input'+configindex+'\'>';
	    html += '</div></td>';
	    html += '<td>';
	    html += '<table>';
	    html += '<tr>';
	    html += '<td><div id=\'output'+configindex+'\'></div></td>';
	    html += '</tr>';
	    html += '<tr>';
	    html += '<td><div id=\'event'+configindex+'\'></div></td>';
	    html += '</tr>';
	    html += '</table>';
            html += '</td>';
	    html += '<td>';
	    html += '<button style="width:95%" id=\'log'+configindex+'\' type=\'log\'>Event<br>Log</button><br>';
	    //html += '<button style="width:95%" id=\'status'+configindex+'\' type=\'button\'>Detailed<br>Status</button><br>';
	    html += '<button style="width:95%" id=\'remove'+configindex+'\' type=\'remove\'>Remove<br>Service</button>';
	    html += '</td>';
	    html += '<td>';
	    html += '<img src=\'http://'+req.host+'/thumbnail'+fileprefix+'.jpg?='+ new Date().getTime() +'\' id=\'thumbnail'+fileprefix+'\'/>';
	    html += '</td>'
	    html += '<td><div id=\'statusinfo'+configindex+'\'></div></td>';
	    html += '</tr>';
	}
    })
    
    html += '</tbody>';
    html += '</table>';
    
    res.writeHead(200, {
	'Content-Type': 'text/html',
	'Content-Length': html.length,
	'Expires': new Date().toUTCString()
    });
    res.end(html);
});

app.get('/api/v1/get_interfaces', (req, res) => {    
    res.send(networkInterfaces);
});

app.post('/api/v1/restore_services', (req, res) => {
    //
});

app.post('/api/v1/removesource/:uid', (req, res) => {
    console.log('received remove source request: ', req.params.uid);

    var files = fs.readdirSync(configFolder);    
    
    var listedfiles = 0;
    
    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    if (configindex == req.params.uid) {	
		var removeConfig = '/var/tmp/configs/' + file;
		var removeStatus = '/var/tmp/status/' + file;

		try {
		    fs.unlinkSync(removeStatus)
		} catch(err) {
		    console.error(err)
		}
		
		try {
		    fs.unlinkSync(removeConfig)
		} catch(err) {
		    console.error(err)
		}
		activeconfigurations--;   
	    }
	    listedfiles++;
	}
    });
    
    res.send(req.body);
});

app.post('/api/v1/newsource', (req, res) => {
    console.log('received new source request');
    console.log('body is ',req.body);

    var nextconfig = '/var/tmp/configs/' + seconds_since_epoch() + '.json';
    
    fs.writeFile(nextconfig, JSON.stringify(req.body), (err) => {
	if (err) {
	    console.error(err);
	    return;
	};
	console.log('File has been created: ', nextconfig);

	activeconfigurations++;

	console.log('updated active configurations: ', activeconfigurations);
    });

    res.send(req.body);    
});

app.post('/api/v1/status_update/:uid', (req, res) => {
    console.log('received status update from: ', req.params.uid);
    console.log('body is ',req.body);

    var nextstatus = '/var/tmp/status/' + req.params.uid + '.json';

    fs.writeFile(nextstatus, JSON.stringify(req.body), (err) => {
	if (err) {
	    console.error(err);
	    return;
	};
	console.log('File has been created: ', nextstatus);
    });
	
    //res.sendStatus(200);
    res.send(req.body);
});

app.post('/api/v1/stop_clicked/:uid', (req, res) => {
    console.log('stop button pressed: ', req.params.uid);

    var files = fs.readdirSync(configFolder);
    var listedfiles = 0;

    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    if (configindex == req.params.uid) {	
		var fullfile = '/var/tmp/configs/'+file;
		var statusfile = '/var/tmp/status/'+file;
		var configdata = fs.readFileSync(fullfile, 'utf8');
		var words = JSON.parse(configdata);
		console.log('this service maps to current file: ', fullfile);

		var fileprefix = path.basename(fullfile, '.json');
		console.log('the file prefix is: ', fileprefix);

		var touchfile = '/var/tmp/status/'+fileprefix+'.lock';		
		fs.closeSync(fs.openSync(touchfile, 'w'));

		var stop_cmd = 'sudo docker stop livestream'+fileprefix;
		var rm_cmd = 'sudo docker rm livestream'+fileprefix;

		fs.unlinkSync(statusfile)
		
		console.log('stop command: ', stop_cmd);
		exec(stop_cmd, (err, stdout, stderr) => {
		    if (err) {
			console.log('Unable to stop Docker container');
		    } else {
			try {
			    console.log('removing status file: ', statusfile);
			    fs.unlinkSync(statusfile);
			} catch (errremove) {
			    console.error(errremove);
			}			
			console.log('Stopped Docker container- now removing it');
			console.log('remove command: ', rm_cmd);			
			exec(rm_cmd, (err, stdout, stderr) => {
			    if (err) {
				console.log('Unable to remove Docker container');
				fs.unlinkSync(touchfile);
			    } else {
				console.log('Removed Docker container');
				fs.unlinkSync(touchfile);
			    }
			});
		    }
		});		
	    }
	    listedfiles++;
	}
    });

    res.sendStatus(200);    
});

app.post('/api/v1/start_clicked/:uid', (req, res) => {
    const click = {clickTime: new Date()};
    console.log(click);    
    console.log('start button pressed: ', req.params.uid);

    var files = fs.readdirSync(configFolder);
    var listedfiles = 0;

    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    if (configindex == req.params.uid) {	
		var fullfile = '/var/tmp/configs/'+file;
		var configdata = fs.readFileSync(fullfile, 'utf8');
		var words = JSON.parse(configdata);
		console.log('this service maps to current file: ', fullfile);

		var fileprefix = path.basename(fullfile, '.json');
		console.log('the file prefix is: ', fileprefix);

		var output_count = 0;
		var codec = 'h264';
		

		var start_cmd = 'sudo docker run -itd --net=host --name livestream'+fileprefix+' --restart=unless-stopped -v /var/tmp:/var/tmp -v /var/tmp/configs:/var/tmp/configs -v /var/tmp/status:/var/tmp/status -v /var/www/html/hls:/var/www/html/hls -v /var/www/html:/var/www/html dockerfillet /usr/bin/fillet --sources 1 --window '+words.windowsize+' --segment '+words.segmentsize+' --transcode --outputs 1 --vcodec h264 --resolutions 640x360 --vrate 2500 --acodec aac --arate 128 --aspect 16:9 --scte35 --quality 0 --stereo --ip '+words.ipaddr_primary+' --interface '+words.inputinterface1+' --manifest /var/www/html/hls --identity '+fileprefix+' --hls --dash --astreams 1';

		//--verbose --sources 1 --ip 0.0.0.0:9000 --interface enp0s25 --window 20 --segment 2 --identity 2000 --dash --hls --transcode --outputs 2 --vcodec h264 --resolutions 640x360,320x240 --maifest /var/www/html/hls --vrate 2500,1250 --acodec aac --arate 128 --aspect 16:9 --scte35 --quality 0 --stereo --webvtt --astreams 1
		
		console.log('start command: ', start_cmd);
		exec(start_cmd, (err, stdout, stderr) => {
		    if (err) {
			console.log('Unable to run Docker');	
		    } else {
			console.log('Started Docker container');
		    }
		});
	    }
	    listedfiles++;
	}
    });

    res.sendStatus(200);
});

app.get('/api/v1/list_services', (req, res) => {
    //
});

function output_stream(height, width, video_bitrate) {
    this.height = height;
    this.width = width;
    this.video_bitrate = video_bitrate;
}

app.get('/api/v1/get_service_status/:uid', (req, res) => {
    console.log('getting signal status: ', req.params.uid);

    var files = fs.readdirSync(configFolder);       
    var listedfiles = 0;
    var found = 0;
    var sent = 0;
    var locked = 0;

    // use the config file to find the correct status file since
    // they will have the same name but just in a different directory
    files.forEach(file => {
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    console.log('get_service_status: checking configindex ', configindex, ' looking for ', req.params.uid);
	    if (configindex == req.params.uid) {
		var fullfile = '/var/tmp/status/'+file;
		var fileprefix = path.basename(fullfile, '.json');
		var touchfile = '/var/tmp/status/'+fileprefix+'.lock';		
		if (!fs.existsSync(touchfile)) {
		    if (fs.existsSync(fullfile)) {
			var configdata = fs.readFileSync(fullfile, 'utf8');		
			if (configdata) {
			    console.log(configdata);
			    var words = JSON.parse(configdata);
			    var uptime;
			    
			    obj = new Object();
			    var retdata;
			    var current_output;
			    
			    uptime = words.data.system.uptime;
			    obj.uptime = uptime;
			    obj.input_signal = words.data.system["input-signal"];
			    obj.input_interface = words.data.source["interface"];
			    obj.source_ip = words.data.source.stream0["source-ip"];
			    obj.source_width = words.data.source.width;
			    obj.source_height = words.data.source.height;
			    obj.fpsnum = words.data.source.fpsnum;
			    obj.fpsden = words.data.source.fpsden;
			    obj.aspectnum = words.data.source.aspectnum;
			    obj.aspectden = words.data.source.aspectden;
			    obj.videomediatype = words.data.source.videomediatype;
			    obj.audiomediatype0 = words.data.source.audiomediatype0;
			    obj.audiomediatype1 = words.data.source.audiomediatype1;
			    obj.audiochannelsinput0 = words.data.source.audiochannelsinput0;
			    obj.audiochannelsinput1 = words.data.source.audiochannelsinput1;
			    obj.audiochannelsoutput0 = words.data.source.audiochannelsoutput0;
			    obj.audiochannelsoutput1 = words.data.source.audiochannelsoutput1;
			    obj.audiosamplerate0 = words.data.source.audiosamplerate0;
			    obj.audiosamplerate1 = words.data.source.audiosamplerate1;
			    
			    obj.window_size = words.data.system["window-size"];
			    obj.segment_length = words.data.system["segment-length"];
			    obj.hls_active = words.data.system["hls-active"];
			    obj.dash_active = words.data.system["dash-fmp4-active"];
			    obj.video_codec = words.data.system.codec;
			    obj.video_profile = words.data.system.profile;
			    obj.video_quality = words.data.system.quality;
			    
			    obj.source_interruptions = words.data.system["source-interruptions"];
			    obj.source_errors = words.data.system["source-errors"];
			    obj.error_count = words.data.system.error_count;
			    obj.transcoding = words.data.system.transcoding;			
			    obj.scte35 = words.data.system.scte35;
			    obj.video_bitrate = words.data.source.stream0["video-bitrate"];
			    obj.video_frames = words.data.source.stream0["video-received-frames"];
			    
			    obj.outputs = words.data.output.outputs;

			    var streams = [];
			    for (current_output = 0; current_output < words.data.output.outputs; current_output++) {
				var outputstream = 'stream'+current_output;
				var height = words.data.output[outputstream]["output-height"];
				var width = words.data.output[outputstream]["output-width"];
				var video_bitrate = words.data.output[outputstream]["video-bitrate"];
				
				var ostream = new output_stream(height, width, video_bitrate);
				streams.push(ostream);
			    }
			    
			    obj.output_streams = streams;
			    retdata = JSON.stringify(obj);
			    
			    console.log(retdata);
			    
			    res.send(retdata);
			    sent = 1;
			    //return;
			}
		    }
		} else {
		    locked = 1;
		}
	    }
	    listedfiles++;
	}
    });
    
    if (locked == 1) {
	console.log('service is unavailable ', req.params.uid);
	res.sendStatus(503);  // service unavailable
    } else if (sent == 0) {
	console.log('serice was not found ', req.params.uid);
	res.sendStatus(404);  // service not found
    } else {
	//console.log('service was fine ', req.params.uid);
	//res.sendStatus(200);
    }
});

