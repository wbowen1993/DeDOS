import yaml
import traceback
from dedos_analytic.database.db_api import DbApi
import feature_engineering as dfe
from ..logger import *
from .classifiers import DbScanner
from .dedos_ctl import DedosCtl
from sklearn.cluster import DBSCAN
import json
import threading
import zmq

zctx = zmq.Context.instance()

class DaemonMessage():

    TRAIN=1
    CLASSIFY=2
    CONFIGURE=3
    SHUTDOWN=4
    SEND=5
    ACT=6

    def __init__(self, type, **params):
        self.type = type
        self.params = params
        for k, v in params.items():
            setattr(self, k, v)

    @classmethod
    def deserialize(cls, serialized):
        return cls(**json.loads(serialized))

    @property
    def serialized(self):
        return json.dumps(dict(type=self.type, **self.params))

    @classmethod
    def train_message(cls, start=0, end=None, name='_MAIN'):
        return DaemonMessage(cls.TRAIN, start=start, end=end, name=name)

    @classmethod
    def classify_message(cls, start, end=None, name='_MAIN'):
        return DaemonMessage(cls.CLASSIFY, start=start, end=end, name=name)

    @classmethod
    def configure_message(cls, config, name='_MAIN'):
        return DaemonMessage(cls.CONFIGURE, config=config, name=name)

    @classmethod
    def send_message(cls, message):
        return DaemonMessage(cls.SEND, message=message)

    @classmethod
    def act_message(cls, name='_MAIN'):
        return DaemonMessage(cls.ACT, name=name)

    @classmethod
    def shutdown_message(cls):
        return DaemonMessage(cls.SHUTDOWN)



class _Daemon():

    SOCKLOC = 'ipc:///tmp/DEDOS_DAEMON'

    def __init__(self, config=None, interval = -1, logdir=None):
        self.dbscanners = {}
        self.dedos_ctl = None
        self.logdir = logdir
        self.interval = interval
        self.rep_socket = None
        self.req_socket = None
        self.is_running = False
        self.config = config
        self.last_classification = None

    def set_config(self, config):
        self.config = config
        self.init_dbscanner(config)
        if self.dedos_ctl is not None:
            self.dedos_ctl.stop_dfg_monitor()
        self.dedos_ctl = DedosCtl(config)
        self.dedos_ctl.start_dfg_monitor()

    def init_dbscanner(self, config, name='_MAIN'):
        self.dbscanners[name] = DbScanner(config, log_dir=self.logdir)

    def send_message(self, daemon_msg):
        if self.req_socket is None:
            log_error("Cannot send to daemon before connecting to daemon")
            return
        log_debug("Sending to daemon: {}".format(daemon_msg.serialized))
        self.req_socket.send(daemon_msg.serialized)
        out = self.req_socket.recv(0)
        log_info("Received from daemon: {}".format(out))

    def process_train_msg(self, msg):
        if msg.name not in self.dbscanners:
            log_error("Scanner {} not initialized".format(msg.name))
            return "ERROR"

        self.dbscanners[msg.name].train(msg.start, msg.end)
        return "SUCCESS"

    def process_classify_msg(self, msg):
        if msg.name not in self.dbscanners:
            log_error("Scanner {} not initialized".format(msg.name))
            return "ERROR"

        if not self.dbscanners[msg.name].is_trained:
            log_error("Scanner {} not trained".format(msg.name))
            return "ERROR"

        self.last_classification = self.dbscanners[msg.name].classify(msg.start, msg.end)
        return "SUCCESS"

    def process_send_msg(self, msg):
        if self.dedos_ctl is None:
            log_error("Cannot send to dedos before connecting")
            return "ERROR"
        self.dedos_ctl.send_message(msg.message)
        return "SUCCESS"

    def process_cfg_msg(self, msg):
        if msg.name in self.dbscanners:
            log_error("Scanner {} already initialized".format(msg.name))
        self.init_dbscanner(msg.config, msg.name)
        self.set_config(msg.config)
        return "SUCCESS"

    def process_shutdown_msg(self, msg):
        self.is_running = False
        return "SHUTTING DOWN"

    def process_act_msg(self, msg):
        classification = self.last_classification
        if classification is None:
            log_error("Cannot act before classifying")
            return "ERROR"
        actions = self.dedos_ctl.act(classification)
        self.last_classification = None
        return "SUCCESS {}".format(actions)


    MSG_HANDLERS = {
            DaemonMessage.TRAIN: process_train_msg,
            DaemonMessage.CLASSIFY: process_classify_msg,
            DaemonMessage.CONFIGURE: process_cfg_msg,
            DaemonMessage.SHUTDOWN: process_shutdown_msg,
            DaemonMessage.SEND: process_send_msg,
            DaemonMessage.ACT: process_act_msg
    }

    def _process_message(self, smsg):
        msg = DaemonMessage.deserialize(smsg)
        if msg.type not in self.MSG_HANDLERS:
            log_error("Unknown message type {} received!".format(msg.type))
            return
        return self.MSG_HANDLERS[msg.type](self, msg)

    def _loop_iteration(self, timeout=1000):
        if self.rep_socket is None:
            log_error("Socket not instantiated! Attempting to instantiate now")
            self.rep_socket = ztcx.socket(zmq.REP)
            self.rep_socket.bind(self.SOCKLOC)
            return

        poller = zmq.Poller()
        poller.register(self.rep_socket, zmq.POLLIN)
        rtn = poller.poll(timeout)

        if len(rtn) == 0:
            return

        smsg = self.rep_socket.recv(0)

        try:
            rtn = self._process_message(smsg)
            self.rep_socket.send(rtn)
        except Exception as e:
            log_error(traceback.format_exc())
            self.rep_socket.send(str(e))


    def _loop(self):
        log_info("Daemon started!")
        self.is_running = True
        while self.is_running:
            self._loop_iteration()

    def connect(self):
        if self.req_socket is None:
            self.req_socket = zctx.socket(zmq.REQ)
            self.req_socket.connect(self.SOCKLOC)

    def start(self):
        self.rep_socket = zctx.socket(zmq.REP)
        self.rep_socket.bind(self.SOCKLOC)
        self._loop()

    def stop(self):
        self.is_running = False
        if self.dedos_ctl is not None:
            self.dedos_ctl.stop_dfg_monitor()

    def run(self):

        if self.is_running:
            log_warn("OH NO! Daemon already running!?")

        threading.Thread(target=self.start).start()

        try:
            self.connect()
        except Exception as e:
            log_error("Error connecting to socket: {}".format(e))

daemon = _Daemon()
