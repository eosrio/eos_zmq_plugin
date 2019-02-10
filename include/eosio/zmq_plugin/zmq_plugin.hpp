#pragma once


#include <string>
#include <zmq.hpp>

#include <fc/variant.hpp>
#include <fc/io/json.hpp>
#include <fc/bloom_filter.hpp>

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <chrono>

#include <appbase/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>

namespace eosio {

    using namespace appbase;

    class zmq_plugin : public appbase::plugin<zmq_plugin> {
    public:
        zmq_plugin();
        virtual ~zmq_plugin();

        APPBASE_PLUGIN_REQUIRES((http_plugin))
        virtual void set_program_options(options_description &, options_description &cfg) override;

        void plugin_initialize(const variables_map &options);
        void plugin_startup();
        void plugin_shutdown();
        std::string get_whitelisted_accounts() const;
        void set_whitelisted_accounts(const fc::flat_set<account_name> &input);
        void push_whitelisted_accounts(const fc::flat_set<account_name> &input);

    private:
        std::unique_ptr<class zmq_plugin_impl> my;
    };

}
