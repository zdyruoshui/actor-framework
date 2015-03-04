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

#ifndef CAF_MIXIN_BEHAVIOR_STACK_BASED_HPP
#define CAF_MIXIN_BEHAVIOR_STACK_BASED_HPP

#include "caf/message_id.hpp"
#include "caf/typed_behavior.hpp"
#include "caf/behavior_policy.hpp"
#include "caf/response_handle.hpp"

#include "caf/detail/behavior_stack.hpp"

namespace caf {
namespace mixin {

template <class Base, class Subtype, class BehaviorType>
class behavior_stack_based_impl : public Base {
 public:
  // types and constructors
  using behavior_type = BehaviorType;
  using combined_type = behavior_stack_based_impl;
  using response_handle_type = response_handle<behavior_stack_based_impl,
                                               message,
                                               nonblocking_response_handle_tag>;

  template <class... Ts>
  behavior_stack_based_impl(Ts&&... vs)
      : Base(std::forward<Ts>(vs)...),
        m_timeout_id(0) {
    // nop
  }

  /****************************************************************************
   *                     become() member function family                      *
   ****************************************************************************/

  void become(behavior_type bhvr) {
    do_become(std::move(bhvr), true);
  }

  void become(const keep_behavior_t&, behavior_type bhvr) {
    do_become(std::move(bhvr), false);
  }

  template <class T, class... Ts>
  typename std::enable_if<
    !std::is_same<keep_behavior_t, typename std::decay<T>::type>::value,
    void
  >::type
  become(T&& arg, Ts&&... args) {
    do_become(behavior_type{std::forward<T>(arg), std::forward<Ts>(args)...},
              true);
  }

  template <class... Ts>
  void become(const keep_behavior_t&, Ts&&... args) {
    do_become(behavior_type{std::forward<Ts>(args)...}, false);
  }

  void unbecome() {
    m_bhvr_stack.pop_async_back();
  }

  /****************************************************************************
   *            convenience member function for stack manipulation            *
   ****************************************************************************/

  bool has_behavior() const {
    return m_bhvr_stack.empty() == false;
  }

  behavior& get_behavior() {
    CAF_ASSERT(m_bhvr_stack.empty() == false);
    return m_bhvr_stack.back();
  }

  optional<behavior&> sync_handler(message_id msg_id) {
    return m_bhvr_stack.sync_handler(msg_id);
  }

  void remove_handler(message_id mid) {
    m_bhvr_stack.erase(mid);
  }

  detail::behavior_stack& bhvr_stack() {
    return m_bhvr_stack;
  }

  /****************************************************************************
   *                             timeout handling                             *
   ****************************************************************************/

  void request_timeout(const duration& d) {
    if (d.valid()) {
      this->has_timeout(true);
      auto tid = ++m_timeout_id;
      auto msg = make_message(timeout_msg{tid});
      if (d.is_zero()) {
        // immediately enqueue timeout message if duration == 0s
        this->enqueue(this->address(), invalid_message_id,
                      std::move(msg), this->host());
      } else
        this->delayed_send(this, d, std::move(msg));
    } else
      this->has_timeout(false);
  }

  bool waits_for_timeout(uint32_t timeout_id) const {
    return this->has_timeout() && m_timeout_id == timeout_id;
  }

  bool is_active_timeout(uint32_t tid) const {
    return waits_for_timeout(tid);
  }

  uint32_t active_timeout_id() const {
    return m_timeout_id;
  }

  void reset_timeout() {
    this->has_timeout(false);
  }

  void handle_timeout(behavior& bhvr, uint32_t timeout_id) {
    if (this->is_active_timeout(timeout_id)) {
      this->reset_timeout();
      bhvr.handle_timeout();
      // request next timeout if behavior stack is not empty
      // and timeout handler did not set a new timeout, e.g.,
      // by calling become()
      if (!this->has_timeout() && has_behavior()) {
        this->request_timeout(get_behavior().timeout());
      }
    }
  }

 private:
  void do_become(behavior_type bhvr, bool discard_old) {
    if (discard_old) this->m_bhvr_stack.pop_async_back();
    // request_timeout simply resets the timeout when it's invalid
    this->request_timeout(bhvr.timeout());
    this->m_bhvr_stack.push_back(std::move(unbox(bhvr)));
  }

  static behavior& unbox(behavior& arg) {
    return arg;
  }

  template <class... Ts>
  static behavior& unbox(typed_behavior<Ts...>& arg) {
    return arg.unbox();
  }

  // utility for getting a pointer-to-derived-type
  Subtype* dptr() {
    return static_cast<Subtype*>(this);
  }

  // utility for getting a const pointer-to-derived-type
  const Subtype* dptr() const {
    return static_cast<const Subtype*>(this);
  }

  detail::behavior_stack m_bhvr_stack;
  uint32_t m_timeout_id;
};

/**
 * Mixin for actors using a stack-based message processing.
 * @note This mixin implicitly includes {@link single_timeout}.
 */
template <class BehaviorType>
class behavior_stack_based {
 public:
  template <class Base, class Subtype>
  class impl : public behavior_stack_based_impl<Base, Subtype, BehaviorType> {
   public:
    using super = behavior_stack_based_impl<Base, Subtype, BehaviorType>;

    using combined_type = impl;

    template <class... Ts>
    impl(Ts&&... args)
        : super(std::forward<Ts>(args)...) {
      // nop
    }
  };
};

} // namespace mixin
} // namespace caf

#endif // CAF_MIXIN_BEHAVIOR_STACK_BASED_HPP
