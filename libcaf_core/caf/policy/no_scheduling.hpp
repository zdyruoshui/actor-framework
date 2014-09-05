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

#ifndef CAF_NO_SCHEDULING_HPP
#define CAF_NO_SCHEDULING_HPP

#include <limits>

#include "caf/mutex.hpp"
#include "caf/chrono.hpp"
#include "caf/thread.hpp"
#include "caf/duration.hpp"
#include "caf/exit_reason.hpp"
#include "caf/condition_variable.hpp"

#include "caf/policy/scheduling_policy.hpp"

#include "caf/detail/logging.hpp"
#include "caf/detail/singletons.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/detail/actor_registry.hpp"
#include "caf/detail/sync_request_bouncer.hpp"
#include "caf/detail/single_reader_queue.hpp"


#include "caf/actor_ostream.hpp"



namespace caf {
namespace policy {


#ifdef __RIOTBUILD_FLAG

class no_scheduling {

 public:
  using timeout_type = std::chrono::high_resolution_clock::time_point;
  no_scheduling() {
    pthread_mutex_init(&m_mtx, nullptr);
    pthread_cond_init(&m_cv, nullptr);
  }

  template <class Actor>
  void enqueue(Actor* self, const actor_addr& sender, message_id mid,
               message& msg, execution_unit*) {
    auto ptr = self->new_mailbox_element(sender, mid, std::move(msg));
    // returns false if mailbox has been closed
    if (!self->mailbox().synchronized_enqueue(m_mtx, m_cv, ptr)) {
      if (mid.is_request()) {
        detail::sync_request_bouncer srb{self->exit_reason()};
        srb(sender, mid);
      }
    }
  }

  template <class Actor>
  void launch(Actor* self, execution_unit*) {
    CAF_REQUIRE(self != nullptr);
    CAF_PUSH_AID(self->id());
    CAF_LOG_TRACE(CAF_ARG(self));
    m_self = reinterpret_cast<void*>(self);
    self->attach_to_scheduler();
    auto loop = [](void* arg) {
      auto context = reinterpret_cast<no_scheduling*>(arg);
      auto mself = reinterpret_cast<Actor*>(context->m_self);
      CAF_PUSH_AID(mself->id());
      CAF_LOG_TRACE("");
      while (mself->resume(nullptr, 0) != resumable::done) {
        // await new data before resuming actor
        context->await_data(mself);
        CAF_REQUIRE(mself->mailbox().blocked() == false);
      }
      mself->detach_from_scheduler();
      return static_cast<void*>(NULL);
    };
    auto rc = pthread_create(&m_thread, nullptr, loop, reinterpret_cast<void*>(this));
    if (rc != 0) {
      // todo error
    }
    rc = pthread_detach(m_thread);
    if (rc != 0) {
      // todo error
    }
  }

  // await_data is being called from no_resume (only)
  template <class Actor>
  void await_data(Actor* self) {
    if (self->has_next_message()) return;
    self->mailbox().synchronized_await(m_mtx, m_cv);
  }

  // this additional member function is needed to implement
  // timer_actor (see scheduler.cpp)
  template <class Actor, class TimePoint>
  bool await_data(Actor* self, const TimePoint& tp) {
    if (self->has_next_message()) return true;
    return self->mailbox().synchronized_await(m_mtx, m_cv, tp);
  }


 private:
  // reference to the actor
  // accessible in the launch lambda through this
  void* m_self;
  pthread_t m_thread;
  pthread_mutex_t m_mtx;
  pthread_cond_t m_cv;

};

#else
class no_scheduling {
  using lock_type = std::unique_lock<std::mutex>;
 public:
  using timeout_type = std::chrono::high_resolution_clock::time_point;

  template <class Actor>
  void enqueue(Actor* self, mailbox_element_ptr ptr, execution_unit*) {
    auto mid = ptr->mid;
    auto sender = ptr->sender;
    // returns false if mailbox has been closed
    if (!self->mailbox().synchronized_enqueue(m_mtx, m_cv, ptr.release())) {
      if (mid.is_request()) {
        detail::sync_request_bouncer srb{self->exit_reason()};
        srb(sender, mid);
      }
    }
  }

  template <class Actor>
  void launch(Actor* self, execution_unit*, bool) {
    CAF_REQUIRE(self != nullptr);
    CAF_PUSH_AID(self->id());
    CAF_LOG_TRACE(CAF_ARG(self));
    intrusive_ptr<Actor> mself{self};
    self->attach_to_scheduler();
    std::thread([=] {
      CAF_PUSH_AID(mself->id());
      CAF_LOG_TRACE("");
      auto max_throughput = std::numeric_limits<size_t>::max();
      while (mself->resume(nullptr, max_throughput) != resumable::done) {
        // await new data before resuming actor
        await_data(mself.get());
        CAF_REQUIRE(self->mailbox().blocked() == false);
      }
      self->detach_from_scheduler();
    }).detach();
  }

  // await_data is being called from no_resume (only)
  template <class Actor>
  void await_data(Actor* self) {
    if (self->has_next_message()) return;
    self->mailbox().synchronized_await(m_mtx, m_cv);
  }

  // this additional member function is needed to implement
  // timer_actor (see scheduler.cpp)
  template <class Actor, class TimePoint>
  bool await_data(Actor* self, const TimePoint& tp) {
    if (self->has_next_message()) return true;
    return self->mailbox().synchronized_await(m_mtx, m_cv, tp);
  }

 private:
  std::mutex m_mtx;
  std::condition_variable m_cv;
};
#endif

} // namespace policy
} // namespace caf

#endif // CAF_NO_SCHEDULING_HPP
