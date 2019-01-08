// Solar Power Monitor
// Makes Arduino/M5Stack solar power monitor data available
// as a web service via an unauthenticated HTTP server.  
// Insecure!  Use only behind a firewall with a tunnelled 
// reverse proxy providing security for external access.

// Configuration options
var desc = "Solar Power Monitor";
var device = 0;
var verbosity = 1;  // default verbosity
var port = 9195;    // default port interface will be served on
var td_ttl_s = 600; // default TD time to live, in s
var td_ttl_refresh_ms = (td_ttl_s-100)*1000; // default refresh interval, in ms

// Dependencies
var os = require('os');
var hostname = os.hostname();
var fs = require('fs');
var uuidv5 = require('uuid/v5');
var EventEmitter = require('events').EventEmitter;
var http = require('http');
var child_process = require('child_process');
var request = require('request');
var program = require('commander');

program
  .option('-d, --device <required>',"device number, defaults to "+device)
  .option('-t, --tdir <required>',"thing directory url")
  .option('-v, --verbose [verbosity]',"how verbose (0-5)", 5)
  .option('-p, --port <required>',"port to serve on, defaults to "+port)
  .parse(process.argv);

if (program.device) {
  device = parseInt(program.device);
}
if (program.port) {
  port = parseInt(program.port);
}
var register_TD = false; // whether to register TD
var tdir = undefined;
if (program.tdir) {
  register_TD = true;
  tdir = program.tdir;
}
if (program.verbose) {
  verbosity = parseInt(program.verbose);
}

// Options
var opt = {
   "tdirs": [tdir], 
   "protocol": "http",
   "device": device,
   "name": hostname + '.local',
   "port": port
};
opt.base = opt.protocol 
         + '://' 
         + opt.name 
         + ':' 
         + opt.port  
         + '/api';
opt.uuid = uuidv5(opt.base, uuidv5.URL);

function serveLocalFile(res,path,contentType,responseCode) {
    if (!responseCode) responseCode = 200;
    fs.readFile(__dirname + path, function(err,data) {
        if (err) {
            res.writeHead(500,{'Content-Type': 'text/plain'});
            res.end('500 - Internal Error');
        } else {
            res.writeHead(responseCode,{'Content-Type': contentType});
            res.end(data);
        }
    });
}

function genTD(error,success) {
    var path = "/TD.template";
    fs.readFile(__dirname + path, function(err,template_data) {
        if (err) {
            error();
        } else {
            var data = template_data.toString();

            data = data.replace(/{{{protocol}}}/gi,opt.protocol);
            data = data.replace(/{{{name}}}/gi,opt.name);
            data = data.replace(/{{{device}}}/gi,opt.device);
            data = data.replace(/{{{base}}}/gi,opt.base);
            data = data.replace(/{{{uuid}}}/gi,opt.uuid);

            success(data);
        }
    });
}

function serveTD(res) {
    var contentType = "application/json";
    var responseCode = 200;
    genTD(
        function () {
            res.writeHead(500,{'Content-Type': 'text/plain'});
            res.end('500 - Internal Error');
        },
        function (data) {
            res.writeHead(responseCode,{'Content-Type': contentType});
            res.end(data);
        }
    );
}

// Register TDs
var td_resource = [];
function regTD() {
    genTD(
        function () {
            if (verbosity > 0) console.log('Error generating TD for registration');
        },
        function (td) {
            for (let j=0; j<opt.tdirs.length; j++) {
               let tdir = opt.tdirs[j];
               if (verbosity > 1) console.log('Register TD',i,'to',tdir);
               request(
                  {
                      "url": tdir + '?lt='+td_ttl_s,
                      "method": 'POST',
                      "headers": {"Content-Type": "application/ld+json"},
                      "body": td.toString()
                  },
                  function (error, response, body) {
                      if (verbosity > 2) console.log('Registration response:',JSON.stringify(response));
                      if (error || !response.statusCode || Math.trunc(response.statusCode/100) != 2) {
                          if (verbosity > 0) console.log('Error registering TD',error,response.statusCode);
                      } else {
                          if (verbosity > 3) console.log(response.headers);
                          //td_resource[i] = response.headers.location.toString();
                          if (verbosity > 3) console.log("TD id:",JSON.parse(td).id);
                            td_resource[i] = JSON.parse(td).id;
                          if (verbosity > 2) console.log(response.statusCode +
                            ': TD '+i+' registered to "'+td_resource[i]+'"');
                      }
                  }
               );
            }
        }
    );
}

// Operations
/*
function camera_init() {
    child_process.execSync(script_init+' '+camera_index);
}

function camera_delete(frame) {
    child_process.exec('rm '+frame,(error,stdout,stderr) => {
        if (error && verbosity > 0) {
            console.log('problem deleting frame',frame);
                console.log('stdout:',stdout);
                console.log('stderr:',stderr);
        }
    });
}

// Cropped Frames
var cropped_frame_basename = 
        (use_ramdisk ? ramdisk : __dirname+"/camera/") 
        + "cr_frame_";
var cropped_idnum = 0;
function camera_crop(params,frame,done) {
    cropped_idnum += 1;
    var cropped_frame = cropped_frame_basename + camera_index + '_' + cropped_idnum + '.jpg';
    var cmd = 
        script_crop_frame
        +' ' + params.xo
        +' ' + params.yo
        +' ' + params.x
        +' ' + params.y
        +' ' + frame
        +' ' + cropped_frame;
    if (verbosity > 2) console.log("executing",cmd);
    child_process.exec(cmd,(error,stdout,stderr) => {
        done(error,cropped_frame);
    });
}

// Frames
var frame_basename = 
        (use_ramdisk ? ramdisk : __dirname+"/camera/") 
        + "frame_";
var frames = [];
var max_frames = 4; 
var current_frame = undefined;
var frameEvent = new EventEmitter();

function camera_grabFrame(idnum,done) {
    if (verbosity > 3) {
        console.log("frames: "+frames);
        console.log("current frame: "+current_frame);
    }
    var frame_name = frame_basename + camera_index + '_' + idnum + '.jpg';
    var script_cmd = script_grab_frame+' '+camera_index +' '+frame_name;
    if (verbosity > 2) console.log("exec: "+script_cmd);
    child_process.exec(script_cmd,
    //child_process.execSync(script_cmd,
      (error,stdout,stderr) => {
        if (verbosity > 3) console.log("grabbed from camera "+camera_index+" to ",frame_name);
        if (!error) {
            current_frame = frame_name;
            frameEvent.emit('newframe',frame_name);
            frames.push(frame_name);
            if (frames.length > max_frames) {
                var old_frame = frames.shift();
                if (verbosity > 3) console.log("deleting "+old_frame);
                camera_delete(old_frame);
            }
        } else {
           if (verbosity > 0) console.log("error in grab from camera "+camera_index+" to ",frame_name);
        }
        done(error,stdout,stderr);
    });
}

function camera_observeFrame(done) {
    var callback = function(frame) {
        frameEvent.removeListener('newframe',callback);
        done(0,frame);
    }
    frameEvent.addListener('newframe',callback);
}

// Properties
for (let pname in camera_info) {
    if (camera_info.hasOwnProperty(pname)) {
        camera_info[pname].obs = new EventEmitter();
        camera_info[pname].old = undefined; 
        camera_info[pname].cur = undefined; 
    }
}

function camera_setIntegerProperty(pname,value,done) {
    child_process.exec(camera_info[pname].set+' '+camera_index+' '+value,
      (error,stdout,stderr) => {
        if (error && verbosity > 0) {
            console.error('exec error:',error);
            console.log('stderr:',stderr);
            done(error,stdout,stderr);
        }
        camera_info[pname].old = camera_info[pname].cur;
        camera_info[pname].cur = value;
        if (undefined === camera_info[pname].old || 
            camera_info[pname].old !== camera_info[pname].cur) {
            camera_info[pname].obs.emit('change in '+pname+' to '+value);
        }
        done(error,stdout,stderr);
    });
}

function camera_getIntegerProperty(pname,done) {
    child_process.exec(camera_info[pname].get+' '+camera_index, 
      (error, stdout, stderr) => {
        if (error && verbosity > 0) {
            console.error('exec error:',error);
            console.log('stderr:',stderr);
            done(error);
        }
        let value = Number(stdout.toString());
        camera_info[pname].old = camera_info[pname].cur;
        camera_info[pname].cur = value;
        if (undefined === camera_info[pname].old || 
            camera_info[pname].old !== camera_info[pname].cur) {
            camera_info[pname].obs.emit('change in '+pname,value);
        }
        done(0,value);
    });
}

function camera_observeIntegerProperty(pname,done) {
    var callback = function(value) {
        camera_info[pname].obs.removeListener('change in '+pname,callback);
        done(0,value);
    }
    camera_info[pname].obs.addListener('change in '+pname,callback);
}
*/

// Process JSON Body Parameters
function processBodyParams(req,res,done) {
    var stringData = '';
    req.on('data', function(data) {
        stringData += data;
        // handle jerks trying to fill up memory and crash our server
        if (stringData.length > 1e6) {
            stringData = '';
            res.writeHead(413, {'Content-Type': 'text/plain'}).end();
            req.connection.destroy();
        }
    });
    // parse data (encoded as JSON)
    req.on('end', function() {
        try {
            done(JSON.parse(stringData));
        } catch (error) {
            res.writeHead(405,{'Content-Type': 'text/plain'});
            res.end('Malformed POST parameters');
        }
    });
}

function handleIntegerProperty(res,path,method,pname) {
    switch (method) {
        case 'GET':
            camera_getIntegerProperty(pname,(error,value) => {
                if (error) {
                    res.writeHead(500,{'Content-Type': 'text/plain'});
                    res.end('Internal error - could not read '+pname);
                    if (verbosity > 2) console.log("Error: "+method+" on "+path);
                } else {
                    res.writeHead(200,{'Content-Type': 'application/json'});
                    res.end(JSON.stringify(value));
                    if (verbosity > 2) console.log("Success: "+method+" on "+path);
                }
            });
            break;
        case 'POST':
        case 'PUT':
            processBodyParams(req,res,(value) => {
                camera_setIntegerProperty(pname,value,(error,stdout,stderr) => {
                    if (error) {
                            res.writeHead(500,{'Content-Type': 'text/plain'});
                            res.end('Internal error - could not set '+pname);
                            if (verbosity > 2) console.log("Error: "+method+" on "+path);
                    } else {
                            res.writeHead(200,{'Content-Type': 'text/plain'});
                            res.end('Camera['+camera_index+'].'+pname+' = '+value);
                            if (verbosity > 2) console.log("Success: "+method+" on "+path);
                    }
                });
            });
            break;
        default:
            res.writeHead(405,{'Content-Type': 'text/plain'});
            res.end('Method '+method+' not supported on '+path+'\n');
            if (verbosity > 2) console.log("Error: "+method+" on "+path);
    }
}

function observeIntegerProperty(res,path,method,pname) {
    switch (method) {
        case 'GET':
            if (verbosity > 1) console.log("OBSERVE...");
            camera_observeIntegerProperty(pname,function (error,value) {
                if (error) {
                    res.writeHead(500,{'Content-Type': 'text/plain'});
                    res.end('Internal error - could not read '+pname);
                    if (verbosity > 2) console.log("Error: "+method+" on "+path);
                } else {
                    if (verbosity > 1) console.log("...DONE");
                    res.writeHead(200,{'Content-Type': 'application/json'});
                    res.end(JSON.stringify(value));
                    if (verbosity > 2) console.log("Success: "+method+" on "+path);
                }
            });
            break;
        default:
            res.writeHead(405,{'Content-Type': 'text/plain'});
            res.end('Method '+method+' not supported on '+path+'\n');
            if (verbosity > 2) console.log("Error: "+method+" on "+path);
    }
}

// Server for all API entry points
function server(req,res,opt) {
    var path = req.url.replace(/\/?(?:\?.*)?$/,'').toLowerCase();
    var method = req.method;
    if (verbosity > 2) console.log("Request: "+method+" on "+path);
    switch (path) {
        case '': {
            switch (method) {
                case 'GET':
                    res.writeHead(200,{'Content-Type': 'text/plain'});
                    res.end('Simple Web Camera\n');
                    if (verbosity > 2) console.log("Success: "+method+" on "+path);
                    break;
                default:
                    res.writeHead(405,{'Content-Type': 'text/plain'});
                    res.end('Method '+method+' not supported on '+path+'\n');
                    if (verbosity > 2) console.log("Error: "+method+" on "+path);
            }
            break;
        }
        case '/desc': {
            switch (method) {
                case 'GET':
                    serveLocalFile(res,'/public/DESC.md','text/markdown');
                    if (verbosity > 2) console.log("Success: "+method+" on "+path);
                    break;
                default:
                    res.writeHead(405,{'Content-Type': 'text/plain'});
                    res.end('Method '+method+' not supported on '+path+'\n');
                    if (verbosity > 2) console.log("Error: "+method+" on "+path);
            }
            break;
        }
        case '/api': {
            switch (method) {
                case 'GET':
                    serveTD(res);
                    if (verbosity > 2) console.log("Success: "+method+" on "+path);
                    break;
                default:
                    res.writeHead(405,{'Content-Type': 'text/plain'});
                    res.end('Method '+method+' not supported on '+path+'\n');
                    if (verbosity > 2) console.log("Error: "+method+" on "+path);
            }
            break;
        }
        case '/api/panel': 
            handleProperty(res,path,method,"0");
            break;
        case '/api/charge': 
            handleProperty(res,path,method,"1");
            break;
        case '/api/output': 
            handleProperty(res,path,method,"2");
            break;
        case '/api/environment': 
            handleProperty(res,path,method,"e");
            break;
        case '/api/status': 
            handleProperty(res,path,method,"s");
            break;
        default: {
            res.writeHead(404,{'Content-Type': 'text/plain'});
            res.end(path+' not found\n');
            if (verbosity > 2) console.log("Error: "+method+" on "+path);
            break;
        }
    }
}

// Start server
function start_server() {
    http.createServer(function(req,res) {
        server(req,res,opt);
    }).listen(opt.port, function () {
        if (verbosity > 0) 
            console.log(desc,'via HTTP started on port',opt.port);
    });
}

start_server();

if (register_TD) {
    regTD();
    setInterval(regTD,td_ttl_refresh_ms);
}
