const zmq = require('zeromq');
const sock = zmq.socket('pull');
sock.connect('tcp://127.0.0.1:5556');
sock.on('message', (data) => {
    console.log(JSON.parse(data));
});