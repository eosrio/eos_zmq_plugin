#pragma once

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

    private:
        std::unique_ptr<class zmq_plugin_impl> my;
    };

}
