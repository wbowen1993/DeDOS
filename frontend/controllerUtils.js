
var net = require('net');

var parseUtils = require('./parseUtils');
var log = require('./logger');
var config = require('./config');
var ssh = require(`./sshUtils`);
var zmq = require('zmq');

/** Whether the connection to the controller has been established */
var connected = false;
/** Whether the controller object has been initialized */
var controller_init = false;
/** The socket connection to the controller to receive DFG */
var dfg_socket;
/** The socket connection to the controller to send control commands */
var ctl_socket;



/** IP address of controller */
var controller_ip;

/** Callback for errors */
var error_cb;
/** Callback for closing connection */
var close_cb;

/** resets initialization flags */
var uninit = function() {
    controller_init = false;
    connected = false;
    dfg_socket.unmonitor();
}

/** Object to hold exported functions */
var controller = {};

/**
 * Initializes controller SSH object and callbacks
 * @param ip IP address of controller to connect to
 * @param on_dfg(dfg) function to call when DFG is received from controller
 * @param on_error function to call if global controller errors out
 * @param on_close function to call when connection to gc is closed
 */
controller.init = function(ip, on_dfg, on_error, on_close) {
    if (controller_init) {
        log.warn(`Overwriting initialized Controller at ${controller_ip}`);
    }
    controller_ip = ip;

    ssh.add_connection("ctl", ip);

    var receive_buffer = '';
    var error_cnt = 0;

    dfg_socket = zmq.socket('sub');
    // On receipt of data from the controller
    dfg_socket.on('message', function(topic, message) {
        connected = true;
        // Add to the received buffer, and attempt to parse
        receive_buffer += message;
        parseUtils.parseDfg(receive_buffer).then(
            // If parsing works, call the dfg callback
            (dfg) => {
                receive_buffer = '';
                on_dfg(dfg);
                error_cnt = 0;
            },
            // Otherwise add to the received buffer
            // (this really shouldn't be a problem since switching to zmq)
            (err) => {
                error_cnt+=1;
                if (error_cnt > 5) {
                    log.error("Can't recover from bad JSON");
                    receive_buffer = '';
                    error_cnt = 0;
                }
            }
        );
    });

    ctl_socket = zmq.socket('req');

    ctl_socket.on('data', function(data) {
        log.note(`Received ${data} from control socket`);
    });

    var conn_str = `tcp://${controller_ip}:${config.gc_sock_port}`
    dfg_socket.connect(conn_str);
    dfg_socket.subscribe("");
    log.info(`Listening for controller at ${conn_str}`);

    error_cb = on_error;
    close_cb = on_close;

    dfg_socket.on('error', when_errored);
    dfg_socket.on('close', when_closed);


    // Try to connect to the control socket, but no worries if it can't happen
    ctl_socket.on('error', (e) => {
        log.warn(`Error on controller ctl socket: ${e}`);
    });
    try {
        var conn_str =`tcp://${controller_ip}:${config.gc_ctl_port}`;
        ctl_socket.connect(conn_str);
        log.info(`Attempted connect to ${conn_str}`);
    } catch(e) {
        log.warn(`Failed to connect to controller ctl socket ${e}`);
    }
}

/** When the connection to the controller closes */
var when_closed = function() {
    log.warn(`Connection to controller at ${controller_ip}:${config.gc_sock_port} was closed`);
    uninit();
    close_cb();
}

/** When the connection to the controller encounters an error */
var when_errored = function(err) {
    log.error(`Connection to controller errored:: ${err}`);
    uninit();
    error_cb(err);
}

/** Closes the conncetion to the global controller */
controller.close_connection = function() {
    if (!controller_init) {
        log.warn("Controller not initialized. Cannot close");
        return;
    }
    dfg_socket.destroy();
    uninit();
}

controller.is_initialized = function() {
    return controller_init;
}

controller.is_connected = function() {
    return connected;
}

/**
 * Starts an initialized global controller
 * @param json_file DFG file to initialize controller with
 * @return Promise Resolves if attempt to start controller succeeded, otherwise rejects
 */
controller.start = function(json_file, on_stop) {
    dfg_socket.monitor(2000, 1);
    return new Promise( (resolve, reject) => {
        ssh.start("ctl", config.controller_cmd(json_file), () => {
                log.warn("Controller stopped");
                uninit();
                if (on_stop) 
                    on_stop();
            }).then(
            (output) => {
                log.info(`Controller started with line: ${output}`);
                resolve();
            },
            (err) => {
                log.error(`Could not start controller: ${err}`);
                reject(err);
            }
        );
    });
}

/**
 * Stops the global controller
 * @return Promise Resolves if command to kill the controller succeeded
 */
controller.kill = function() {
    dfg_socket.monitor(2000, 1);
    return new Promise((resolve, reject) => {
        ssh.kill("ctl", config.gc_exec).then( () => {
            log.info("Killed controller");
        }, (err) => {
            log.warn(`Error killing controller: ${err}`);
        });
    });
}

controller.send_cmd = function(cmd) {
    return new Promise((resolve, reject) => {
        ctl_socket.send(`${cmd}`, null, () => {
            log.debug(`Wrote: '${cmd}'`);
            resolve(cmd);
        });
    });
}

controller.clone_msu = function(msu_id) {
    return new Promise((resolve, reject) => {
        this.send_cmd(`clone msu ${msu_id}`).then(
            () => {resolve(msu_id);},
            () => {reject(msu_id);});
    });
}

controller.unclone_msu = function(msu_id) {
    return new Promise((resolve, reject) => {
        this.send_cmd(`unclone msu ${msu_id}`).then(
            () => {resolve(msu_id);},
            () => {reject(msu_id)});
    });
}

module.exports = controller;