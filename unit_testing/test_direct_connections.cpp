#include <iostream>

#define CAF_LOG_LEVEL CAF_TRACE

#include "test.hpp"

#include "caf/all.hpp"
#include "caf/io/all.hpp"

#include "caf/detail/logging.hpp"
#include "caf/detail/singletons.hpp"

using namespace std;
using namespace caf;

// Test scenario:
//
//    A <--> C <--> B
//
// Node A (source) wants to send a message to node B (sink), but does not have
// a direct connection to it. A knows B only via the relay C. The first time C
// relays a message from A to B, A attempts to create a direct connection to B.

struct relay : event_based_actor {
  behavior make_behavior() {
    port = io::publish(this, 0);
    cout << "relay published at port " << port << endl;
    return {
      on(atom("port")) >> [=] {
        return port;
      },
      on(atom("sink"), arg_match) >> [=](actor const& a) {
        sink = a;
      },
      on(atom("sink")) >> [=] {
        return sink;
      },
      on(atom("done")) >> [=] {
        quit();
      }
    };
  }

  actor sink;
  uint16_t port;
};

void sink_actor(event_based_actor* self, uint16_t port) {
  CAF_LOGF_INFO("connecting to relay on port " << port);
  auto relay = io::remote_actor("127.0.0.1", port);
  self->send(relay, atom("sink"), self);
  self->become(
    on_arg_match >> [](int value) {
      CAF_LOGF_INFO("got data " << value);
      return value + 1337;
    },
    on(atom("done")) >> [=] {
      self->quit();
    }
  );
}

void run_source(uint16_t port) {
  CAF_LOGF_INFO("connecting to relay on port " << port);
  auto relay = io::remote_actor("127.0.0.1", port);
  actor sink;
  scoped_actor self;
  cout << "getting sink from relay" << endl;
  self->sync_send(relay, atom("sink")).await(
    on_arg_match >> [&](actor const& a) {
      sink = a;
    }
  );
  cout << "sending 1st message to be routed via relay" << endl;
  self->sync_send(sink, 42).await(
    on_arg_match >> [&](int value) {
      CAF_LOGF_INFO("got response: " << value);
      CAF_CHECK(value == 1337 + 42);
    });
  cout << "sleeping to ensure direct connection will be established" << endl;
  std::this_thread::sleep_for(std::chrono::seconds(1));
  cout << "sending 2nd message to be routed directly" << endl;
  self->sync_send(sink, 7).await(
    on_arg_match >> [&](int value) {
      CAF_LOGF_INFO("got response: " << value);
      CAF_CHECK(value == 1337 + 7);
    });
  // shoot the relay down, then the sink
  self->send(relay, atom("done"));
  self->send(sink, atom("done"));
}

int main(int argc, char** argv) {
  CAF_TEST(test_direct_connections);
  message_builder{argv + 1, argv + argc}.apply({
    on("--relay") >> []() {
      cerr << "running as relay: " 
        << to_string(caf::detail::singletons::get_node_id()) << endl;
      spawn<relay>();
    },
    on("--sink", spro<uint16_t>) >> [](uint16_t port) {
      cerr << "running as sink: " 
        << to_string(caf::detail::singletons::get_node_id()) << endl;
      spawn(sink_actor, port);
    },
    on("--source", spro<uint16_t>) >> [](uint16_t port) {
      cerr << "running as source: " 
        << to_string(caf::detail::singletons::get_node_id()) << endl;
      run_source(port);
    },
    on() >> [&] {
      cerr << "running all-in-one mode: " 
        << to_string(caf::detail::singletons::get_node_id()) << endl;
      uint16_t port = 0;
      auto r = spawn<relay>();
      scoped_actor self;
      self->sync_send(r, atom("port")).await([&](uint16_t p) { port = p; });
      CAF_CHECK(port > 0);
      CAF_CHECKPOINT();
      spawn(sink_actor, port);
      run_source(port);
    },
    others() >> [&] {
      cerr
        << "usage: " << argv[0] 
        << " [--sink <relay_port> | --source <relay_port>| --relay]" << endl;
    }
  });
  await_all_actors_done();
  shutdown();
  return CAF_TEST_RESULT();
}
