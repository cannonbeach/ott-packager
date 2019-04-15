console.log('Server-side code running');

var os = require( 'os' );

var networkInterfaces = os.networkInterfaces( );

const express = require('express');

const app = express();

// serve files from the public directory
app.use(express.static('public'));

// start the express web server listening on 8080
app.listen(8080, () => {
    console.log('listening on 8080');
});

// serve the homepage
app.get('/', (req, res) => {
    res.sendFile(__dirname + '/index.html');
});

app.get('/get_interfaces', (req, res) => {    
    res.send(networkInterfaces);
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




