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

#include <iostream>

#include "ping_pong.hpp"

#define CAF_SUITE "spawn"

#include "caf/all.hpp"
#include "caf/detail/logging.hpp"
#include "caf/test/unit_test.hpp"

using std::cout;
using std::endl;
using namespace caf;
using namespace caf;

namespace {

size_t s_pongs = 0;

behavior ping_behavior(local_actor* self, size_t num_pings) {
  return {
    on(atom("pong"), arg_match) >> [=](int value)->message {
      if (!self->current_sender()) {
        CAF_TEST_ERROR("current_sender() invalid!");
      }
      CAF_TEST_INFO("received {'pong', " << value << "}");
      // cout << to_string(self->current_message()) << endl;
      if (++s_pongs >= num_pings) {
        CAF_TEST_INFO("reached maximum, send {'EXIT', user_defined} "
                      << "to last sender and quit with normal reason");
        self->send_exit(self->current_sender(),
                exit_reason::user_shutdown);
        self->quit();
      }
      return make_message(atom("ping"), value);
    },
    others() >> [=] {
      self->quit(exit_reason::user_shutdown);
    }
  };
}

behavior pong_behavior(local_actor* self) {
  return {
    on(atom("ping"), arg_match) >> [](int value)->message {
      return make_message(atom("pong"), value + 1);
    },
    others() >> [=] {
      self->quit(exit_reason::user_shutdown);
    }
  };
}

} // namespace <anonymous>

size_t pongs() { return s_pongs; }

void ping(blocking_actor* self, size_t num_pings) {
  s_pongs = 0;
  self->receive_loop(ping_behavior(self, num_pings));
}

void event_based_ping(event_based_actor* self, size_t num_pings) {
  s_pongs = 0;
  self->become(ping_behavior(self, num_pings));
}

void pong(blocking_actor* self, actor ping_actor) {
  self->send(ping_actor, atom("pong"), 0); // kickoff
  self->receive_loop(pong_behavior(self));
}

void event_based_pong(event_based_actor* self, actor ping_actor) {
  CAF_LOGF_TRACE("ping_actor = " << to_string(ping_actor));
  CAF_REQUIRE(ping_actor != invalid_actor);
  self->send(ping_actor, atom("pong"), 0); // kickoff
  self->become(pong_behavior(self));
}

CAF_TEST("ping-pong") {
  using namespace caf;
  scoped_actor self;
  self->trap_exit(true);
  auto ping_actor = self->spawn<monitored+blocking_api>(ping, 10);
  auto pong_actor = self->spawn<monitored+blocking_api>(pong, ping_actor);
  self->link_to(pong_actor);
  int i = 0;
  int flags = 0;
  self->delayed_send(self, std::chrono::seconds(1), atom("FooBar"));
  // wait for DOWN and EXIT messages of pong
  self->receive_for(i, 4) (
    [&](const exit_msg& em) {
      CAF_CHECK(em.source == pong_actor);
      CAF_CHECK(em.reason == exit_reason::user_shutdown);
      flags |= 0x01;
    },
    [&](const down_msg& dm) {
      if (dm.source == pong_actor) {
        flags |= 0x02;
        CAF_CHECK(dm.reason == exit_reason::user_shutdown);
      }
      else if (dm.source == ping_actor) {
        flags |= 0x04;
        CAF_CHECK(dm.reason == exit_reason::normal);
      }
    },
    [&](const atom_value& val) {
      CAF_CHECK(val == atom("FooBar"));
      flags |= 0x08;
    },
    others() >> [&]() {
      // FIXME: abort test with error message.
      //CAF_FAILURE("unexpected message: " << to_string(self->current_message()));
    },
    after(std::chrono::seconds(5)) >> [&]() {
      // FIXME: abort test with error message.
      //CAF_FAILURE("timeout in file " << __FILE__ << " in line " << __LINE__);
    }
  );
  // wait for termination of all spawned actors
  self->await_all_other_actors_done();
  CAF_CHECK(flags == 0x0F);
  CAF_CHECK(pongs() == 10);
}
