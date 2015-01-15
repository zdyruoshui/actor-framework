/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2014                                                  *
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

#ifndef CAF_IO_BASP_HPP
#define CAF_IO_BASP_HPP

#include <cstdint>

#include "caf/node_id.hpp"
#include "caf/abstract_actor.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/binary_deserializer.hpp"

namespace caf {
namespace io {
namespace basp {

/**
 * The header of a Binary Actor System Protocol (BASP) message.
 * A BASP header consists of a routing part, i.e., source and
 * destination, as well as an operation and operation data. Several
 * message types consist of only a header.
 */
struct header {
  node_id source_node;
  node_id dest_node;
  actor_id source_actor;
  actor_id dest_actor;
  uint32_t payload_len;
  uint32_t operation;
  uint64_t operation_data;
};

/**
 * The current BASP version. Different BASP versions will not
 * be able to exchange messages.
 */
constexpr uint64_t version = 2;

/**
 * Size of a BASP header in serialized form
 */
constexpr size_t header_size =
  node_id::host_id_size * 2 + sizeof(uint32_t) * 2 +
  sizeof(actor_id) * 2 + sizeof(uint32_t) * 2 + sizeof(uint64_t);

inline bool valid(const node_id& val) {
  return val != invalid_node_id;
}

inline bool invalid(const node_id& val) {
  return !valid(val);
}

template <class T>
inline bool zero(T val) {
  return val == 0;
}

template <class T>
inline bool nonzero(T aid) {
  return !zero(aid);
}

/**
 * Send from server, i.e., the node with a published actor, to client,
 * i.e., node that initiates a new connection using remote_actor().
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of server
 * dest_node      | invalid
 * source_actor   | Optional: ID of published actor
 * dest_actor     | 0
 * payload_len    | Optional: size of actor id + interface definition
 * operation_data | BASP version of the server
 */
constexpr uint32_t server_handshake = 0x00;

inline bool server_handshake_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && invalid(hdr.dest_node)
       && zero(hdr.dest_actor)
       && nonzero(hdr.operation_data)
       && (   (nonzero(hdr.source_actor) && nonzero(hdr.payload_len))
           || (zero(hdr.source_actor) && zero(hdr.payload_len)));
}

/**
 * Send from client to server after it has successfully received the
 * server_handshake to establish the connection.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of client
 * dest_node      | ID of server
 * source_actor   | 0
 * dest_actor     | 0
 * payload_len    | 0
 * operation_data | 0
 */
constexpr uint32_t client_handshake = 0x01;

inline bool client_handshake_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && valid(hdr.dest_node)
       && hdr.source_node != hdr.dest_node
       && zero(hdr.source_actor)
       && zero(hdr.dest_actor)
       && zero(hdr.payload_len)
       && zero(hdr.operation_data);
}

/**
 * Transmits a message from source_node:source_actor to
 * dest_node:dest_actor.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of sending node (invalid in case of anon_send)
 * dest_node      | ID of receiving node
 * source_actor   | ID of sending actor (invalid in case of anon_send)
 * dest_actor     | ID of receiving actor, must not be invalid
 * payload_len    | size of serialized message object, must not be 0
 * operation_data | message ID (0 for asynchronous messages)
 */
constexpr uint32_t dispatch_message = 0x02;

inline bool dispatch_message_valid(const header& hdr) {
  return  valid(hdr.dest_node)
       && nonzero(hdr.dest_actor)
       && nonzero(hdr.payload_len);
}

/**
 * Informs the receiving node that the sending node has created a proxy
 * instance for one of its actors. Causes the receiving node to attach
 * a functor to the actor that triggers a kill_proxy_instance
 * message on termination.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of sending node
 * dest_node      | ID of receiving node
 * source_actor   | 0
 * dest_actor     | ID of monitored actor
 * payload_len    | 0
 * operation_data | 0
 */
constexpr uint32_t announce_proxy_instance = 0x03;

inline bool announce_proxy_instance_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && valid(hdr.dest_node)
       && hdr.source_node != hdr.dest_node
       && zero(hdr.source_actor)
       && nonzero(hdr.dest_actor)
       && zero(hdr.payload_len)
       && zero(hdr.operation_data);
}

/**
 * Informs the receiving node that it has a proxy for an actor
 * that has been terminated.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of sending node
 * dest_node      | ID of receiving node
 * source_actor   | ID of monitored actor
 * dest_actor     | 0
 * payload_len    | 0
 * operation_data | exit reason (uint32)
 */
constexpr uint32_t kill_proxy_instance = 0x04;

inline bool kill_proxy_instance_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && valid(hdr.dest_node)
       && hdr.source_node != hdr.dest_node
       && nonzero(hdr.source_actor)
       && zero(hdr.dest_actor)
       && zero(hdr.payload_len)
       && nonzero(hdr.operation_data);
}

/**
 * Establish a direct connection between two nodes. This is a two-step
 * operation. In the first step, node A wants node C to connect to node B.
 * Node A sends node B a `direct_conn_request` with `operation_data == 0`
 * and `target == C` in the payload. When B receives the request, it sends a
 * "completed" request with `operation_data == 1` and the full payload
 * containing port and address(es) for the connection. Node C then receives
 * the request, tries to connect to B using the given addresses and sends the
 * `direct_conn_response` directly to A, i.e., the`request_origin` from
 * the payload.
 *
 * A request can also be send from B to C directly to optimize routing
 * (without a third node triggering it). In this case A sends only a
 * completed request and is sender as well as request origin.
 *
 * The payload is:
 * - operation_data is 0 => {node_id request_origin, node_id target}
 * - operation_data is 1 => {node_id request_origin, node_id target,
 *                           uint16_t port, vector<pair<string>> addresses}
 *
 * Each pair of strings contains the protocol, e.g., "ipv4" or
 * "ipv6", followed by a technology-specific locator.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of node receiving the response
 * dest_node      | ID of receiving node
 * source_actor   | 0
 * dest_actor     | 0
 * payload_len    | size of serialized data, must not be 0
 * operation_data | 0 or 1, indicating whether the request is completed or not
 */
constexpr uint32_t direct_conn_request = 0x05;

inline bool direct_conn_request_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && valid(hdr.dest_node)
       && hdr.source_node != hdr.dest_node
       && zero(hdr.source_actor)
       && zero(hdr.dest_actor)
       && nonzero(hdr.payload_len)
       && zero(hdr.operation_data);
}

/**
 * Response to a direct connection request. If the connection has been
 * established successfully, *operation_data* contains 1 and 0 otherwise.
 *
 * Field          | Assignment
 * ---------------|----------------------------------------------------------
 * source_node    | ID of node that initiated the direct connection
 * dest_node      | ID of node in the source field of the request
 * source_actor   | 0
 * dest_actor     | 0
 * payload_len    | 0
 * operation_data | 0 or 1, indicating failure or success
 */
constexpr uint32_t direct_conn_response = 0x06;

inline bool direct_conn_response_valid(const header& hdr) {
  return  valid(hdr.source_node)
       && valid(hdr.dest_node)
       && zero(hdr.source_actor)
       && zero(hdr.dest_actor)
       && zero(hdr.payload_len)
       && hdr.operation_data <= 1;
}

/**
 * Checks whether given header is valid.
 */
inline bool valid(header& hdr) {
  switch (hdr.operation) {
    default:
      return false; // invalid operation field
    case server_handshake:
      return server_handshake_valid(hdr);
    case client_handshake:
      return client_handshake_valid(hdr);
    case dispatch_message:
      return dispatch_message_valid(hdr);
    case announce_proxy_instance:
      return announce_proxy_instance_valid(hdr);
    case kill_proxy_instance:
      return kill_proxy_instance_valid(hdr);
    case direct_conn_request:
      return direct_conn_request_valid(hdr);
    case direct_conn_response:
      return direct_conn_response_valid(hdr);
  }
}

} // namespace basp
} // namespace io
} // namespace caf

#endif // CAF_IO_BASP_HPP
