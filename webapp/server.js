console.log('Server-side code running');

var os = require('os');
var networkInterfaces = os.networkInterfaces( );
const express = require('express');
var bodyParser = require('body-parser');
const fs = require('fs');
const app = express();

app.use(bodyParser.json());

// serve files from the public directory
app.use(express.static('public'));

const testFolder = '/var/tmp/configs';

function getExtension(filename) {
    var i = filename.lastIndexOf('.');
    return (i < 0) ? '' : filename.substr(i);
}

function seconds_since_epoch(){ return Math.floor( Date.now() / 1000 ) }

// start the express web server listening on 8080
app.listen(8080, () => {
    console.log('listening on 8080');
});

// serve the homepage
//app.get('/', (req, res) => {
//    
//    console.log('Active configurations: ', activeconfigurations);
//    
//    res.sendFile(__dirname + '/index.html');
//});

var activeconfigurations = 0;

fs.readdir(testFolder, (err, files) => {
    files.forEach(file => {
	console.log(getExtension(file));
	if (getExtension(file) == '.json') {
	    activeconfigurations++;
	}
    });    
});

app.get('/get_configuration_count', (req, res) => {
    res.send(JSON.stringify(activeconfigurations));
});

app.get('/get_configuration_data', (req, res) => {   
});

app.get('/get_control_page', (req, res) => {
    var html = '';
    var i;
    var files = fs.readdirSync('/var/tmp/configs');
    var listedfiles = 0;
    
    html += '<table>';
    html += '<thead>';
    html += '<tr class="header">';
    html += '<th>Session ID<div>Session ID</div></th>';
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
	    html += '<td>ACTIVE</td>';
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

app.get('/get_interfaces', (req, res) => {    
    res.send(networkInterfaces);
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
	console.log("File has been created");

	activeconfigurations++;

	console.log('updated active configurations: ', activeconfigurations);
    });

    res.send(req.body);    
});

app.post('/start_clicked', (req, res) => {
    const click = {clickTime: new Date()};
    console.log(click);

    var http = require('http');

    var req = http.request('http://127.0.0.1:18000/api/v1/status', function (res) {
	var responseString = "";
	console.log('sending off an http request');
	res.on("data", function(data) {
  	    responseString += data;
        });
	res.on("end", function() {
	    console.log(responseString);
	});
    });
    req.write('ping');
    req.end();
    res.sendStatus(201);
});




