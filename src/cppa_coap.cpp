/******************************************************************************\
 *           ___        __                                                    *
 *          /\_ \    __/\ \                                                   *
 *          \//\ \  /\_\ \ \____    ___   _____   _____      __               *
 *            \ \ \ \/\ \ \ '__`\  /'___\/\ '__`\/\ '__`\  /'__`\             *
 *             \_\ \_\ \ \ \ \L\ \/\ \__/\ \ \L\ \ \ \L\ \/\ \L\.\_           *
 *             /\____\\ \_\ \_,__/\ \____\\ \ ,__/\ \ ,__/\ \__/.\_\          *
 *             \/____/ \/_/\/___/  \/____/ \ \ \/  \ \ \/  \/__/\/_/          *
 *                                          \ \_\   \ \_\                     *
 *                                           \/_/    \/_/                     *
 *                                                                            *
 * Copyright (C) 2011-2014                                                    *
 * Dominik Charousset <dominik.charousset@haw-hamburg.de>                     *
 * Raphael Hiesgen <raphael.hiesgen@haw-hamburg.de>                           *
 *                                                                            *
 * This file is part of libcppa.                                              *
 * libcppa is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU Lesser General Public License as published by the     *
 * Free Software Foundation; either version 2.1 of the License,               *
 * or (at your option) any later version.                                     *
 *                                                                            *
 * libcppa is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                       *
 * See the GNU Lesser General Public License for more details.                *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with libcppa. If not, see <http://www.gnu.org/licenses/>.            *
\******************************************************************************/

#include <string>
#include <netdb.h>
#include <arpa/inet.h>

#include "coap.h"

#include "cppa/cppa.hpp"
#include "cppa/cppa_coap.hpp"
#include "cppa/singletons.hpp"
#include "cppa/binary_serializer.hpp"
#include "cppa/binary_deserializer.hpp"

#include "cppa/util/buffer.hpp"

#include "cppa/detail/raw_access.hpp"

#include "cppa/intrusive/single_reader_queue.hpp"

#include "cppa/io/coap_util.hpp"
#include "cppa/io/middleman.hpp"
#include "cppa/io/remote_actor_proxy.hpp"

using namespace std;

namespace cppa {
  
void coap_publish(cppa::actor whom, uint16_t port, const char* addr) {
//    coap_set_log_level(LOG_DEBUG);
    actor a{detail::raw_access::unsafe_cast(detail::raw_access::get(whom))};
    CPPA_LOGF_TRACE(CPPA_TARG(whom, to_string) << ", " << CPPA_MARG(aptr, get));
    if (!whom) return;
    get_actor_registry()->put(whom->id(), detail::raw_access::get(whom));
    coap_endpoint_t* interface{nullptr};
//    char addr_str[NI_MAXHOST] = "::"; // use addr instead
    coap_context_t* ctx = io::get_context((addr == nullptr ? "::" : addr),
                                          std::to_string(port).c_str(),
                                          &interface);
    if (!ctx || !interface) {
        coap_free_context(ctx);
        throw ios_base::failure("Cannot create socket");
    }
    auto mm = get_middleman();
    mm->run_later([mm, ctx, interface, whom] {
        auto new_peer = new io::transaction_based_peer(mm, ctx, interface,
                                                       nullptr, whom.address().id());
        get_middleman()->continue_reader(new_peer);
    });
}


actor coap_remote_actor(const char* host, uint16_t port) {
    auto tmp = detail::coap_remote_actor_impl(host, port);
    actor res;
    // actually safe, because remote_actor_impl throws on type mismatch
    detail::raw_access::unsafe_assign(res, tmp);
    return res;
}
    
namespace detail {
    
abstract_actor_ptr coap_remote_actor_impl(const char* host, uint16_t port) {
//    coap_set_log_level(LOG_DEBUG);
    coap_context_t  *ctx{nullptr};
    coap_address_t dst;
    coap_address_init(&dst);
    void *addrptr = NULL;
    char port_str[NI_MAXSERV] = "0";
    coap_endpoint_t* interface{nullptr};
    auto res = io::resolve_address(host, &dst.addr.sa);
    if (res < 0) {
        throw ios_base::failure("cannot resolve address of remote actor");
    }
    dst.size = res;
    dst.addr.sin.sin_port = htons(port);
    switch (dst.addr.sa.sa_family) {
        case AF_INET:
            addrptr = &dst.addr.sin.sin_addr;
            /* create context for IPv4 */
            ctx = io::get_context("0.0.0.0", port_str, &interface);
            break;
        case AF_INET6:
            addrptr = &dst.addr.sin6.sin6_addr;
            
            /* create context for IPv6 */
            ctx = io::get_context("::", port_str, &interface);
            break;
        default:
            ;
    }
    if (ctx == nullptr || interface == nullptr) {
        coap_free_context(ctx);
        throw ios_base::failure("Cannot create socket");
    }
    auto mm = get_middleman();
    // (handshake) send our node_id as CON message
    util::buffer snd_buf(COAP_MAX_PDU_SIZE, COAP_MAX_PDU_SIZE);
    coap_address_t remote;
    binary_serializer bs(&snd_buf, &(mm->get_namespace()));
    bs << message_header{};
    bs << make_any_tuple(atom("HANDSHAKE"), mm->node());
    auto pdu = coap_new_pdu();
    unsigned char token_data[8];
    str token = {0, token_data};
    io::generate_token(&token);
    pdu->hdr->type = COAP_MESSAGE_CON;
    pdu->hdr->id   = coap_new_message_id(ctx);
    pdu->hdr->code = 0x01; // todo change this
    pdu->hdr->token_length = token.length;
    coap_add_token(pdu, token.length, token.s);
    coap_add_data(pdu, snd_buf.size(),
                  reinterpret_cast<unsigned char*>(snd_buf.data()));
    CPPA_LOGF_DEBUG("[coap_remote_actor] starting handshake with CON message");
    auto tid = coap_send_confirmed(ctx, interface, &dst, pdu);
    // TODO: handle retransmit
    // receive ACK (may include their ids)
    unsigned char rcv_buf[COAP_MAX_PDU_SIZE];
    actor_id remote_aid{0};
    node_id_ptr node = nullptr;
    bool rcvd_ack = false;
    bool rcvd_ids = false;
    for(bool done = false; !done; done = rcvd_ack && rcvd_ids) {
        // ### wait ACK and IDs ###
        coap_address_init(&remote);
        auto bytes_read = coap_network_read(interface, &remote,
                                            rcv_buf, COAP_MAX_PDU_SIZE);
        if (bytes_read < 0) {
            throw runtime_error("waiting for handshake replay, "
                                     "but received empty packet");
        }
        coap_pdu_t* rcvd_msg = coap_pdu_init(0, 0, 0, bytes_read);
        if (!coap_pdu_parse(rcv_buf, bytes_read, rcvd_msg)) {
            CPPA_LOGF_DEBUG("[coap_remote_actor] malformed pdu");
            CPPA_LOGF_DEBUG("[coap_remote_actor] malformed pdu");
            continue;
        }
        coap_tid_t rcvd_tid;
        coap_transaction_id(&remote, pdu, &rcvd_tid);
        CPPA_LOGF_DEBUG("[coap_remote_actor] comparing transaction ids: "
                  << tid << " =?= " << rcvd_tid);
        size_t payload_size;
        unsigned char *payload;
        const uniform_type_info* m_meta_hdr = uniform_typeid<message_header>();
        const uniform_type_info* m_meta_msg = uniform_typeid<any_tuple>();
        switch (rcvd_msg->hdr->type) {
            case COAP_MESSAGE_CON:
                CPPA_LOGF_DEBUG("[coap_remote_actor] received CON");
                coap_send_ack(ctx, interface, &remote, rcvd_msg);
                break;
            case COAP_MESSAGE_ACK:
                CPPA_LOGF_DEBUG("[coap_remote_actor] received ACK");
                if (!rcvd_ack && rcvd_tid == tid) {
                    rcvd_ack = true;
                }
                break;
            default:
                // other messages are not expected during the handshake
                continue;
        }
        if (coap_get_data(rcvd_msg, &payload_size, &payload)) {
            CPPA_LOGF_DEBUG("[coap_remote_actor] msg has data");
            message_header hdr;
            any_tuple msg;
            binary_deserializer bd(payload, payload_size,
                                   &(mm->get_namespace()),
                                   nullptr);
            try {
                m_meta_hdr->deserialize(&hdr, &bd);
                m_meta_msg->deserialize(&msg, &bd);
            }
            catch (exception& e) {
                CPPA_LOGF_ERROR("exception during read_message: "
                                << detail::demangle(typeid(e))
                                << ", what(): " << e.what());
                continue;
            }
            CPPA_LOGF_DEBUG("deserialized: " << to_string(hdr)
                                             << " " << to_string(msg));
            match(msg) (
                on(atom("HANDSHAKE"), arg_match) >> [&](node_id_ptr node_id) {
                    CPPA_LOGF_DEBUG("[coap_remote_actor] received node '"
                         << to_string(node_id) << "'");
                    node = node_id;
                    rcvd_ids = true;
                },
                on(atom("HANDSHAKE"), arg_match) >> [&](actor_id aid,
                                                        node_id_ptr node_id) {
                    CPPA_LOGF_DEBUG("[coap_remote_actor] received '"
                         << aid << ", "
                         << to_string(node_id));
                    remote_aid = aid;
                    node = node_id;
                    rcvd_ids = true;
                },
                others() >> []() {
                    CPPA_LOGF_DEBUG("[coap_remote_actor] received unknown payload");
                });
        }
        else {
            CPPA_LOGF_DEBUG("[coap_remote_actor] has no data");
        }
        // todo handle retransmit
        coap_delete_pdu(rcvd_msg);
    }
    snd_buf.clear();
    // create new peer
    CPPA_LOGF_DEBUG("[coap_remote_actor] creating new peer");
    auto new_peer = new io::transaction_based_peer(mm, ctx, interface, nullptr);
    // add other node to our peer
    coap_address_t addr;
    coap_address_init(&addr);
    memcpy(&addr, &remote, sizeof(coap_address_t));
    new_peer->m_known_nodes.emplace(*node, move(addr));
//    auto pinfptr = make_counted<node_id>(peer_pid, peer_node_id);
    if (*(mm->node()) == *node) {
        // this is a local actor, not a remote actor
        CPPA_LOGF_WARNING("remote_actor() called to access a local actor");
        auto ptr = get_actor_registry()->get(remote_aid);
        return ptr;
    }
    struct remote_actor_result { remote_actor_result* next; actor value; };
    mutex qmtx;
    condition_variable qcv;
    intrusive::single_reader_queue<remote_actor_result> q;
    mm->run_later([mm, node, remote_aid, &q, &qmtx, &qcv, &new_peer] {
        CPPA_LOGC_TRACE("cppa", "remote_actor$create_connection", "");
        // todo: check if peer exists
        mm->continue_reader(new_peer);
        mm->register_peer(*node, new_peer);
        auto res = mm->get_namespace().get_or_put(node, remote_aid);
        q.synchronized_enqueue(qmtx, qcv, new remote_actor_result{0, res});
    });
    unique_ptr<remote_actor_result> result(q.synchronized_pop(qmtx, qcv));
    CPPA_LOGF_DEBUG(CPPA_MARG(result, get));
    CPPA_LOGF_DEBUG("[coap_remote_actor] actor is " << to_string(result->value));
    return raw_access::get(result->value);
}
    
} // namespace detail

} // namespace cppa
