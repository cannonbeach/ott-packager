/*****************************************************************************
  Copyright (C) 2018-2023 John William

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111, USA.

  This program is also available with customization/support packages.
  For more information, please contact me at cannonbeachgoonie@gmail.com

*******************************************************************************/

console.log('Server-side code running');

const { createLogger, transports, format } = require('winston');
var exec = require('child_process').exec;
var os = require('os');
var networkInterfaces = os.networkInterfaces( );
const express = require('express');
const readLastLines = require('read-last-lines');
var bodyParser = require('body-parser');
const fs = require('fs');
const smi = require('node-nvidia-smi');
const app = express();
var path = require('path');

app.use(bodyParser.json());

// serve files from the public directory
app.use(express.static('public'));

const logfilename = '/var/log/eventlog.log';

const logger = createLogger({
//    format: format.combine(
//        format.json(),
//        format.timestamp()
    //    ),
    format: format.json(),
    transports: [
        new transports.File( {
            filename: logfilename,
            json: true,
            colorize: true
        })
    ]
});

const scanFolder = '/var/tmp/scan';
const configFolder = '/var/tmp/configs';
const statusFolder = '/var/tmp/status';
const apacheFolder = '/var/www/html';
const logFolder = '/var/log';

function getExtension(filename) {
    var i = filename.lastIndexOf('.');
    return (i < 0) ? '' : filename.substr(i);
}

function seconds_since_epoch(){ return Math.floor( Date.now() / 1000 ) }

// start the express web server listening on 8080
app.listen(8080, () => {
    console.log('listening on 8080');
});

function getNewestFile(dir, regexp) {
    newest = null;
    files = fs.readdirSync(dir)
    one_matched = 0
    for (i = 0; i < files.length; i++) {
        if (regexp.test(files[i]) == false) {
            continue;
        } else if (one_matched == 0) {
            newest = files[i];
            one_matched = 1;
            continue;
        }

        f1_time = fs.statSync(files[i]).mtime.getTime();
        f2_time = fs.statSync(newest).mtime.getTime();
        if (f1_time > f2_time) {
            newest = files[i]
        }
    }

    if (newest != null) {
        return (dir + '/' + newest);
    }
    return null;
}

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
    var retdata;

    var gpucount = 0;
    var gpudriver;
    var gpuname;
    var gpudecodeload;
    var gpuencodeload;
    var gpucudaload;

    smi(function (err, data) {
        if (err) {
            console.warn(err);
        } else {
            var retdata;
            var gpudata = JSON.stringify(data, null, ' ');
            var words = JSON.parse(gpudata);

            obj = new Object();
            obj.cpuinfo = cpuILoad();
            obj.totalmem = os.totalmem();
            obj.freemem = os.freemem();

            //console.log(gpudata);
            gpucount = words.nvidia_smi_log.attached_gpus;
            gpudriver = words.nvidia_smi_log.driver_version;

            //console.log(gpucount);
            //console.log(gpudriver);
            if (gpucount == 1) {
                gpuname = words.nvidia_smi_log.gpu.product_name;
                gpudecodeload = words.nvidia_smi_log.gpu.utilization.decoder_util;
                gpuencodeload = words.nvidia_smi_log.gpu.utilization.encoder_util;
                gpucudaload = words.nvidia_smi_log.gpu.utilization.gpu_util;
            }
            //console.log(gpudecodeload);
            //console.log(gpuencodeload);
            //console.log(gpucudaload);

            obj.gpucount = gpucount;
            obj.gpudriver = gpudriver;
            obj.gpudecodeload = gpudecodeload;
            obj.gpuencodeload = gpuencodeload;
            obj.gpucudaload = gpucudaload;
            retdata = JSON.stringify(obj);
            //console.log(retdata);
            res.send(retdata);
        }
    });
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
            var fullfile = configFolder+'/'+file;
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
    res.send(retdata);
});

app.get('/api/v1/get_scan_data', (req, res) => {
    var source;
    var sourcestreams = [];
    var retdata;
    var address = req.query.address;
    var intf = req.query.intf;

    console.log('get_scan_data address: '+address);

    if (address == '' || address == null) {
        var configdata = [];

        obj = new Object();
        obj.sources = configdata;

        retdata = JSON.stringify(obj);

        console.log('sending back scan data: '+retdata);

        res.send(retdata);
    } else {
        var fullfile = scanFolder+'/'+address+'_simple.json';
        if (fs.existsSync(fullfile)) {
            var configdata = fs.readFileSync(fullfile, 'utf8');
            var parsedconfig = JSON.parse(configdata);

            obj = new Object();
            obj.sources = parsedconfig;

            retdata = JSON.stringify(parsedconfig);

            console.log('sending back scan data: '+retdata);

            res.send(retdata);
        } else {
            var configdata = [];

            obj = new Object();
            obj.sources = configdata;

            retdata = JSON.stringify(obj);

            console.log('sending back scan data: '+retdata);

            res.send(retdata);
        }
    }
});

app.get('/api/v1/get_repackage_page', (req, res) => {
    var html = '';

    html += '<div class="row">';
    html += '<div class="col-50">';
    html += '<h2>Add A New Source For Stream Repackaging</h2>';
    html += '<section class="configtable">';
    html += '<table>';
    html += '<thead>';
    html += '<tr>';
    html += '<th><div>Config Option</div></th>';
    html += '<th><div>Config Information </div></th>';
    html += '<th><div></div></th>';
    html += '<th><div></div></th>';
    html += '</tr>';
    html += '</thead>';
    html += '<tbody>';
    html += '<tr>';
    html += '<td>';
    html += '<label>Channel Name</label>';
    html += '</td>';
    html += '<td>';
    html += '<input type="text" id="repackage_sourcename" name="repackage_sourcename" placeholder="XYZ Sports Network"/>';
    html += '</td>';
    html += '</tr>';
    html += '<tr>';
    html += '<td>';
    html += '<label>Video Sources</label>';
    html += '</td>';
    html += '<td>';
    html += '<select id="repackage_videosources" onchange="update_videosources(this)">';
    html += '<option value="1">1 Video Profile</option>';
    html += '<option value="2">2 Video Profiles</option>';
    html += '<option value="3">3 Video Profiles</option>';
    html += '<option value="4">4 Video Profiles</option>';
    html += '<option value="5">5 Video Profiles</option>';
    html += '<option value="6">6 Video Profiles</option>';
    html += '<option value="7">7 Video Profiles</option>';
    html += '<option value="8">8 Video Profiles</option>';
    html += '</select>';
    html += '</td>';
    html += '<td>';
    html += '</td>';
    html += '<td>';
    html += '</td>';
    html += '</tr>';
    html += '<tr>';
    html += '<td>';
    html += '<label>Audio Sources</label>';
    html += '</td>';
    html += '<td>';
    html += '<select id="repackage_audiosources">';
    html += '<option value="1">1 Audio Profile</option>';
    html += '<option value="2">2 Audio Profiles</option>';
    html += '</select>';
    html += '</td>';
    html += '<td>';
    html += '</td>';
    html += '<td>';
    html += '</td>';
    html += '</tr>';
    html += '<tr>';
    html += '<td>';
    html += '<label>Network Interface</label>';
    html += '</td>';
    html += '<td>';
    html += '<select id="repackage_inputinterface">';

    var parsedJSON = JSON.parse(JSON.stringify(networkInterfaces));
    for (var prop in parsedJSON) {
        if (parsedJSON.hasOwnProperty(prop)) {
            html += '<option value='+prop+'>'+prop+'</option>';
        }
    }

    html += '</select>';
    html += '</td>';
    html += '</tr>';
    html += '</tbody>';
    html += '</table>';
    html += '</section>';
    html += '</div>';
    html += '</div>';

    res.writeHead(200, {
        'Content-Type': 'text/html',
        'Content-Length': html.length,
        'Expires': new Date().toUTCString()
    });
    res.end(html);
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
    html += '<th>Input Thumbnail<div>Input Thumbnail</div></th>';
    html += '<th>Status<div>Status</div></th>';
    html += '</tr>';
    html += '</thead>';
    html += '<tbody>';

    files.forEach(file => {
        console.log(getExtension(file));
        if (getExtension(file) == '.json') {
            var configindex = listedfiles + 1;
            var fullfile = configFolder+'/'+file;
            var configdata = fs.readFileSync(fullfile, 'utf8');
            var words = JSON.parse(configdata);
            var fileprefix = path.basename(fullfile, '.json');

            listedfiles++;
            //console.log(listedfiles);

            if (words.filletmode == "transcode") {
                html += '<tr>';
                html += '<td>' + configindex + '</td>';
                html += '<td>' + words.sourcename + '</td>';
                html += '<td>';
                html += '<button style="width:95%" id=\'start_service'+configindex+'\'>Start </button><br>';
                html += '<button style="width:95%" id=\'stop_service'+configindex+'\'>Stop </button><br>';
                html += '<button hidden style="width:95%" id=\'reset_button'+configindex+'\'>Reset</button>';
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
                html += '<img src=\'http://'+req.hostname+'/thumbnail'+fileprefix+'.jpg?='+ new Date().getTime() +'\' id=\'thumbnail'+fileprefix+'\'/>';
                html += '</td>'
                html += '<td><div id=\'statusinfo'+configindex+'\'></div></td>';
                html += '</tr>';
            } else {
                html += '<tr>';
                html += '<td>' + configindex + '</td>';
                html += '<td>' + words.sourcename + '</td>';
                html += '<td>';
                html += '<button style="width:95%" id=\'start_service'+configindex+'\'>Start </button><br>';
                html += '<button style="width:95%" id=\'stop_service'+configindex+'\'>Stop </button><br>';
                html += '<button hidden style="width:95%" id=\'reset_button'+configindex+'\'>Reset</button>';
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
                html += '<label>No Thumbnail<br>Available<br>In Repackage Mode</label>';
                //html += '<img src=\'http://'+req.hostname+'/thumbnail'+fileprefix+'.jpg?='+ new Date().getTime() +'\' id=\'thumbnail'+fileprefix+'\'/>';
                html += '</td>'
                html += '<td><div id=\'statusinfo'+configindex+'\'></div></td>';
                html += '</tr>';
            }
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

app.get('/api/v1/get_last_error/:uid', (req, res) => {
    //responds with last error
});

app.get('/api/v1/get_last_success/:uid', (req, res) => {
    //response with last successful transaction
});

app.get('/api/v1/get_event_log/:uid', (req, res) => {
    //console.log('received event log request: ', req.params.uid);

    var newestlog = 'eventlog.log';
    //getNewestFile(logFolder, new RegExp('eventlog.log'))
    console.log('newest log filename: ', newestlog);

    readLastLines.read(newestlog, 10)
        .then((lines) => {
            console.log(lines)
            var retdata;
            var current_status = 'success';
            obj = new Object();
            obj = lines;
            retdata = obj;
            //obj.status = lines; //current_status;
            //retdata = JSON.stringify(obj);
            console.log(retdata);
            res.send(retdata);
        });
});

app.post('/api/v1/remove_service/:uid', (req, res) => {
    console.log('received remove source request: ', req.params.uid);

    var files = fs.readdirSync(configFolder);
    var responding = 0;
    var listedfiles = 0;

    files.forEach(file => {
        console.log(getExtension(file));
        if (getExtension(file) == '.json') {
            var configindex = listedfiles + 1;
            var fullfile = configFolder+'/'+file;
            var fileprefix = path.basename(fullfile, '.json');
            if ((configindex == req.params.uid) || (fileprefix == req.params.uid)) {
                var removeConfig = fullfile;
                var removeStatus = statusFolder+'/' + file;

                activeconfigurations--;
                try {
                    fs.unlinkSync(removeStatus)
                } catch(err) {
                    console.error(err)
                }

                responding = 1;
                try {
                    fs.unlinkSync(removeConfig)
                    var retdata;
                    var current_status = 'success';
                    obj = new Object();
                    obj.status = current_status;
                    retdata = JSON.stringify(obj);
                    console.log(retdata);
                    res.send(retdata);
                } catch(err) {
                    console.error(err)
                    var retdata;
                    var current_status = 'failed';
                    obj = new Object();
                    obj.status = current_status;
                    retdata = JSON.stringify(obj);
                    console.log(retdata);
                    res.send(retdata);
                }
            }
            listedfiles++;
        }
    });

    if (!responding) {
        var retdata;
        var current_status = 'invalid';
        obj = new Object();
        obj.status = current_status;
        retdata = JSON.stringify(obj);
        console.log(retdata);
        res.send(retdata);
    }
});

app.post('/api/v1/new_service', (req, res) => {
    console.log('received new source request');
    console.log('body is ',req.body);

    //this could cause a collision if multiple services are created at the exact same time
    //so we should look at adding another modifier
    //if the file already exists, we should wait and try again
    var servicenum = seconds_since_epoch();
    var nextconfig = configFolder+'/'+servicenum+'.json';

    fs.writeFile(nextconfig, JSON.stringify(req.body), (err) => {
        if (err) {
            console.error(err);
            return;
        };
        console.log('File has been created: ', nextconfig);

        activeconfigurations++;

        console.log('updated active configurations: ', activeconfigurations);
    });

    var retdata;

    obj = new Object();

    obj.servicenum = servicenum;
    retdata = JSON.stringify(obj);
    console.log(retdata);
    res.send(retdata);
});

app.post('/api/v1/status_update/:uid', (req, res) => {
    console.log('received status update from: ', req.params.uid);
    //console.log('body is ',req.body);

    var nextstatus = statusFolder+'/'+req.params.uid+'.json';

    fs.writeFile(nextstatus, JSON.stringify(req.body), (err) => {
        if (err) {
            console.error(err);
            return;
        };
        console.log('File has been created: ', nextstatus);
    });

    res.send(req.body);
});

app.post('/api/v1/signal/:uid', (req, res) => {
    console.log('receive event signal from: ', req.params.uid);
    console.log('body is ', req.body);
    logger.info(req.body);
    res.send(req.body);
});

app.post('/api/v1/stop_service/:uid', (req, res) => {
    console.log('stop button pressed: ', req.params.uid);

    var files = fs.readdirSync(configFolder);
    var listedfiles = 0;
    var responding = 0;

    files.forEach(file => {
        console.log(getExtension(file));
        if (getExtension(file) == '.json') {
            var fullfile = configFolder+'/'+file;
            var fileprefix = path.basename(fullfile, '.json');
            var configindex = listedfiles + 1;
            if ((configindex == req.params.uid) || (fileprefix == req.params.uid)) {
                var statusfile = statusFolder+'/'+file;
                var configdata = fs.readFileSync(fullfile, 'utf8');
                var words = JSON.parse(configdata);
                console.log('this service maps to current file: ', fullfile);
                console.log('the file prefix is: ', fileprefix);

                var touchfile = statusFolder+'/'+fileprefix+'.lock';
                fs.closeSync(fs.openSync(touchfile, 'w'));

                var stop_cmd = 'sudo docker rm -f livestream'+fileprefix;
                if (fs.existsSync(statusfile)) {
                    fs.unlinkSync(statusfile)
                }

                console.log('stop command: ', stop_cmd);
                responding = 1;
                exec(stop_cmd, (err, stdout, stderr) => {
                    if (err) {
                        console.log('Unable to stop Docker container');
                        var retdata;
                        var current_status = 'failed';
                        obj = new Object();
                        obj.status = current_status;
                        retdata = JSON.stringify(obj);
                        console.log(retdata);
                        res.send(retdata);
                    } else {
                        var failed_sent = 0;
                        try {
                            console.log('removing status file: ', statusfile);
                            fs.unlinkSync(statusfile);
                        } catch (errremove) {
                            console.error(errremove);
                            fs.unlinkSync(touchfile); // remove this?
                            failed_sent = 1;
                            var retdata;
                            var current_status = 'failed';
                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        }
                        if (!failed_sent) {
                            console.log('Stopped and Removed Docker container');
                            fs.unlinkSync(touchfile);
                            var retdata;
                            var current_status = 'success';
                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        }
                    }
                });
            }
            listedfiles++;
        }
    });

    if (!responding) {
        var retdata;
        var current_status = 'invalid';
        obj = new Object();
        obj.status = current_status;
        retdata = JSON.stringify(obj);
        console.log(retdata);
        res.send(retdata);
    }
});

function os_func() {
    this.execCommand = function(cmd) {
        return new Promise((resolve, reject) => {
            exec(cmd, (err, stdout, stderr) => {
                if (err) {
                    reject(err);
                    return;
                }
                resolve(stdout)
            });
        })
    }
}

app.post('/api/v1/start_service/:uid', (req, res) => {
    const click = {clickTime: new Date()};
    console.log(click);
    console.log('start button pressed: ', req.params.uid);

    var files = fs.readdirSync(configFolder);
    var listedfiles = 0;
    var valid_service = 0;
    var responding = 0;

    files.forEach(file => {
        console.log(getExtension(file));
        if (getExtension(file) == '.json') {
            var fullfile = configFolder+'/'+file;
            var fileprefix = path.basename(fullfile, '.json');
            var configindex = listedfiles + 1;
            if ((configindex == req.params.uid) || (fileprefix == req.params.uid)) {
                var configdata = fs.readFileSync(fullfile, 'utf8');
                var words = JSON.parse(configdata);
                console.log('this service maps to current file: ', fullfile);
                console.log('the file prefix is: ', fileprefix);

                valid_service = 1;

                var touchfile = statusFolder+'/'+fileprefix+'.lock';
                fs.closeSync(fs.openSync(touchfile, 'w'));

                var operationmode = words.filletmode;

                if (operationmode === 'srtcaller') {
                    //to be completed
                }

                if (operationmode === 'srtlistener') {
                    //to be completed
                }

                if (operationmode === 'srtrendezvous') {
                    //to be completed
                }

                if (operationmode === 'repackage') {
                    var videosources = words.videosources;
                    var audiosources = words.audiosources;
                    var output_dash_enable = '';
                    if (words.enabledash === 'on') {
                        output_dash_enable = '--dash';
                    }
                    var output_hls_enable = '';
                    var output_scte35_enable = '';
                    if (words.enablehls === 'on') {
                        output_hls_enable = '--hls';
                    }

                    var manifest_string = '';
                    var manifestdirectory;
                    if (words.manifestdirectory === null || words.manifestdirectory === '' || !words.manifestdirectory) {
                        manifestdirectory = apacheFolder+'/hls';
                    } else {
                        manifestdirectory = words.manifestdirectory;
                    }
                    var hlsmanifest;
                    if (words.hlsmanifest === null || words.hlsmanifest === '' || !words.hlsmanifest) {
                        hlsmanifest = 'master.m3u8';
                    } else {
                        hlsmanifest = words.hlsmanifest;
                    }
                    var fmp4manifest;
                    if (words.fmp4manifest === null || words.fmp4manifest === '' || !words.fmp4manifest) {
                        fmp4manifest = 'masterfmp4.m3u8';
                    } else {
                        fmp4manifest = words.fmp4manifest;
                    }
                    var dashmanifest;
                    if (words.dashmanifest === null || words.dashmanifest === '' || !words.dashmanifest) {
                        dashmanifest = 'master.mpd';
                    } else {
                        dashmanifest = words.dashmanifest;
                    }

                    var start_cmd = 'sudo docker run -itd --net=host --name livestream'+fileprefix+' --restart=unless-stopped --log-opt max-size=25m -v /var/tmp:/var/tmp -v '+configFolder+':'+configFolder+' -v '+statusFolder+':'+statusFolder+' -v '+manifestdirectory+':'+manifestdirectory+' -v '+apacheFolder+':'+apacheFolder+' dockerfillet_repackage /usr/bin/fillet_repackage --vsources '+videosources+' --asources '+audiosources+' ';

                    start_cmd += '--vip';
                    if (videosources >= 1) {
                        start_cmd += ' '+words.source_video1;
                    }
                    if (videosources >= 2) {
                        start_cmd += ','+words.source_video2;
                    }
                    if (videosources >= 3) {
                        start_cmd += ','+words.source_video3;
                    }
                    if (videosources >= 4) {
                        start_cmd += ','+words.source_video4;
                    }
                    if (videosources >= 5) {
                        start_cmd += ','+words.source_video5;
                    }
                    if (videosources >= 6) {
                        start_cmd += ','+words.source_video6;
                    }
                    if (videosources >= 7) {
                        start_cmd += ','+words.source_video7;
                    }
                    if (videosources >= 8) {
                        start_cmd += ','+words.source_video8;
                    }
                    start_cmd += ' ';
                    start_cmd += '--aip';
                    if (audiosources >= 1) {
                        start_cmd += ' '+words.source_audio1;
                    }
                    if (audiosources >= 2) {
                        start_cmd += ','+words.source_audio2;
                    }
                    start_cmd += ' ';
                    start_cmd += '--interface '+words.inputinterface;
                    start_cmd += ' ';
                    start_cmd += '--manifest '+manifestdirectory;
                    start_cmd += ' ';
                    start_cmd += '--identity '+fileprefix;
                    start_cmd += ' ';

                    start_cmd += output_hls_enable;
                    // dash?
                    start_cmd += ' ';

                    manifest_string += '--manifest-hls '+hlsmanifest+' ';
                    manifest_string += '--manifest-fmp4 '+fmp4manifest+' ';
                    manifest_string += '--manifest-dash '+dashmanifest;

                    start_cmd += manifest_string;

// --sources 1 --window '+words.windowsize+' --segment '+words.segmentsize+' --transcode --gpu '+gpuselect+' --outputs '+output_count+' --vcodec '+codec+' --resolutions '+resolution_string+' --vrate '+bitrate_string+' --acodec aac --arate '+words.audiobitrate+' --aspect 16:9 '+output_scte35_enable+' --quality '+words.videoquality+' --stereo --ip '+words.ipaddr_primary+' --interface '+words.inputinterface1+' --manifest '+manifestdirectory+' --select '+selectedstream1+' --identity '+fileprefix+' '+output_hls_enable+' '+output_dash_enable+' --astreams '+astreams+' '+manifest_string;

                    console.log('start command: ', start_cmd);

                    responding = 1;
                    exec(start_cmd, (err, stdout, stderr) => {
                        if (err) {
                            var retdata;
                            var current_status = 'failed';

                            console.log('Unable to run Docker');
                            fs.unlinkSync(touchfile);
                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        } else {
                            var retdata;
                            var current_status = 'success';

                            console.log('Started Docker container');
                            fs.unlinkSync(touchfile);

                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        }
                    });
                }

                if (operationmode === 'transcode') {
                    var output_count = 0;
                    var codec = words.videocodec;

                    var video_bitrate1 = parseInt(words.video_bitrate1);
                    var video_bitrate2 = parseInt(words.video_bitrate2);
                    var video_bitrate3 = parseInt(words.video_bitrate3);
                    var video_bitrate4 = parseInt(words.video_bitrate4);
                    var resolution_string = '';
                    var bitrate_string = '';
                    if (video_bitrate1) {
                        if (!isNaN(video_bitrate1)) {
                            output_count++;
                            resolution_string = words.outputresolution1;
                            bitrate_string = words.video_bitrate1;
                            if (video_bitrate2) {
                                if (!isNaN(video_bitrate2)) {
                                    output_count++;
                                    resolution_string += ','+words.outputresolution2;
                                    bitrate_string += ','+words.video_bitrate2;
                                    if (video_bitrate3) {
                                        if (!isNaN(video_bitrate3)) {
                                            output_count++;
                                            resolution_string += ','+words.outputresolution3;
                                            bitrate_string += ','+words.video_bitrate3;
                                            if (video_bitrate4) {
                                                if (!isNaN(video_bitrate4)) {
                                                    output_count++;
                                                    resolution_string += ','+words.outputresolution4;
                                                    bitrate_string += ','+words.video_bitrate4;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    //managementserverip
                    //publishpoint1
                    //cdnusername1
                    //cdnpassword1
                    //enablescte35
                    //ipaddr_backup
                    //inputinterface2
                    //enablehls
                    //enabledash

                    var astreams = parseInt(words.audiosources);
                    if (isNaN(astreams)) {
                        astreams = 1;
                    }
                    var manifest_string = '';
                    var manifestdirectory;
                    if (words.manifestdirectory === null || words.manifestdirectory === '' || !words.manifestdirectory) {
                        manifestdirectory = apacheFolder+'/hls';
                    } else {
                        manifestdirectory = words.manifestdirectory;
                    }
                    var hlsmanifest;
                    if (words.hlsmanifest === null || words.hlsmanifest === '' || !words.hlsmanifest) {
                        hlsmanifest = 'master.m3u8';
                    } else {
                        hlsmanifest = words.hlsmanifest;
                    }
                    var fmp4manifest;
                    if (words.fmp4manifest === null || words.fmp4manifest === '' || !words.fmp4manifest) {
                        fmp4manifest = 'masterfmp4.m3u8';
                    } else {
                        fmp4manifest = words.fmp4manifest;
                    }
                    var dashmanifest;
                    if (words.dashmanifest === null || words.dashmanifest === '' || !words.dashmanifest) {
                        dashmanifest = 'master.mpd';
                    } else {
                        dashmanifest = words.dashmanifest;
                    }

                    var output_dash_enable = '';
                    if (words.enabledash === 'on') {
                        output_dash_enable = '--dash';
                    }
                    var output_hls_enable = '';
                    var output_scte35_enable = '';
                    if (words.enablehls === 'on') {
                        output_hls_enable = '--hls';
                        if (words.enablescte35 === 'on') {
                            output_scte35_enable = '--scte35';
                        }
                    }

                    var gpuselect = parseInt(words.gpuselect);
                    if (isNaN(gpuselect)) {
                        gpuselect = 0;
                    }

                    var selectedstream1 = parseInt(words.selectedstream1);

                    manifest_string += '--manifest-hls '+hlsmanifest+' ';
                    manifest_string += '--manifest-fmp4 '+fmp4manifest+' ';
                    manifest_string += '--manifest-dash '+dashmanifest;

                    var gpu_mapping = '-e NVIDIA_DRIVER_CAPABILITIES=all -e NVIDIA_VISIBLE_DEVICES=all --gpus all';

                    // enablestereo on by default for now
                    // aspect not currently set
                    var start_cmd = 'sudo docker run -itd --net=host --name livestream'+fileprefix+' --restart=unless-stopped --log-opt max-size=25m -v /var/tmp:/var/tmp -v '+configFolder+':'+configFolder+' -v '+statusFolder+':'+statusFolder+' '+gpu_mapping+' -v '+manifestdirectory+':'+manifestdirectory+' -v '+apacheFolder+':'+apacheFolder+' dockerfillet_transcode /usr/bin/fillet_transcode --sources 1 --window '+words.windowsize+' --segment '+words.segmentsize+' --transcode --gpu '+gpuselect+' --outputs '+output_count+' --vcodec '+codec+' --resolutions '+resolution_string+' --vrate '+bitrate_string+' --acodec aac --arate '+words.audiobitrate+' --aspect 16:9 '+output_scte35_enable+' --quality '+words.videoquality+' --stereo --ip '+words.ipaddr_primary+' --interface '+words.inputinterface1+' --manifest '+manifestdirectory+' --select '+selectedstream1+' --identity '+fileprefix+' '+output_hls_enable+' '+output_dash_enable+' --astreams '+astreams+' '+manifest_string;

                    console.log('start command: ', start_cmd);

                    responding = 1;
                    exec(start_cmd, (err, stdout, stderr) => {
                        if (err) {
                            var retdata;
                            var current_status = 'failed';

                            console.log('Unable to run Docker');
                            fs.unlinkSync(touchfile);
                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        } else {
                            var retdata;
                            var current_status = 'success';

                            console.log('Started Docker container');
                            fs.unlinkSync(touchfile);

                            obj = new Object();
                            obj.status = current_status;
                            retdata = JSON.stringify(obj);
                            console.log(retdata);
                            res.send(retdata);
                        }
                    });

                }//transcode operation done
            }
            listedfiles++;
        }
    });

    if (!responding) {
        var retdata;
        var current_status = 'invalid';
        obj = new Object();
        obj.status = current_status;
        retdata = JSON.stringify(obj);
        console.log(retdata);
        res.send(retdata);
    }
});

function scan_response_video(streamindex, avtype, codec, width, height, framerate, bitrate, pid) {
    this.streamindex = streamindex;
    this.avtype = avtype;
    this.codec = codec;
    this.width = width;
    this.height = height;
    this.framerate = framerate;
    this.bitrate = bitrate;
    this.pid = pid;
}

function scan_response_audio(streamindex, avtype, codec, channels, samplerate, bitrate, pid) {
    this.streamindex = streamindex;
    this.avtype = avtype;
    this.codec = codec;
    this.channels = channels;
    this.samplerate = samplerate;
    this.bitrate = bitrate;
    this.pid = pid;
}

function scan_response_data(streamindex, avtype, codec, pid) {
    this.streamindex = streamindex;
    this.avtype = avtype;
    this.codec = codec;
    this.pid = pid;
}

app.post('/api/v1/scan', (req, res) => {
    console.log('address: '+JSON.stringify(req.query.address));
    console.log('interface: '+JSON.stringify(req.query.intf));

    var address = req.query.address;
    var intf = req.query.intf;

    console.log('address: '+address+' interface '+intf);

    var words = req.body;
    var input_sources = 1;
    var i;
    var programdata = [];
    var descriptivedata = [];

    console.log('input_sources: ', input_sources);
    for (i = 0; i < input_sources; i++) {
        var retdata;
        var scan_cmd = 'ffprobe -v quiet -timeout 20 -print_format json -show_format -show_programs udp://'+address+'?reuse=1';

        console.log('running: ', scan_cmd);

        exec(scan_cmd, (err, stdout, stderr) => {
            if (err) {
                var retdata;
                var current_status = 'failed';

                console.log('Unable to run ffprobe');

                obj = new Object();
                obj.status = current_status;
                retdata = JSON.stringify(obj);
                console.log(retdata);
                res.send(retdata);
            } else {
                var retdata;
                var current_status = 'success';

                console.log('ffprobe run successfully');

                var scan_data = stdout;

                var parsed_data = JSON.parse(scan_data);
                //var nb_streams = parsed_data.format.nb_streams;
                var programs_list = parsed_data.programs;
                var nb_programs = parsed_data.programs.length;

                console.log('nb_programs ', nb_programs);
                console.log('programs ', programs_list);

                var s;
                var p;
                var sources = '';
                for (p = 0; p < nb_programs; p++) {
                    var new_stream;

                    var program_id = parsed_data.programs[p].program_id;
                    var program_num = parsed_data.programs[p].program_num;
                    var nb_streams = parsed_data.programs[p].nb_streams;
                    var pmt_pid = parsed_data.programs[p].pmt_pid;
                    var streams = [];

                    sources = 'ID:'+program_num;
                    for (s = 0; s < nb_streams; s++) {
                        var codec_name = parsed_data.programs[p].streams[s].codec_name;
                        var codec_type = parsed_data.programs[p].streams[s].codec_type;

                        console.log('codec: ', codec_name);
                        if (codec_type === "video") {
                            var width = parsed_data.programs[p].streams[s].width;
                            var height = parsed_data.programs[p].streams[s].height;
                            var bit_rate = parsed_data.programs[p].streams[s].bit_rate;
                            var framerate = parsed_data.programs[p].streams[s].avg_frame_rate;
                            var pid = parsed_data.programs[p].streams[s].id;

                            sources += ' '+codec_name+' @ '+width+'x'+height+' '+framerate+' fps ';
                            var service = new scan_response_video(s, codec_type, codec_name, width, height, framerate, bit_rate, pid);
                            streams.push(service);
                        } else if (codec_type === "audio") {
                            var bit_rate = parsed_data.programs[p].streams[s].bit_rate;
                            var channels = parsed_data.programs[p].streams[s].channels;
                            var pid = parsed_data.programs[p].streams[s].id;
                            var samplerate = parsed_data.programs[p].streams[s].sample_rate;
                            if (channels == 1) {
                                sources += '['+codec_name+' @ mono '+samplerate+'Hz] ';
                            } else if (channels == 2) {
                                sources += '['+codec_name+' @ stereo '+samplerate+'Hz] ';
                            } else {
                                sources += '['+codec_name+' @ 5.1 '+samplerate+'Hz] ';
                            }

                            var service = new scan_response_audio(s, codec_type, codec_name, channels, samplerate, bit_rate, pid);
                            streams.push(service);
                        } else {
                            // do nothing for now
                        }
                    }
                    descriptivedata.push(sources);
                    programdata.push(streams);
                }

                console.log("response: ", programdata);

                obj = new Object();
                obj.scan_result = programdata;
                var retdata = JSON.stringify(obj);

                var nextscan = scanFolder+'/'+address+'.json';
                fs.writeFile(nextscan, retdata, (err) => {
                    if (err) {
                        console.error(err);
                        return;
                    };
                    console.log('Scan file has been created: ', nextscan);
                });

                console.log("description: ", descriptivedata);

                obj2 = new Object();
                obj2.sources = descriptivedata;
                var retdata2 = JSON.stringify(obj2);
                var nextscan2 = scanFolder+'/'+address+'_simple.json';
                fs.writeFile(nextscan2, retdata2, (err) => {
                    if (err) {
                        console.error(err);
                        return;
                    };
                    console.log('Scan file has been created: ', nextscan2);
                });

                res.send(retdata);
            }
        });
    }
});

function listed_service(serviceindex, servicenum) {
    this.serviceindex = serviceindex;
    this.servicenum = servicenum;
}

app.get('/api/v1/list_services', (req, res) => {
    console.log('requested to list services');

    var files = fs.readdirSync(configFolder);
    var serviceindex = 0;
    var retdata;

    // send list of services and quick status in json format
    obj = new Object();

    var services = [];
    files.forEach(file => {
        if (getExtension(file) == '.json') {
            var fullfileConfig = configFolder+'/'+file;
            var fileprefix = path.basename(fullfileConfig, '.json');
            serviceindex++;
            var service = new listed_service(serviceindex, fileprefix);
            services.push(service);
        }
    })

    obj.service_list = services;
    retdata = JSON.stringify(obj);
    //console.log(retdata);
    res.send(retdata);
});

function output_stream(height, width, video_bitrate) {
    this.height = height;
    this.width = width;
    this.video_bitrate = video_bitrate;
}

function input_stream(ip, port, input_interface, bitrate) {
    this.ip = ip;
    this.port = port;
    this.input_interface = input_interface;
    this.bitrate = bitrate;
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
            var fullfileStatus = statusFolder+'/'+file;
            var fileprefix = path.basename(fullfileStatus, '.json');
            console.log('get_service_status: checking configindex ', configindex, ' looking for ', req.params.uid);
            if ((configindex == req.params.uid) || (fileprefix == req.params.uid)) {
                var fullfileStatus = statusFolder+'/'+file;
                var fullfileConfig = configFolder+'/'+file;
                var fileprefix = path.basename(fullfileStatus, '.json');
                var touchfile = statusFolder+'/'+fileprefix+'.lock';
                if (!fs.existsSync(touchfile)) {
                    if (fs.existsSync(fullfileStatus)) {
                        var configdata = fs.readFileSync(fullfileStatus, 'utf8');
                        if (configdata) {
                            //console.log(configdata);
                            var words = JSON.parse(configdata);
                            var uptime;

                            obj = new Object();
                            var retdata;
                            var current_output;
                            var current_input;

                            uptime = words.data.system.uptime;
                            obj.uptime = uptime;
                            if (words.data.system["transcoding"] == 0) {
                                obj.filletmode = "repackage";

                                var vstreams = [];
                                var astreams = [];
                                var input_interface;

                                for (current_input = 0; current_input < words.data.vstreams.length; current_input++) {
                                    var ip = words.data.vstreams[current_input]["source-ip"];
                                    var port = words.data.vstreams[current_input]["port"];
                                    var local_input_interface = words.data.vstreams[current_input]["interface"];
                                    var bitrate = words.data.vstreams[current_input]["bitrate"];
                                    var istream = new input_stream(ip, port, local_input_interface, bitrate);
                                    vstreams.push(istream);
                                }
                                obj.vstreams = vstreams;

                                for (current_input = 0; current_input < words.data.astreams.length; current_input++) {
                                    var ip = words.data.astreams[current_input]["source-ip"];
                                    var port = words.data.astreams[current_input]["port"];
                                    var local_input_interface = words.data.astreams[current_input]["interface"];
                                    var bitrate = words.data.astreams[current_input]["bitrate"];
                                    var istream = new input_stream(ip, port, local_input_interface, bitrate);
                                    astreams.push(istream);
                                }
                                obj.astreams = astreams;

                                obj.input_interface = local_input_interface;

                                obj.video_synchronizer_entries = words.data.system["video-synchronizer-entries"];
                                obj.audio_synchronizer_entries = words.data.system["audio-synchronizer-entries"];
                                obj.current_video_time = words.data.system["current-video-time"];
                                obj.current_audio_time = words.data.system["current-audio-time"];
                                obj.source_video_codec = words.data.system["source-video-codec"];
                                obj.source_audio_codec = words.data.system["source-audio-codec"];
                            } else {
                                obj.filletmode = "transcode";
                                obj.input_interface = words.data.source["interface"];
                                obj.source_ip = words.data.source.stream0["source-ip"];
                                obj.stream_select = words.data.source["stream-select"];
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
                                obj.gpu = words.data.system["gpu"];
                                obj.video_codec = words.data.system.codec;
                                obj.video_profile = words.data.system.profile;
                                obj.video_quality = words.data.system.quality;
                                obj.latency = words.data.system.latency;

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
                                obj.video_bitrate = words.data.source.stream0["video-bitrate"];
                                obj.video_frames = words.data.source.stream0["video-received-frames"];
                            }

                            obj.input_signal = words.data.system["input-signal"];

                            obj.window_size = words.data.system["window-size"];
                            obj.segment_length = words.data.system["segment-length"];
                            obj.hls_active = words.data.system["hls-active"];
                            obj.dash_active = words.data.system["dash-fmp4-active"];

                            obj.source_interruptions = words.data.system["source-interruptions"];
                            obj.source_errors = words.data.system["source-errors"];
                            obj.error_count = words.data.system.error_count;
                            obj.transcoding = words.data.system.transcoding;
                            obj.scte35 = words.data.system.scte35;

                            retdata = JSON.stringify(obj);

                            //console.log(retdata);

                            res.send(retdata);
                            sent = 1;
                            //return;
                        }
                    } else if (fs.existsSync(fullfileConfig)) {
                        var configdata = fs.readFileSync(fullfileConfig, 'utf8');
                        if (configdata) {
                            //console.log(configdata);
                            var words = JSON.parse(configdata);
                            var uptime;

                            obj = new Object();
                            var retdata;
                            var current_output;

                            uptime = -1;
                            obj.filletmode = words.filletmode;
                            obj.uptime = uptime;
                            obj.input_signal = 0;
                            if (words.filletmode == "transcode") {
                                obj.input_interface = words.inputinterface1;
                                obj.source_ip = words.ipaddr_primary;
                                obj.source_width = 0;
                                obj.source_height = 0;
                                obj.fpsnum = 0;
                                obj.fpsden = 0;
                                obj.aspectnum = 0;
                                obj.aspectden = 0;
                                obj.videomediatype = 0;
                                obj.audiomediatype0 = 0;
                                obj.audiomediatype1 = 0;
                                obj.audiochannelsinput0 = 0;
                                obj.audiochannelsinput1 = 0;
                                obj.audiochannelsoutput0 = 0;
                                obj.audiochannelsoutput1 = 0;
                                obj.audiosamplerate0 = 48000;
                                obj.audiosamplerate1 = 0;
                                obj.gpu = 0;
                                obj.window_size = words.windowsize;
                                obj.segment_length = words.segmentsize;
                                obj.hls_active = 0;//words.data.system["hls-active"];
                                obj.dash_active = 0;//words.data.system["dash-fmp4-active"];
                                obj.video_codec = 0;//words.data.system.codec;
                                obj.video_profile = 0;//words.data.system.profile;
                                obj.video_quality = 0;//words.data.system.quality;
                                obj.stream_select = 0;
                                obj.source_interruptions = 0;//words.data.system["source-interruptions"];
                                obj.source_errors = 0;//words.data.system["source-errors"];
                                obj.error_count = 0;//words.data.system.error_count;
                                obj.transcoding = 1;//words.data.system.transcoding;
                                obj.scte35 = 0;//words.data.system.scte35;
                                obj.video_bitrate = 0;//words.data.source.stream0["video-bitrate"];
                                obj.video_frames = 0;//words.data.source.stream0["video-received-frames"];
                                obj.outputs = 0;//words.data.output.outputs;
                            } else {
                                obj.input_interface = words.inputinterface;
                                obj.videosources = words.videosources;
                                obj.audiosources = words.audiosources;
                                obj.repackage_source_video1 = words.source_video1;
                                obj.repackage_source_video2 = words.source_video2;
                                obj.repackage_source_video3 = words.source_video3;
                                obj.repackage_source_video4 = words.source_video4;
                                obj.repackage_source_video5 = words.source_video5;
                                obj.repackage_source_video6 = words.source_video6;
                                obj.repackage_source_video7 = words.source_video7;
                                obj.repackage_source_video8 = words.source_video8;
                                obj.repackage_source_audio1 = words.source_audio1;
                                obj.repackage_source_audio2 = words.source_audio2;
                                obj.hls_active = words.enablehls;
                                obj.dash_active = words.enabledash;
                                obj.window_size = words.windowsize;
                                obj.segment_length = words.segmentsize;
                                obj.scte35 = words.enablescte35;
                                obj.outputs = 0;
                            }

                            /*
                            var streams = [];
                            for (current_output = 0; current_output < words.data.output.outputs; current_output++) {
                                var outputstream = 'stream'+current_output;
                                var height = words.data.output[outputstream]["output-height"];
                                var width = words.data.output[outputstream]["output-width"];
                                var video_bitrate = words.data.output[outputstream]["video-bitrate"];

                                var ostream = new output_stream(height, width, video_bitrate);
                                streams.push(ostream);
                            }
                            */

                            obj.output_streams = 0;//null;//streams;
                            retdata = JSON.stringify(obj);

                            //console.log(retdata);

                            res.status(200);
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
        //let's provide basic configuration information instead of just an empty status
        //that is the best we can do


        console.log('service was not found ', req.params.uid);
        res.sendStatus(404);  // service not found
    } else {
        //console.log('service was fine ', req.params.uid);
        //res.sendStatus(200);
    }
});
