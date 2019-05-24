# ØMQ Nodeos Plugin

Lighter version of the [eosio zmq_plugin](https://github.com/cc32d9/eos_zmq_plugin)

## Configuration

The following configuration statements in `config.ini` are recognized:

Make sure zmq is loaded after the chain plugin.
```
plugin = eosio::chain_plugin
plugin = eosio::zmq_plugin
```

* `zmq-enable-actions = true/false` -- enable action output (default: false)

* `zmq-enable-transactions = true/false` -- enable transactions output (default: false)

* `zmq-action-blacklist = CODE::ACTION` -- filter out a specific action

Action `onblock` in `eosio` account is ignored and is not producing a
ZMQ event. These actions are generated every 0.5s, and ignored in order
to save the CPU resource.

### Socket options

Both socket types are disabled by default, you need to explicitly enable them

#### PUSH/PULL

* `zmq-enable-push-socket = true/false` -- enables sending messages on push/pull pattern (blocking)
	- If multiple clients are connected, messages will be distributed in a round-robin fashion to the clients

* `zmq-sender-bind = ENDPOINT`
	- specifies the PUSH socket binding endpoint.
	- Default value: `tcp://127.0.0.1:5556`.

#### PUB/SUB

* `zmq-enable-pub-socket = true/false` --  enables sending messages on pub/sub pattern (non-blocking)
	- In the case multiple subscribers are connected all messages will be sent to all subscribers

* `zmq-publisher-bind = ENDPOINT`
	- specifies the PUSH socket binding endpoint.
	- Default value: `tcp://127.0.0.1:5557`.

#### Whitelist options

* `zmq-whitelist-accounts-file = FILENAME` -- whilelist accounts from a file (one account name per line)

* `zmq-whitelist-account = ACCOUNT` -- sets up a whitelist, so that only traces for specified accounts are exported. Multiple options define multiple accounts to trace. If the account is a contract, all its actions (including inline actions) are exported. Also all transfers to and from the account, system actions, and third-party notifications are triggering the trace export. Whitelisted accounts will override the blacklist.

* `zmq-use-bloom-filter` -- WARNING: experimental bloom filter, optimized for large lists.

#### Whitelist HTTP API (hot-reload, no need to restart nodeos)

Example to set a new whitelist
```bash
curl -sL -X POST -d '["eosio.token","eosio","eosio.forum"]' http://127.0.0.1:8888/v1/zmq/set_whitelist
```

Get the current whitelist with
```bash
curl -sL http://127.0.0.1:8888/v1/zmq/get_whitelist
```

## Compiling

Clone the zmq_plugin repo:
```bash
apt-get install -y pkg-config libzmq5-dev libnorm-dev
mkdir ${HOME}/build
cd ${HOME}/build/
git clone https://github.com/eosrio/eos_zmq_plugin.git
```

Change to the eos repository folder and run:
```bash
LOCAL_CMAKE_FLAGS="-DEOSIO_ADDITIONAL_PLUGINS=${HOME}/build/eos_zmq_plugin" ./scripts/eosio_build.sh
```

## Reading data from the socket

This sample code uses the [zeromq](https://github.com/zeromq/zeromq.js/) prebuilt bindings for Node.js.

```bash
cd reader
npm install
node reader.js
```

### Original Plugin Author

* cc32d9 <cc32d9@gmail.com>
* https://github.com/cc32d9
* https://medium.com/@cc32d9
