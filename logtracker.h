/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __LOGTRACKER_H__
#define __LOGTRACKER_H__

#include "config.h"

#include <memory>

#include "globalregistry.h"
#include "kis_mutex.h"
#include "trackedelement.h"
#include "kis_net_microhttpd.h"
#include "devicetracker_component.h"
#include "streamtracker.h"

class KisLogfileBuilder;
typedef std::shared_ptr<KisLogfileBuilder> SharedLogBuilder;

class KisLogfile;
typedef std::shared_ptr<KisLogfile> SharedLogfile;

// Logfile builders are responsible for telling the logging tracker what sort of 
// log we are, the type and default name, if we're a singleton log that can't have multiple
// simultaneous instances, how to actually instantiate the log, and various other
// attributes
class KisLogfileBuilder : public tracker_component {
public:
    KisLogfileBuilder() :
        tracker_component() {
        register_fields();
        reserve_fields(NULL);
        set_local_name("kismet.log.type_driver");
        initialize();
    }

    KisLogfileBuilder(int in_id) :
        tracker_component(in_id) {
        register_fields();
        reserve_fields(NULL);
        set_local_name("kismet.log.type_driver");
        initialize();
    }

    KisLogfileBuilder(int in_id, std::shared_ptr<tracker_element_map> e) :
        tracker_component(in_id) {
        register_fields();
        reserve_fields(e);
        set_local_name("kismet.log.type_driver");
        initialize();
    }

    virtual ~KisLogfileBuilder() { };

    virtual uint32_t get_signature() const override {
        return adler32_checksum("KisLogfileBuilder");
    }

    virtual std::unique_ptr<tracker_element> clone_type() override {
        using this_t = std::remove_pointer<decltype(this)>::type;
        auto dup = std::unique_ptr<this_t>(new this_t());
        return std::move(dup);
    }

    virtual std::unique_ptr<tracker_element> clone_type(int in_id) override {
        using this_t = std::remove_pointer<decltype(this)>::type;
        auto dup = std::unique_ptr<this_t>(new this_t(in_id));
        return std::move(dup);
    }

    // Take a shared_ptr reference to ourselves from the caller, because we can't 
    // consistently get a universal shared_ptr to 'this'
    virtual SharedLogfile build_logfile(SharedLogBuilder) {
        return NULL;
    }

    virtual void initialize() { };

    __Proxy(log_class, std::string, std::string, std::string, log_class);
    __Proxy(log_name, std::string, std::string, std::string, log_name);
    __Proxy(stream, uint8_t, bool, bool, stream_log);
    __Proxy(singleton, uint8_t, bool, bool, singleton);
    __Proxy(log_description, std::string, std::string, std::string, description);

protected:
    virtual void register_fields() override {
        tracker_component::register_fields();

        register_field("kismet.logfile.type.class", "class/type", &log_class);
        register_field("kismet.logfile.type.name", "base type name", &log_name);
        register_field("kismet.logfile.type.stream", "continual streaming", &stream_log);
        register_field("kismet.logfile.type.singleton", 
                "single-instance of log type permitted", &singleton);
        register_field("kismet.logfile.type.description", "base description", &description);
    }

    std::shared_ptr<tracker_element_string> log_class;
    std::shared_ptr<tracker_element_string> log_name;
    std::shared_ptr<tracker_element_uint8> stream_log;
    std::shared_ptr<tracker_element_uint8> singleton;
    std::shared_ptr<tracker_element_string> description;
};

// Logfiles written to disk can be 'block' logs (like the device log), or they can be
// streaming logs (like gps or pcapng streams); 
class KisLogfile : public tracker_component, public streaming_agent {
public:
    KisLogfile() :
        tracker_component() {
        register_fields();
        reserve_fields(NULL);
    }

    KisLogfile(int in_id) :
        tracker_component(in_id) {
        register_fields();
        reserve_fields(NULL);
    }

    KisLogfile(int in_id, std::shared_ptr<tracker_element_map> e) :
        tracker_component(in_id) {
        register_fields();
        reserve_fields(e);
    }

    KisLogfile(SharedLogBuilder in_builder) :
        tracker_component() {
        register_fields();
        reserve_fields(NULL);
        builder = in_builder;

        if (builder != nullptr)
            insert(builder);

        uuid luuid;
        luuid.GenerateRandomTimeUUID();
        set_int_log_uuid(luuid);
    }

    virtual ~KisLogfile() { 
        local_locker l(&log_mutex);

        if (builder != NULL && builder->get_stream()) {
            std::shared_ptr<StreamTracker> streamtracker = 
                Globalreg::fetch_mandatory_global-as<StreamTracker>("STREAMTRACKER");

            streamtracker->remove_streamer(get_stream_id());
        }
    }

    virtual uint32_t get_signature() const override {
        return adler32_checksum("KisLogfile");
    }

    virtual std::unique_ptr<tracker_element> clone_type() override {
        using this_t = std::remove_pointer<decltype(this)>::type;
        auto dup = std::unique_ptr<this_t>(new this_t());
        return std::move(dup);
    }

    virtual std::unique_ptr<tracker_element> clone_type(int in_id) override {
        using this_t = std::remove_pointer<decltype(this)>::type;
        auto dup = std::unique_ptr<this_t>(new this_t(in_id));
        return std::move(dup);
    }

    virtual bool Log_Open(std::string in_path) { 
        local_locker lock(&log_mutex);

        set_int_log_path(in_path);
        set_int_log_open(false);

        return false; 
    }

    virtual void Log_Close() { 
        local_locker lock(&log_mutex);

        set_int_log_open(false);
    }

    __ProxyPrivSplit(log_uuid, uuid, uuid, uuid, log_uuid);
    __ProxyTrackable(builder, KisLogfileBuilder, builder);
    __ProxyPrivSplit(log_path, std::string, std::string, std::string, log_path);
    __ProxyPrivSplit(log_open, uint8_t, bool, bool, log_open);
    __ProxyPrivSplit(log_desc, std::string, std::string, std::string, log_description);

protected:
    virtual void register_fields() override {
        tracker_component::register_fields();

        register_field("kismet.logfile.uuid", "unique log id", &log_uuid);
        register_field("kismet.logfile.description", "log description", &log_description);
        register_field("kismet.logfile.path", "filesystem path to log", &log_path);
        register_field("kismet.logfile.open", "log is currently open", &log_open);

    }

    // Builder/prototype that made us
    SharedLogBuilder builder;

    kis_recursive_timed_mutex log_mutex;

    std::shared_ptr<tracker_element_uuid> log_uuid;
    std::shared_ptr<tracker_element_string> log_description;
    std::shared_ptr<tracker_element_string> log_path;
    std::shared_ptr<tracker_element_uint8> log_open;
};

class LogTracker : public tracker_component, public kis_net_httpd_cppstream_handler, 
    public lifetime_global, public deferred_startup {
public:
    static std::string global_name() { return "LOGTRACKER"; }

    static std::shared_ptr<LogTracker> create_logtracker() {
        std::shared_ptr<LogTracker> mon(new LogTracker());
        Globalreg::globalreg->register_lifetime_global(mon);
        Globalreg::globalreg->register_deferred_global(mon);
        Globalreg::globalreg->insert_global(global_name(), mon);
        return mon;
    }

    // HTTP API
    virtual bool httpd_verify_path(const char *path, const char *method) override;

    virtual void httpd_create_stream_response(kis_net_httpd *httpd,
            kis_net_httpd_connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size, std::stringstream &stream) override;
    virtual int httpd_post_complete(kis_net_httpd_connection *concls) override;

    virtual void trigger_deferred_startup() override;
    virtual void trigger_deferred_shutdown() override;

    // Register a log type
    int register_log(SharedLogBuilder in_builder);

    // Open a log
    SharedLogfile open_log(std::string in_class);
    SharedLogfile open_log(SharedLogBuilder in_builder);
    SharedLogfile open_log(std::string in_class, std::string in_title);
    SharedLogfile open_log(SharedLogBuilder in_builder, std::string in_title);

    // Close a log
    int close_log(SharedLogfile in_logfile);

    static void Usage(const char *argv0);
private:
    LogTracker();

public:
    virtual ~LogTracker();

    __ProxyPrivSplit(logging_enabled, uint8_t, bool, bool, logging_enabled);
    __ProxyPrivSplit(log_title, std::string, std::string, std::string, log_title);
    __ProxyPrivSplit(log_prefix, std::string, std::string, std::string, log_prefix);
    __ProxyPrivSplit(log_template, std::string, std::string, std::string, log_template);

protected:
    virtual void register_fields() override;
    virtual void reserve_fields(std::shared_ptr<tracker_element_map> e) override;

    kis_recursive_timed_mutex tracker_mutex;

    std::shared_ptr<StreamTracker> streamtracker;

    // Vector of prototypes
    std::shared_ptr<tracker_element_vector> logproto_vec;
    int logproto_entry_id;

    // Vector of logs
    std::shared_ptr<tracker_element_vector> logfile_vec;
    int logfile_entry_id;

    // Various global config items common to all
    std::shared_ptr<tracker_element_uint8> logging_enabled;
    std::shared_ptr<tracker_element_string> log_title;
    std::shared_ptr<tracker_element_string> log_prefix;
    std::shared_ptr<tracker_element_string> log_template;

    std::shared_ptr<tracker_element_vector> log_types_vec;
};

#endif
    

