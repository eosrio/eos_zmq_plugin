# zmq_plugin

Lighter version of the [eosio zmq_plugin](https://github.com/cc32d9/eos_zmq_plugin)

## Configuration

The following configuration statements in `config.ini` are recognized:

* `plugin = eosio::zmq_plugin` -- enables the ZMQ plugin

* `zmq-action-blacklist = CODE::ACTION` -- filter out a specific action

Action `onblock` in `eosio` account is ignored and is not producing a
ZMQ event. These actions are generated every 0.5s, and ignored in order
to save the CPU resource.

* `zmq-sender-bind = ENDPOINT` -- specifies the PUSH socket binding
endpoint. Default value: `tcp://127.0.0.1:5556`.

#### Whitelist options

* `zmq-whitelist-account-file` -- whilelist accounts from a file (one account name per line)

* `zmq-whitelist-account = ACCOUNT` -- sets up a whitelist, so that only traces for specified accounts are exported. Multiple options define multiple accounts to trace. If the account is a contract, all its actions (including inline actions) are exported. Also all transfers to and from the account, system actions, and third-party notifications are triggering the trace export. Whitelisted accounts will override the blacklist.



## Compiling

Clone the zmq_plugin repo:
```bash
apt-get install -y pkg-config libzmq5-dev
mkdir ${HOME}/build
cd ${HOME}/build/
git clone https://github.com/eosrio/eos_zmq_plugin.git
```

Change to the eos repository folder and run:
```bash
LOCAL_CMAKE_FLAGS="-DEOSIO_ADDITIONAL_PLUGINS=${HOME}/build/eos_zmq_plugin" ./eosio_build.sh
```


### Original Plugin Author

* cc32d9 <cc32d9@gmail.com>
* https://github.com/cc32d9
* https://medium.com/@cc32d9