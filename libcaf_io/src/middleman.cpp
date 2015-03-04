/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include <tuple>
#include <cerrno>
#include <memory>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "caf/on.hpp"
#include "caf/actor.hpp"
#include "caf/config.hpp"
#include "caf/node_id.hpp"
#include "caf/announce.hpp"
#include "caf/to_string.hpp"
#include "caf/actor_proxy.hpp"
#include "caf/scoped_actor.hpp"
#include "caf/uniform_type_info.hpp"

#include "caf/io/middleman.hpp"
#include "caf/io/basp_broker.hpp"
#include "caf/io/system_messages.hpp"

#include "caf/detail/logging.hpp"
#include "caf/detail/ripemd_160.hpp"
#include "caf/detail/safe_equal.hpp"
#include "caf/detail/singletons.hpp"
#include "caf/detail/make_counted.hpp"
#include "caf/detail/get_root_uuid.hpp"
#include "caf/detail/actor_registry.hpp"
#include "caf/detail/get_mac_addresses.hpp"

#ifdef CAF_WINDOWS
# include <io.h>
# include <fcntl.h>
#endif

namespace caf {
namespace io {

namespace {

template <class Subtype>
inline void serialize_impl(const handle<Subtype>& hdl, serializer* sink) {
  sink->write_value(hdl.id());
}

template <class Subtype>
inline void deserialize_impl(handle<Subtype>& hdl, deserializer* source) {
  hdl.set_id(source->read<int64_t>());
}

inline void serialize_impl(const new_connection_msg& msg, serializer* sink) {
  serialize_impl(msg.source, sink);
  serialize_impl(msg.handle, sink);
}

inline void deserialize_impl(new_connection_msg& msg, deserializer* source) {
  deserialize_impl(msg.source, source);
  deserialize_impl(msg.handle, source);
}

inline void serialize_impl(const new_data_msg& msg, serializer* sink) {
  serialize_impl(msg.handle, sink);
  auto buf_size = static_cast<uint32_t>(msg.buf.size());
  if (buf_size != msg.buf.size()) { // narrowing error
    std::ostringstream oss;
    oss << "attempted to send more than "
        << std::numeric_limits<uint32_t>::max() << " bytes";
    auto errstr = oss.str();
    CAF_LOGF_INFO(errstr);
    throw std::ios_base::failure(std::move(errstr));
  }
  sink->write_value(buf_size);
  sink->write_raw(msg.buf.size(), msg.buf.data());
}

inline void deserialize_impl(new_data_msg& msg, deserializer* source) {
  deserialize_impl(msg.handle, source);
  auto buf_size = source->read<uint32_t>();
  msg.buf.resize(buf_size);
  source->read_raw(msg.buf.size(), msg.buf.data());
}

// connection_closed_msg & acceptor_closed_msg have the same fields
template <class T>
typename std::enable_if<std::is_same<T, connection_closed_msg>::value
                        || std::is_same<T, acceptor_closed_msg>::value>::type
serialize_impl(const T& dm, serializer* sink) {
  serialize_impl(dm.handle, sink);
}

// connection_closed_msg & acceptor_closed_msg have the same fields
template <class T>
typename std::enable_if<std::is_same<T, connection_closed_msg>::value
                        || std::is_same<T, acceptor_closed_msg>::value>::type
deserialize_impl(T& dm, deserializer* source) {
  deserialize_impl(dm.handle, source);
}

template <class T>
class uti_impl : public detail::abstract_uniform_type_info<T> {
 public:
  using super = detail::abstract_uniform_type_info<T>;

  uti_impl(const char* tname) : super(tname) {
    // nop
  }

  void serialize(const void* instance, serializer* sink) const {
    serialize_impl(super::deref(instance), sink);
  }

  void deserialize(void* instance, deserializer* source) const {
    deserialize_impl(super::deref(instance), source);
  }
};

template <class T>
void do_announce(const char* tname) {
  announce(typeid(T), uniform_type_info_ptr{new uti_impl<T>(tname)});
}

} // namespace <anonymous>

using detail::make_counted;

using middleman_actor_base = middleman_actor::extend<
                               reacts_to<ok_atom, int64_t>,
                               reacts_to<ok_atom, int64_t, actor_addr>,
                               reacts_to<error_atom, int64_t, std::string>
                             >::type;

class middleman_actor_impl : public middleman_actor_base::base {
 public:
  middleman_actor_impl(middleman& mref, actor default_broker)
      : m_broker(default_broker),
        m_parent(mref),
        m_next_request_id(0) {
    // nop
  }

  ~middleman_actor_impl();

  void on_exit() {
    CAF_LOG_TRACE("");
    m_pending_gets.clear();
    m_pending_deletes.clear();
    m_broker = invalid_actor;
  }

  using get_op_result = either<ok_atom, actor_addr>
                        ::or_else<error_atom, std::string>;

  using get_op_promise = typed_response_promise<get_op_result>;

  using del_op_result = either<ok_atom>::or_else<error_atom, std::string>;

  using del_op_promise = typed_response_promise<del_op_result>;

  using map_type = std::map<int64_t, response_promise>;

  middleman_actor_base::behavior_type make_behavior() {
    return {
      [=](put_atom, const actor_addr& whom, uint16_t port,
          const std::string& addr, bool reuse_addr) {
        return put(whom, port, addr.c_str(), reuse_addr);
      },
      [=](put_atom, const actor_addr& whom, uint16_t port,
          const std::string& addr) {
        return put(whom, port, addr.c_str());
      },
      [=](put_atom, const actor_addr& whom, uint16_t port, bool reuse_addr) {
        return put(whom, port, nullptr, reuse_addr);
      },
      [=](put_atom, const actor_addr& whom, uint16_t port) {
        return put(whom, port);
      },
      [=](get_atom, const std::string& hostname, uint16_t port,
          std::set<std::string>& expected_ifs) {
        return get(hostname, port, std::move(expected_ifs));
      },
      [=](get_atom, const std::string& hostname, uint16_t port) {
        return get(hostname, port, std::set<std::string>());
      },
      [=](delete_atom, const actor_addr& whom) {
        return del(whom);
      },
      [=](delete_atom, const actor_addr& whom, uint16_t port) {
        return del(whom, port);
      },
      [=](ok_atom, int64_t request_id) {
        // not legal for get results
        CAF_ASSERT(m_pending_gets.count(request_id) == 0);
        handle_ok<del_op_result>(m_pending_deletes, request_id);
      },
      [=](ok_atom, int64_t request_id, actor_addr& result) {
        // not legal for delete results
        CAF_ASSERT(m_pending_deletes.count(request_id) == 0);
        handle_ok<get_op_result>(m_pending_gets, request_id, std::move(result));
      },
      [=](error_atom, int64_t request_id, std::string& reason) {
        handle_error(request_id, reason);
      }
    };
  }

 private:
  either<ok_atom, uint16_t>::or_else<error_atom, std::string>
  put(const actor_addr& whom, uint16_t port,
      const char* in = nullptr, bool reuse_addr = false) {
    CAF_LOG_TRACE(CAF_TSARG(whom) << ", " << CAF_ARG(port)
                  << ", " << CAF_ARG(reuse_addr));
    accept_handle hdl;
    uint16_t actual_port;
    try {
      // treat empty strings like nullptr
      if (in != nullptr && in[0] == '\0') {
        in = nullptr;
      }
      auto res = m_parent.backend().new_tcp_doorman(port, in, reuse_addr);
      hdl = res.first;
      actual_port = res.second;
    }
    catch (bind_failure& err) {
      return {error_atom::value, std::string("bind_failure: ") + err.what()};
    }
    catch (network_error& err) {
      return {error_atom::value, std::string("network_error: ") + err.what()};
    }
    send(m_broker, put_atom::value, hdl, whom, actual_port);
    return {ok_atom::value, actual_port};
  }

  get_op_promise get(const std::string& hostname, uint16_t port,
                     std::set<std::string> expected_ifs) {
    CAF_LOG_TRACE(CAF_ARG(hostname) << ", " << CAF_ARG(port));
    auto result = make_response_promise();
    try {
      auto hdl = m_parent.backend().new_tcp_scribe(hostname, port);
      auto req_id = m_next_request_id++;
      send(m_broker, get_atom::value, hdl, req_id,
           actor{this}, std::move(expected_ifs));
      m_pending_gets.insert(std::make_pair(req_id, result));
    }
    catch (network_error& err) {
      // fullfil promise immediately
      std::string msg = "network_error: ";
      msg += err.what();
      result.deliver(get_op_result{error_atom::value, std::move(msg)}.value);
    }
    return result;
  }

  del_op_promise del(const actor_addr& whom, uint16_t port = 0) {
    CAF_LOG_TRACE(CAF_TSARG(whom) << ", " << CAF_ARG(port));
    auto result = make_response_promise();
    auto req_id = m_next_request_id++;
    send(m_broker, delete_atom::value, req_id, whom, port);
    m_pending_deletes.insert(std::make_pair(req_id, result));
    return result;
  }

  template <class T, class... Vs>
  void handle_ok(map_type& storage, int64_t request_id, Vs&&... vs) {
    CAF_LOG_TRACE(CAF_ARG(request_id));
    auto i = storage.find(request_id);
    if (i == storage.end()) {
      CAF_LOG_ERROR("request id not found: " << request_id);
      return;
    }
    i->second.deliver(T{ok_atom::value, std::forward<Vs>(vs)...}.value);
    storage.erase(i);
  }

  template <class F>
  bool finalize_request(map_type& storage, int64_t req_id, F fun) {
    CAF_LOG_TRACE(CAF_ARG(req_id));
    auto i = storage.find(req_id);
    if (i == storage.end()) {
      CAF_LOG_INFO("request ID not found in storage");
      return false;
    }
    fun(i->second);
    storage.erase(i);
    return true;
  }

  void handle_error(int64_t request_id, std::string& reason) {
    CAF_LOG_TRACE(CAF_ARG(request_id) << ", " << CAF_ARG(reason));
    auto fget = [&](response_promise& rp) {
      rp.deliver(get_op_result{error_atom::value, std::move(reason)}.value);
    };
    auto fdel = [&](response_promise& rp) {
      rp.deliver(del_op_result{error_atom::value, std::move(reason)}.value);
    };
    if (!finalize_request(m_pending_gets, request_id, fget)
        && !finalize_request(m_pending_deletes, request_id, fdel)) {
      CAF_LOG_ERROR("invalid request id: " << request_id);
    }
  }

  actor m_broker;
  middleman& m_parent;
  int64_t m_next_request_id;
  map_type m_pending_gets;
  map_type m_pending_deletes;
};

middleman_actor_impl::~middleman_actor_impl() {
  CAF_LOG_TRACE("");
}

middleman* middleman::instance() {
  CAF_LOGF_TRACE("");
  auto sid = detail::singletons::middleman_plugin_id;
  auto fac = [] { return new middleman; };
  auto res = detail::singletons::get_plugin_singleton(sid, fac);
  return static_cast<middleman*>(res);
}

void middleman::add_broker(broker_ptr bptr) {
  m_brokers.insert(bptr);
  bptr->attach_functor([=](uint32_t) { m_brokers.erase(bptr); });
}

void middleman::initialize() {
  CAF_LOG_TRACE("");
  m_backend = network::multiplexer::make();
  m_backend_supervisor = m_backend->make_supervisor();
  m_thread = std::thread([this] {
    CAF_LOG_TRACE("");
    m_backend->run();
  });
  m_backend->thread_id(m_thread.get_id());
  // announce io-related types
  do_announce<new_data_msg>("caf::io::new_data_msg");
  do_announce<new_connection_msg>("caf::io::new_connection_msg");
  do_announce<acceptor_closed_msg>("caf::io::acceptor_closed_msg");
  do_announce<connection_closed_msg>("caf::io::connection_closed_msg");
  do_announce<accept_handle>("caf::io::accept_handle");
  do_announce<acceptor_closed_msg>("caf::io::acceptor_closed_msg");
  do_announce<connection_closed_msg>("caf::io::connection_closed_msg");
  do_announce<connection_handle>("caf::io::connection_handle");
  do_announce<new_connection_msg>("caf::io::new_connection_msg");
  do_announce<new_data_msg>("caf::io::new_data_msg");
  actor mgr = get_named_broker<basp_broker>(atom("_BASP"));
  m_manager = spawn_typed<middleman_actor_impl, hidden>(*this, mgr);
}

void middleman::stop() {
  CAF_LOG_TRACE("");
  m_backend->dispatch([=] {
    CAF_LOG_TRACE("");
    // m_managers will be modified while we are stopping each manager,
    // because each manager will call remove(...)
    for (auto& kvp : m_named_brokers) {
      if (kvp.second->exit_reason() == exit_reason::not_exited) {
        kvp.second->cleanup(exit_reason::normal);
      }
    }
  });
  m_backend_supervisor.reset();
  m_thread.join();
  m_named_brokers.clear();
  scoped_actor self(true);
  self->monitor(m_manager);
  self->send_exit(m_manager, exit_reason::user_shutdown);
  self->receive(
    [](const down_msg&) {
      // nop
    }
  );
}

void middleman::dispose() {
  delete this;
}

middleman::middleman() {
  // nop
}

middleman::~middleman() {
  // nop
}

middleman_actor middleman::actor_handle() {
  return m_manager;
}

middleman_actor get_middleman_actor() {
  return middleman::instance()->actor_handle();
}

} // namespace io
} // namespace caf
