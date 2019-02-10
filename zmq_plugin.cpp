#include <eosio/zmq_plugin/zmq_plugin.hpp>

namespace {

    const char *PUB_OPT = "zmq-enable-pub-socket";
    const char *PUSH_OPT = "zmq-enable-push-socket";

    const char *PUSH_BIND_OPT = "zmq-sender-bind";
    const char *PUSH_BIND_DEFAULT = "tcp://127.0.0.1:5556";

    const char *PUBLISHER_BIND_OPT = "zmq-publisher-bind";
    const char *PUBLISHER_BIND_DEFAULT = "tcp://127.0.0.1:5557";

    const char *WHITELIST_OPT = "zmq-whitelist-account";
    const char *WHITELIST_FILE_OPT = "zmq-whitelist-accounts-file";

    const char *BLACKLIST_OPT = "zmq-action-blacklist";

    const char *BLOOM_FILTER_OPT = "zmq-use-bloom-filter";

    const char *SEND_TRANSACTIONS_OPT = "zmq-enable-transactions";
    const char *SEND_ACTIONS_OPT = "zmq-enable-actions";

    const std::string MSGTYPE_ACTION_TRACE = "action_trace";
    const std::string MSGTYPE_TRANSACTION_TRACE = "transaction_trace";
    const std::string MSGTYPE_IRREVERSIBLE_BLOCK = "irreversible_block";
    const std::string MSGTYPE_FORK = "fork";
    const std::string MSGTYPE_ACCEPTED_BLOCK = "accepted_block";
    const std::string MSGTYPE_FAILED_TX = "failed_tx";
}

namespace zmqplugin {
    using namespace eosio;
    using namespace eosio::chain;

    struct zmq_action_object {
        uint64_t                       global_action_seq;
        block_num_type                 block_num;
        chain::block_timestamp_type    block_time;
        fc::variant                    action_trace;
        uint32_t                       last_irreversible_block;
        string                         deserialization_time;
    };

    struct zmq_irreversible_block_object {
        block_num_type               irreversible_block_num;
        digest_type                  irreversible_block_digest;
    };

    struct zmq_fork_block_object {
        block_num_type                    invalid_block_num;
    };

    struct zmq_accepted_block_object {
        block_num_type               accepted_block_num;
        block_timestamp_type         accepted_block_timestamp;
        account_name                 accepted_block_producer;
        digest_type                  accepted_block_digest;
    };

    struct zmq_failed_transaction_object {
        string                                         trx_id;
        block_num_type                                 block_num;
        eosio::chain::transaction_receipt::status_enum status_name;
        uint8_t                                        status_int;
    };
}


namespace eosio {
    using namespace chain;
    using namespace zmqplugin;
    using namespace std::chrono;
    using boost::signals2::scoped_connection;

    static appbase::abstract_plugin &_zmq_plugin = app().register_plugin<zmq_plugin>();

    class zmq_plugin_impl {
    public:
        zmq::context_t context;

        zmq::socket_t push_socket;
        zmq::socket_t pub_socket;

        string push_bind_str;
        string pub_bind_str;

        chain_plugin          *chain_plugin = nullptr;
        fc::microseconds       abi_serializer_max_time;
        std::map<name, std::set<name>>  blacklist_actions;
        std::map<transaction_id_type, transaction_trace_ptr> cached_traces;

        uint32_t                _end_block = 0;
        bool                    use_whitelist = false;
        bool                    use_bloom = false;
        bool                    send_trx = false;
        bool                    send_actions = false;

        bool                    enable_publisher = false;
        bool                    enable_push = false;

        fc::bloom_filter        *whitelist_accounts_bf = NULL;
        flat_set<account_name>  whitelisted_accounts;

        fc::optional<scoped_connection> applied_transaction_connection;
        fc::optional<scoped_connection> accepted_block_connection;
        fc::optional<scoped_connection> irreversible_block_connection;

        zmq_plugin_impl():
            context(1),
            push_socket(context, ZMQ_PUSH),
            pub_socket(context, ZMQ_PUB) {
            // Add eosio::onblock by default to the blacklist
            blacklist_actions.emplace(
                std::make_pair(
                    chain::config::system_account_name,
                    std::set<name> {N(onblock)}
                )
            );
        }

        void send_msg( const string content, const string msgtype) {

            string part1 = "{\"type\":\"";
            string part2 = "\",\"" + msgtype + "\":";
            string part3 = "}";
            string result = part1 + msgtype + part2 + content + part3;

            // Send to non-blocking publisher socker (fan out)
            if(enable_publisher) {
                string topic = msgtype + "-";
                string result_with_topic = topic + result;
                zmq::message_t message_pub(result_with_topic.length());
                unsigned char *ptr_pub = (unsigned char *) message_pub.data();
                memcpy(ptr_pub, result_with_topic.c_str(), result_with_topic.length());
                pub_socket.send(message_pub);
            }

            // Send to blocking push socker (round-robin)
            if(enable_push) {
                zmq::message_t message(result.length());
                unsigned char *ptr = (unsigned char *) message.data();
                memcpy(ptr, result.c_str(), result.length());
                push_socket.send(message);
            }
        }


        void on_applied_transaction( const transaction_trace_ptr &p ) {
            if (p->receipt) {
                cached_traces[p->id] = p;
            }
        }


        void on_accepted_block(const block_state_ptr &block_state) {

            auto block_num = block_state->block->block_num();
            auto &chain = chain_plugin->chain();

            if ( _end_block >= block_num ) {
                // report a fork. All traces sent with higher block number are invalid.
                zmq_fork_block_object zfbo;
                zfbo.invalid_block_num = block_num;
                send_msg(fc::json::to_string(zfbo), MSGTYPE_FORK);
            }

            _end_block = block_num;

            {
                zmq_accepted_block_object zabo;
                zabo.accepted_block_num = block_num;
                zabo.accepted_block_timestamp = block_state->block->timestamp;
                zabo.accepted_block_producer = block_state->header.producer;
                zabo.accepted_block_digest = block_state->block->digest();
                send_msg(fc::json::to_string(zabo), MSGTYPE_ACCEPTED_BLOCK);
            }

            if( send_trx || send_actions ) {

                for (auto &r : block_state->block->transactions) {
                    transaction_id_type id;
                    if (r.trx.contains<transaction_id_type>()) {
                        id = r.trx.get<transaction_id_type>();
                    } else {
                        id = r.trx.get<packed_transaction>().id();
                    }

                    if( r.status == transaction_receipt_header::executed ) {

                        // Send traces only for executed transactions
                        auto it = cached_traces.find(id);
                        if (it == cached_traces.end() || !it->second->receipt) {
                            ilog("missing trace for transaction {id}", ("id", id));
                            continue;
                        }

                        // send full transaction
                        if(send_trx) {
                            auto v = chain.to_variant_with_abi(r, abi_serializer_max_time);
                            string trx_json = fc::json::to_string(v);
                            send_msg(trx_json, MSGTYPE_TRANSACTION_TRACE);
                        }

                        if(send_actions) {
                            for( const auto &atrace : it->second->action_traces ) {
                                on_action_trace(atrace, block_state);
                            }
                        }

                    } else {
                        // Notify about a failed transaction
                        zmq_failed_transaction_object zfto;
                        zfto.trx_id = id.str();
                        zfto.block_num = block_num;
                        zfto.status_name = r.status;
                        zfto.status_int = static_cast<uint8_t>(r.status);
                        send_msg(fc::json::to_string(zfto), MSGTYPE_FAILED_TX);
                    }
                }
            }

            cached_traces.clear();
        }


        void on_action_trace( const action_trace &at, const block_state_ptr &block_state) {

            if(use_whitelist) {

                if(use_bloom) {
                    if(!whitelist_accounts_bf->contains(at.act.account)) {
                        return;
                    }
                } else {
                    if(whitelisted_accounts.count(at.act.account) == 0 ) {
                        return;
                    }
                }
            }

            auto search_acc = blacklist_actions.find(at.act.account);
            if(search_acc != blacklist_actions.end()) {
                if( search_acc->second.count(at.act.name) != 0 ) {
                    return;
                }
            }

            auto &chain = chain_plugin->chain();
            zmq_action_object zao;
            zao.global_action_seq = at.receipt.global_sequence;
            zao.block_num = block_state->block->block_num();
            zao.block_time = block_state->block->timestamp;
            
            // Measure deserialization time
            high_resolution_clock::time_point t1 = high_resolution_clock::now();
            zao.action_trace = chain.to_variant_with_abi(at, abi_serializer_max_time);
            high_resolution_clock::time_point t2 = high_resolution_clock::now();

            duration<double> time_span = duration_cast<duration<double>>(t2 - t1);

            zao.deserialization_time = std::to_string(time_span.count()) + "s";
            zao.last_irreversible_block = chain.last_irreversible_block_num();
            send_msg(fc::json::to_string(zao), MSGTYPE_ACTION_TRACE);
        }


        void on_irreversible_block( const chain::block_state_ptr &bs ) {
            zmq_irreversible_block_object zibo;
            zibo.irreversible_block_num = bs->block->block_num();
            zibo.irreversible_block_digest = bs->block->digest();
            send_msg(fc::json::to_string(zibo), MSGTYPE_IRREVERSIBLE_BLOCK);
        }

    };


    zmq_plugin::zmq_plugin(): my(new zmq_plugin_impl()) {}
    zmq_plugin::~zmq_plugin() {}

    void zmq_plugin::set_program_options(options_description &, options_description &cfg) {
        cfg.add_options()
        (PUSH_OPT, bpo::bool_switch()->default_value(false), "Enable ZMQ PUSH socket")
        (PUB_OPT, bpo::bool_switch()->default_value(false), "Enable ZMQ PUB socket")
        (PUSH_BIND_OPT, bpo::value<string>()->default_value(PUSH_BIND_DEFAULT), "ZMQ PUSH Socket binding - blocking")
        (PUBLISHER_BIND_OPT, bpo::value<string>()->default_value(PUBLISHER_BIND_DEFAULT), "ZMQ PUB Socket binding - non-blocking")
        (BLOOM_FILTER_OPT, bpo::bool_switch()->default_value(false), "Use bloom filter for whitelisting")
        (SEND_TRANSACTIONS_OPT, bpo::bool_switch()->default_value(false), "Enable transactions output")
        (SEND_ACTIONS_OPT, bpo::bool_switch()->default_value(false), "Enable actions output")
        (WHITELIST_FILE_OPT, bpo::value<string>(), "ZMQ Whitelisted accounts from file (may specify only a single time)")
        (BLACKLIST_OPT, bpo::value<vector<string>>()->composing()->multitoken(), "Action (in the form code::action) added to zmq action blacklist (may specify multiple times)")
        (WHITELIST_OPT, bpo::value<vector<string>>()->composing()->multitoken(), "ZMQ plugin whitelist of accounts to track");
    }

    void zmq_plugin::plugin_initialize(const variables_map &options) {

        try {
            const auto &_http_plugin = app().get_plugin<http_plugin>();
            if( !_http_plugin.is_on_loopback()) {
                wlog( "\n"
                      "**********SECURITY WARNING**********\n"
                      "*                                  *\n"
                      "* --       ZMQ PLUGIN API       -- *\n"
                      "* - EXPOSED to the LOCAL NETWORK - *\n"
                      "* - USE ONLY ON SECURE NETWORKS! - *\n"
                      "*                                  *\n"
                      "************************************\n" );

            }
        }
        FC_LOG_AND_RETHROW()


        my->push_bind_str = options.at(PUSH_BIND_OPT).as<string>();
        my->pub_bind_str = options.at(PUBLISHER_BIND_OPT).as<string>();

        if (my->push_bind_str.empty() && my->pub_bind_str.empty()) {
            wlog("zmq-sender-bind nor zmq-publisher-bind not specified => eosio::zmq_plugin disabled.");
            return;
        }

        my->enable_publisher = options.at(PUB_OPT).as<bool>();
        my->enable_push =      options.at(PUSH_OPT).as<bool>();

        my->use_bloom = options.at(BLOOM_FILTER_OPT).as<bool>();
        my->send_trx = options.at(SEND_TRANSACTIONS_OPT).as<bool>();
        my->send_actions = options.at(SEND_ACTIONS_OPT).as<bool>();

        if( (options.count(WHITELIST_OPT) > 0) || options.count(WHITELIST_FILE_OPT) ) {

            std::string currentActor;
            std::ifstream infile;
            flat_set<account_name> accounts;

            // add from file
            if(options.count(WHITELIST_FILE_OPT)) {
                const std::string &whitelist_file_name = options[WHITELIST_FILE_OPT].as<std::string>();
                EOS_ASSERT(fc::exists(whitelist_file_name), plugin_config_exception, "whitelist file does not exist");
                infile = std::ifstream(whitelist_file_name, (std::ios::in));
                while(std::getline(infile, currentActor)) {
                    // TODO: validate account_name
                    accounts.insert(account_name(currentActor));
                }
            }

            // add from config file
            if(options.count(WHITELIST_OPT) > 0) {
                auto whl = options.at(WHITELIST_OPT).as<vector<string>>();
                for( auto &whlname : whl ) {
                    accounts.insert(account_name(whlname));
                }
            }

            ilog("Whitelist size = ${a}", ("a", accounts.size()));

            bloom_parameters *p = new bloom_parameters();
            p->projected_element_count = 100000;
            p->false_positive_probability = 1.0 / p->projected_element_count;
            p->compute_optimal_parameters();

            fc::bloom_filter *bf = new fc::bloom_filter(*p);

            for(auto &acc : accounts) {
                bf->insert(acc);
                ilog("${a} added to the whitelist", ("a", acc));
            }

            my->whitelist_accounts_bf = bf;
            my->whitelisted_accounts = accounts;
            my->use_whitelist = true;
        }


        if( options.count(BLACKLIST_OPT)) {
            const vector<string> &acts = options[BLACKLIST_OPT].as<vector<string>>();
            auto &list = my->blacklist_actions;
            for( const auto &a : acts ) {
                auto pos = a.find( "::" );
                EOS_ASSERT( pos != string::npos, plugin_config_exception, "Invalid entry in zmq-action-blacklist: '${a}'", ("a", a));
                account_name code( a.substr( 0, pos ));
                account_name act( a.substr( pos + 2 ));
                list.emplace(make_pair( N(code.value), std::set<account_name> { act } ));
            }
        }

        if(options.count(PUSH_BIND_OPT)) {
            ilog("Binding to ZMQ PUSH socket ${u}", ("u", my->push_bind_str));
            my->push_socket.bind(my->push_bind_str);
        }

        if(options.count(PUBLISHER_BIND_OPT)) {
            ilog("Binding to ZMQ PUB socket ${u}", ("u", my->pub_bind_str));
            my->pub_socket.bind(my->pub_bind_str);
        }

        my->chain_plugin = app().find_plugin<chain_plugin>();
        my->abi_serializer_max_time = my->chain_plugin->get_abi_serializer_max_time();

        auto &chain = my->chain_plugin->chain();

        my->applied_transaction_connection.emplace
        ( chain.applied_transaction.connect( [&]( const transaction_trace_ptr & p ) {
            my->on_applied_transaction(p);
        }));

        my->accepted_block_connection.emplace
        ( chain.accepted_block.connect([&](const block_state_ptr & p) {
            my->on_accepted_block(p);
        }));

        my->irreversible_block_connection.emplace
        ( chain.irreversible_block.connect( [&]( const chain::block_state_ptr & bs ) {
            my->on_irreversible_block( bs );
        } ));
    }


    std::string zmq_plugin::get_whitelisted_accounts() const {
        flat_set<account_name> list = my->whitelisted_accounts;
        return fc::json::to_string(list);
    }

    void zmq_plugin::set_whitelisted_accounts(const flat_set<account_name> &input) {
        my->use_whitelist = input.size() != 0;
        my->whitelisted_accounts = input;
    }

    void zmq_plugin::push_whitelisted_accounts(const flat_set<account_name> &input) {
        my->use_whitelist = input.size() != 0;
        my->whitelisted_accounts = input;
    }


    void zmq_plugin::plugin_startup() {

        auto &zmq = app().get_plugin<zmq_plugin>();

        app().get_plugin<http_plugin>().add_api({
            {
                "/v1/zmq/get_whitelist",
                [&zmq](string, string body, url_response_callback cb) mutable {
                    try {
                        if (body.empty()) body = "{}";
                        cb(200, zmq.get_whitelisted_accounts());
                    } catch (...) {
                        http_plugin::handle_exception("zmq", "get_whitelist", body, cb);
                    }
                }
            },
            {
                "/v1/zmq/set_whitelist",
                [&zmq](string, string body, url_response_callback cb) mutable {
                    try {
                        if (body.empty()) body = "{}";
                        zmq.set_whitelisted_accounts(fc::json::from_string(body).as<flat_set<account_name>>());
                        cb(200, "OK");
                    } catch (...) {
                        http_plugin::handle_exception("zmq", "set_whitelist", body, cb);
                    }
                }
            },
            {
                "/v1/zmq/push_whitelist",
                [&zmq](string, string body, url_response_callback cb) mutable {
                    try {
                        if (body.empty()) body = "{}";
                        zmq.push_whitelisted_accounts(fc::json::from_string(body).as<flat_set<account_name>>());
                        cb(200, "OK");
                    } catch (...) {
                        http_plugin::handle_exception("zmq", "set_whitelist", body, cb);
                    }
                }
            }
        });

    }

    void zmq_plugin::plugin_shutdown() {

        if( ! my->push_bind_str.empty() ) {
            my->push_socket.disconnect(my->push_bind_str);
            my->push_socket.close();
        }

        if( ! my->pub_bind_str.empty() ) {
            my->pub_socket.disconnect(my->pub_bind_str);
            my->pub_socket.close();
        }
    }

#undef INVOKE_R_V
#undef INVOKE_V_R
#undef CALL
}

FC_REFLECT( zmqplugin::zmq_action_object,
            (global_action_seq)
            (block_num)
            (block_time)
            (action_trace)
            (last_irreversible_block)
            (deserialization_time)
          )

FC_REFLECT( zmqplugin::zmq_irreversible_block_object,
            (irreversible_block_num)
            (irreversible_block_digest)
          )

FC_REFLECT( zmqplugin::zmq_fork_block_object,
            (invalid_block_num)
          )

FC_REFLECT(zmqplugin::zmq_accepted_block_object,
           (accepted_block_num)
           (accepted_block_timestamp)
           (accepted_block_producer)
           (accepted_block_digest)
          )

FC_REFLECT( zmqplugin::zmq_failed_transaction_object,
            (trx_id)
            (block_num)
            (status_name)
            (status_int)
          )
