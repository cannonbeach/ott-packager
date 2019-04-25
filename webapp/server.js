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
    var files = fs.readdirSync('/var/tmp/configs');
    var listedfiles = 0;
    
    html += '<table>';
    html += '<thead>';
    html += '<tr class="header">';
    html += '<th>Service<div>Service</div></th>';
    html += '<th>Name<div>Name</div></th>';
    html += '<th>Process Control<div>Process Control</div></th>';
    html += '<th>Ingesting<div>Ingesting</div></th>';
    html += '<th>Uptime<div>Uptime</div></th>';
    html += '<th>Signal<div>Signal</div></th>';
    html += '<th>Session Control<div>Session Control</div></th>';
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
	    
	    listedfiles++;
	    console.log(listedfiles);

	    html += '<tr>';
	    html += '<td>' + configindex + '</td>';
	    html += '<td>' + words.sourcename + '</td>';
            html += '<td>';
	    html += '<button id=\'start_button'+configindex+'\'>Start</button>';
	    html += '<button id=\'stop_button'+configindex+'\'>Stop</button>';
	    html += '<button id=\'reset_button'+configindex+'\'>Reset</button>';
	    html += '</td>';
	    html += '<td><div id=\'active'+configindex+'\'>';  
	    html += '</div></td>';	    
	    html += '<td></td>';
	    html += '<td>YES</td>';
	    html += '<td>';
	    html += '<button id=\'log'+configindex+'\' type=\'log\'>Show<br>Log</button>';
	    html += '<button id=\'status'+configindex+'\' type=\'button\'>Show<br>Status</button>';
	    html += '<button id=\'edit'+configindex+'\' type=\'edit\'>Update<br>Session</button>';
	    html += '<button id=\'remove'+configindex+'\' type=\'remove\'>Remove<br>Session</button>';
	    html += '</td>';
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

app.post('/api/v1/removesource/:uid', (req, res) => {
    console.log('received remove source request: ', req.params.uid);

    var files = fs.readdirSync('/var/tmp/configs');    
    
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

    var files = fs.readdirSync('/var/tmp/configs');
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

		var stop_cmd = 'sudo docker stop livestream'+fileprefix;
		var rm_cmd = 'sudo docker rm livestream'+fileprefix;
		
		console.log('stop command: ', stop_cmd);
		exec(stop_cmd, (err, stdout, stderr) => {
		    if (err) {
			console.log('Unable to stop Docker container');
		    } else {
			console.log('Stopped Docker container- now removing it');
			console.log('remove command: ', rm_cmd);
			exec(rm_cmd, (err, stdout, stderr) => {
			    if (err) {
				console.log('Unable to remove Docker container');
			    } else {
				console.log('Removed Docker container');
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

    var files = fs.readdirSync('/var/tmp/configs');
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

		var start_cmd = 'sudo docker run -itd --net=host --name livestream'+fileprefix+' --restart=unless-stopped -v /var/tmp:/var/tmp -v /var/tmp/configs:/var/tmp/configs -v /var/tmp/status:/var/tmp/status -v /var/www/html/hls:/var/www/html/hls dockerfillet /usr/bin/fillet --sources 1 --window '+words.windowsize+' --segment '+words.segmentsize+' --transcode --outputs 1 --vcodec h264 --resolutions 640x360 --vrate 2500 --acodec aac --arate 128 --aspect 16:9 --scte35 --quality 0 --stereo --ip '+words.ipaddr_primary+' --interface '+words.inputinterface1+' --manifest /var/www/html/hls --identity '+fileprefix+' --hls --dash --astreams 1';

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

app.get('/api/v1/get_service_status/:uid', (req, res) => {
    console.log('getting signal status: ', req.params.uid);

    var files = fs.readdirSync('/var/tmp/status');       
    var listedfiles = 0;
    var found = 0;
    var sent = 0;
    
    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    var configindex = listedfiles + 1;
	    console.log('checking configindex ', configindex);
	    if (configindex == req.params.uid) {
		var fullfile = '/var/tmp/status/'+file;
		var configdata = fs.readFileSync(fullfile, 'utf8');
		console.log(configdata);
		var words = JSON.parse(configdata);
		var uptime;
		
		obj = new Object();
		var retdata;

		console.log('uptime: ',words.data.system.uptime);
		console.log('input.signal: ',words.data.system["input-signal"]);
		uptime = words.data.system.uptime;
		obj.uptime = uptime;
		obj.input_signal = words.data.system["input-signal"];
		
		retdata = JSON.stringify(obj);

		console.log(retdata);

		res.send(retdata);
		sent = 1;
		return;
	    }
	}	
    });
    if (sent == 0) {
	res.sendStatus(404);  // service not found
    }
});

/*		    
    var http = require('http');
    var httpurl = 'http://127.0.0.1:18000/api/v1/status';
    var words;
    var obj = null;
    var retdata = null;
    var reqstatus = http.request(httpurl, function (resstatus) {
	var responseString = "";
	console.log('sending off an http request');
	resstatus.on("data", function(data) {
  	    responseString += data;
        });
	resstatus.on("end", function() {
	    var words = JSON.parse(responseString);
	    var uptime;

	    obj = new Object();
	    var retdata;
	    //console.log(responseString);
	    console.log(words.data.system.uptime);

	    uptime = words.data.system.uptime;
	    obj.uptime = uptime;

	    retdata = JSON.stringify(obj);
	    console.log(retdata);

	    res.send(retdata);
	});
    });
    reqstatus.on('error', function(err) {
	console.log('unable to reach the server');
    });

    reqstatus.end();
});
*/



