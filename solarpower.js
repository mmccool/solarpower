// Solar Power Monitor
// Makes Arduino/M5Stack solar power monitor data available
// as a web service via an unauthenticated HTTP server.  
// Insecure!  Use only behind a firewall with a tunnelled 
// reverse proxy providing security for external access.

// Configuration options
var desc = "Solar Power Monitor";
var device = 0;     // specifies tty as "/dev/ttyUSB"+device
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


// Process JSON Body Parameters
function processBodyParams(req,res,done) {
    var stringData = '';
    req.on('data', function(data) {
        stringData += data;
        // drop connections from jerks trying to fill up memory and crash our server
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

// Device Interface
var device_properties;
var device_interval;
const SerialPort = require('serialport');
const device_port = new SerialPort('/dev/ttyUSB'+device, {
  baudRate: 115200,
  autoOpen: false
});
device_port.on('error', function(err) {
  console.log('Serial Port Error: ', err.message)
});
device_port.open(function (err) {
  if (err) {
    console.log('Error opening Serial Port: ', err.message)
  }
});
/*
device_port.on('open', function(err) {
  if (err) {
    console.log('Serial Port Error: ', err.message)
  } else {
    // periodically ask for all properties
    device_interval = setInterval(function() {
      device_port.write("a;");
      // note: errors will be handled by the above event
    }, 5000);
  }
});
*/
/*
const Delimiter = require('@serialport/parser-delimiter');
const device_parser = device_port.pipe(new Delimiter({ delimiter: ';' }));
device_parser.on('data', function(buffer) {
  console.log("DEVICE RECORD:");
  console.log(buffer);
}); // emits record after every ';'
*/

// Poll device periodically, grab all parameters

function device_getProperty(pname,done) {
  done(0,"get "+pname);
}
function device_setIntegerProperty(pname,value,done) {
  done(0,"(success)");
}
function device_observeIntegerProperty(pname,done) {
  done(0,"observe "+pname); 
}

function handleProperty(res,path,method,pname) {
    switch (method) {
        case 'GET':
            device_getProperty(pname,(error,value) => {
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
                device_setProperty(pname,value,(error,msg) => {
                    if (error) {
                            res.writeHead(500,{'Content-Type': 'text/plain'});
                            res.end('Internal error - could not set '+pname);
                            res.end(' msg: '+msg);
                            if (verbosity > 2) console.log("Error: "+method+" on "+path);
                    } else {
                            res.writeHead(200,{'Content-Type': 'text/plain'});
                            res.end('Device['+device_index+'].'+pname+' = '+value);
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
            device_observeIntegerProperty(pname,function (error,value) {
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
                    res.end(desc+'\n');
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
        case '/api/c0': 
            handleProperty(res,path,method,"c0");
            break;
        case '/api/charge': 
        case '/api/c1': 
            handleProperty(res,path,method,"c1");
            break;
        case '/api/output': 
        case '/api/c2': 
            handleProperty(res,path,method,"c2");
            break;
        case '/api/environment': 
        case '/api/e': 
            handleProperty(res,path,method,"e");
            break;
        case '/api/status': 
        case '/api/s': 
            handleProperty(res,path,method,"s");
            break;
        case '/api/dispmode': 
        case '/api/d': 
            handleProperty(res,path,method,"d");
            break;
        case '/api/period': 
        case '/api/y': 
            handleProperty(res,path,method,"y");
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
