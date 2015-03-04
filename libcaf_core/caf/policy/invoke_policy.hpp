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

#ifndef CAF_POLICY_INVOKE_POLICY_HPP
#define CAF_POLICY_INVOKE_POLICY_HPP

#include <memory>
#include <type_traits>

#include "caf/none.hpp"

#include "caf/on.hpp"
#include "caf/behavior.hpp"
#include "caf/to_string.hpp"
#include "caf/message_id.hpp"
#include "caf/exit_reason.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/system_messages.hpp"
#include "caf/response_promise.hpp"

#include "caf/detail/memory.hpp"
#include "caf/detail/logging.hpp"
#include "caf/detail/scope_guard.hpp"

namespace caf {
namespace policy {

enum invoke_message_result {
  im_success,
  im_skipped,
  im_dropped
};

/**
 * Base class for invoke policies.
 */
class invoke_policy {
 public:
  enum class msg_type {
    normal_exit,           // an exit message with normal exit reason
    non_normal_exit,       // an exit message with abnormal exit reason
    expired_timeout,       // an 'old & obsolete' timeout
    inactive_timeout,      // a currently inactive timeout
    expired_sync_response, // a sync response that already timed out
    timeout,               // triggers currently active timeout
    ordinary,              // an asynchronous message or sync. request
    sync_response          // a synchronous response
  };

  // the workflow of invoke_message (im) is as follows:
  // - should_skip? if yes: return im_skipped
  // - msg is ordinary message? if yes:
  //   - begin(...) -> prepares a self for message handling
  //   - self could process message?
  //   - yes: cleanup()
  //   - no: revert(...) -> set self back to state it had before begin()
  template <class Actor>
  invoke_message_result invoke_message(Actor* self, mailbox_element_ptr& node,
                                       behavior& fun,
                                       message_id awaited_response) {
    CAF_LOG_TRACE("");
    switch (this->filter_msg(self, *node)) {
      case msg_type::normal_exit:
        CAF_LOG_DEBUG("dropped normal exit signal");
        return im_dropped;
      case msg_type::expired_sync_response:
        CAF_LOG_DEBUG("dropped expired sync response");
        return im_dropped;
      case msg_type::expired_timeout:
        CAF_LOG_DEBUG("dropped expired timeout message");
        return im_dropped;
      case msg_type::inactive_timeout:
        CAF_LOG_DEBUG("skipped inactive timeout message");
        return im_skipped;
      case msg_type::non_normal_exit:
        CAF_LOG_DEBUG("handled non-normal exit signal");
        // this message was handled
        // by calling self->quit(...)
        return im_success;
      case msg_type::timeout: {
        CAF_LOG_DEBUG("handle timeout message");
        auto& tm = node->msg.get_as<timeout_msg>(0);
        self->handle_timeout(fun, tm.timeout_id);
        if (awaited_response.valid()) {
          self->mark_arrived(awaited_response);
          self->remove_handler(awaited_response);
        }
        return im_success;
      }
      case msg_type::sync_response:
        CAF_LOG_DEBUG("handle as synchronous response: "
                      << CAF_TARG(node->msg, to_string) << ", "
                      << CAF_MARG(node->mid, integer_value) << ", "
                      << CAF_MARG(awaited_response, integer_value));
        if (awaited_response.valid() && node->mid == awaited_response) {
          bool is_sync_tout = node->msg.match_elements<sync_timeout_msg>();
          node.swap(self->current_mailbox_element());
          auto res = invoke_fun(self, fun);
          node.swap(self->current_mailbox_element());
          self->mark_arrived(awaited_response);
          self->remove_handler(awaited_response);
          if (!res) {
            if (is_sync_tout) {
              CAF_LOG_WARNING("sync timeout occured in actor "
                              << "with ID " << self->id());
              self->handle_sync_timeout();
            } else {
              CAF_LOG_WARNING("sync failure occured in actor "
                              << "with ID " << self->id());
              self->handle_sync_failure();
            }
          }
          return im_success;
        }
        return im_skipped;
      case msg_type::ordinary:
        if (!awaited_response.valid()) {
          node.swap(self->current_mailbox_element());
          auto res = invoke_fun(self, fun);
          node.swap(self->current_mailbox_element());
          if (res) {
            return im_success;
          }
        }
        CAF_LOG_DEBUG_IF(awaited_response.valid(),
                  "ignored message; await response: "
                    << awaited_response.integer_value());
        return im_skipped;
    }
    // should be unreachable
    CAF_CRITICAL("invalid message type");
  }

  template <class Actor>
  response_promise fetch_response_promise(Actor* cl, int) {
    return cl->make_response_promise();
  }

  template <class Actor>
  response_promise fetch_response_promise(Actor*, response_promise& hdl) {
    return std::move(hdl);
  }

  // - extracts response message from handler
  // - returns true if fun was successfully invoked
  template <class Actor, class Fun, class MaybeResponseHdl = int>
  optional<message> invoke_fun(Actor* self, Fun& fun,
                               MaybeResponseHdl hdl = MaybeResponseHdl{}) {
#   ifdef CAF_LOG_LEVEL
    auto msg = to_string(self->current_mailbox_element()->msg);
#   endif
    auto mid = self->current_mailbox_element()->mid;
    CAF_LOG_TRACE(CAF_MARG(mid, integer_value) << ", " << CAF_ARG(msg));
    auto res = fun(self->current_mailbox_element()->msg);
    CAF_LOG_DEBUG(msg << " => " << to_string(res));
    if (!res) {
      return none;
    }
    if (res->empty()) {
      // make sure synchronous requests always receive a response;
      // note: !current_mailbox_element() means client has forwarded the request
      auto& ptr = self->current_mailbox_element();
      if (ptr) {
        mid = ptr->mid;
        if (mid.is_request() && !mid.is_answered()) {
          CAF_LOG_WARNING("actor with ID " << self->id()
                          << " did not reply to a synchronous request message");
          auto fhdl = fetch_response_promise(self, hdl);
          if (fhdl) {
            fhdl.deliver(make_message(unit));
          }
        }
      }
      return res;
    }
    CAF_LOGF_DEBUG("res = " << to_string(*res));
    if (handle_message_id_res(self, *res, none)) {
      return message{};
    }
    // respond by using the result of 'fun'
    CAF_LOG_DEBUG("respond via response_promise");
    auto fhdl = fetch_response_promise(self, hdl);
    if (fhdl) {
      fhdl.deliver(std::move(*res));
      // inform caller about success by returning not none
      return message{};
    }
    return res;
  }

 private:
  // enables `return sync_send(...).then(...)`
  template <class Actor>
  bool handle_message_id_res(Actor* self, message& res,
                             optional<response_promise> hdl) {
    if (res.match_elements<atom_value, uint64_t>()
        && res.get_as<atom_value>(0) == atom("MESSAGE_ID")) {
      CAF_LOG_DEBUG("message handler returned a message id wrapper");
      auto id = res.get_as<uint64_t>(1);
      auto msg_id = message_id::from_integer_value(id);
      auto ref_opt = self->sync_handler(msg_id);
      // install a behavior that calls the user-defined behavior
      // and using the result of its inner behavior as response
      if (ref_opt) {
        response_promise fhdl = hdl ? *hdl : self->make_response_promise();
        behavior inner = *ref_opt;
        ref_opt->assign(
          others >> [=] {
            // inner is const inside this lambda and mutable a C++14 feature
            behavior cpy = inner;
            auto inner_res = cpy(self->current_message());
            if (inner_res && !handle_message_id_res(self, *inner_res, fhdl)) {
              fhdl.deliver(*inner_res);
            }
          }
        );
        return true;
      }
    }
    return false;
  }

  // identifies 'special' messages that should not be processed normally:
  // - system messages such as exit_msg and timeout_msg
  // - expired synchronous response messages
  template <class Actor>
  msg_type filter_msg(Actor* self, mailbox_element& node) {
    const message& msg = node.msg;
    auto mid = node.mid;
    if (mid.is_response()) {
      return self->awaits(mid) ? msg_type::sync_response
                               : msg_type::expired_sync_response;
    }
    if (msg.size() != 1) {
      return msg_type::ordinary;
    }
    if (msg.match_element<timeout_msg>(0)) {
      auto& tm = msg.get_as<timeout_msg>(0);
      auto tid = tm.timeout_id;
      CAF_ASSERT(!mid.valid());
      if (self->is_active_timeout(tid)) {
        return msg_type::timeout;
      }
      return self->waits_for_timeout(tid) ? msg_type::inactive_timeout
                                          : msg_type::expired_timeout;
    }
    if (msg.match_element<exit_msg>(0)) {
      auto& em = msg.get_as<exit_msg>(0);
      CAF_ASSERT(!mid.valid());
      // make sure to get rid of attachables if they're no longer needed
      self->unlink_from(em.source);
      if (self->trap_exit() == false) {
        if (em.reason != exit_reason::normal) {
          self->quit(em.reason);
          return msg_type::non_normal_exit;
        }
        return msg_type::normal_exit;
      }
    }
    return msg_type::ordinary;
  }
};

} // namespace policy
} // namespace caf

#endif // CAF_POLICY_INVOKE_POLICY_HPP
