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

#include <stdexcept>

#include "cppa/cppa.hpp"
#include "cppa/binary_serializer.hpp"
#include "cppa/binary_deserializer.hpp"

#include "cppa/io/coap_util.hpp"
#include "cppa/io/transaction_based_peer.hpp"

using namespace std;

namespace cppa {
namespace io {

transaction_based_peer::coap_request::coap_request()
    : tid{0}, pdu{nullptr}
    , timeout{0}, the_token{0, token_data}
    , block{0, 0, 6}, obs_wait{0}
    , flags{0}, options{nullptr} {
    generate_token(&the_token, 8);
}

transaction_based_peer::coap_request::~coap_request() {
    coap_delete_list(options);
//    coap_free_context(ctx);
//    coap_free_endpoint(m_interface);
}

transaction_based_peer::transaction_based_peer(middleman* parent,
                                               coap_context_t* ctx,
                                               coap_endpoint_t* interface,
                                               node_id_ptr peer_ptr,
                                               actor_id aid)
        : super{interface->handle, interface->handle, peer_ptr}
        , m_parent{parent}
        , m_state{(peer_ptr) ? read_message : wait_for_process_info}
        , m_ctx{ctx}
        , m_default_msgtype{COAP_MESSAGE_CON}
        , m_default_method{1}
        , m_max_wait{0}
        , m_interface{interface}
        , m_options{nullptr}
        , m_actor{aid} {
    m_rd_buf.final_size( m_state == wait_for_process_info
                       ? sizeof(uint32_t) + node_id::host_id_size
                       : sizeof(uint32_t));
    m_meta_hdr = uniform_typeid<message_header>();
    m_meta_msg = uniform_typeid<any_tuple>();

    coap_set_app_data(m_ctx, this);
    coap_register_option(ctx, COAP_OPTION_BLOCK2);
    coap_register_response_handler(m_ctx, response_handler);
    set_timeout(&m_max_wait, wait_seconds);
    coap_resource_t *r = coap_resource_init(NULL, 0, 0);
    coap_add_attr(r,
                  (unsigned char *)"id", 2,
                  (unsigned char *)"f00", 3,
                  0);
    coap_add_resource(ctx, r);
}

transaction_based_peer::transaction_based_peer(middleman* parent,
                                               coap_context_t* ctx,
                                               coap_endpoint_t* interface,
                                               node_id_ptr peer_ptr)
        : super{interface->handle, interface->handle, peer_ptr}
        , m_parent{parent}
        , m_state{(peer_ptr) ? read_message : wait_for_process_info}
        , m_ctx{ctx}
        , m_default_msgtype{COAP_MESSAGE_CON}
        , m_default_method{1}
        , m_max_wait{0}
        , m_interface{interface}
        , m_options{nullptr}
        , m_actor{0} {
    m_rd_buf.final_size( m_state == wait_for_process_info
                       ? sizeof(uint32_t) + node_id::host_id_size
                       : sizeof(uint32_t));
    m_meta_hdr = uniform_typeid<message_header>();
    m_meta_msg = uniform_typeid<any_tuple>();

    coap_set_app_data(m_ctx, this);
    coap_register_option(ctx, COAP_OPTION_BLOCK2);
    coap_register_response_handler(m_ctx, response_handler);
    set_timeout(&m_max_wait, wait_seconds);
    coap_resource_t *r = coap_resource_init(NULL, 0, 0);
    coap_add_attr(r,
                  (unsigned char *)"id", 2,
                  (unsigned char *)"f00", 3,
                  0);
    coap_add_resource(ctx, r);
}
  
void transaction_based_peer::io_failed(event_bitmask) {
    CPPA_LOG_TRACE("node = " << (has_node() ? to_string(node()) : "nullptr")
                   << " mask = " << mask);
}

continue_reading_result transaction_based_peer::continue_reading() {
    CPPA_LOG_TRACE("");
    static unsigned char msg[COAP_MAX_PDU_SIZE];
    coap_address_t remote;
    coap_address_init(&remote);
    auto msg_len = coap_network_read(m_interface, &remote, msg, sizeof(msg));
    if (msg_len < 0) {
        CPPA_LOG_ERROR("coap_read: recvfrom\n");
        return continue_reading_result::continue_later;
    }
    // handle message, mostly from coap_dispatch
    coap_queue_t *node;
    // msg_len is > 0, otherwise there would have been an error earlier
    if (static_cast<size_t>(msg_len) < sizeof(coap_hdr_t) ) {
        CPPA_LOG_DEBUG("discarded invalid frame");
        return continue_reading_result::continue_later;
    }
    if (static_cast<size_t>(msg_len) < sizeof(coap_hdr_t) ) {
        CPPA_LOG_DEBUG("discarded invalid frame");
        return continue_reading_result::continue_later;
    }
    // check version identifier
    if (((*msg >> 6) & 0x03) != COAP_DEFAULT_VERSION) {
        CPPA_LOG_DEBUG("unknown protocol version " << ((*msg >> 6) & 0x03));
        return continue_reading_result::continue_later;
    }
    node = coap_new_node();
    if (!node) {
        CPPA_LOG_DEBUG("could not create node");
        return continue_reading_result::continue_later;
    }
    node->pdu = coap_pdu_init(0, 0, 0, msg_len);
    if (!node->pdu) {
        CPPA_LOG_DEBUG("could not initialize pdu");
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    }
    coap_ticks(&node->t);
    node->local_if = (coap_endpoint_t *) m_interface;
    memcpy(&node->remote, &remote, sizeof(coap_address_t));
    if (!coap_pdu_parse(msg, msg_len, node->pdu)) {
        CPPA_LOG_DEBUG("discard malformed PDU");
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    }
    // and add new node to receive queue
    coap_transaction_id(&node->remote, node->pdu, &node->id);
//#ifndef INET6_ADDRSTRLEN
//#define INET6_ADDRSTRLEN 40
//#endif
//    unsigned char addr[INET6_ADDRSTRLEN+8];
//    if (coap_print_addr(&remote, addr, INET6_ADDRSTRLEN+8) != 0) {
//        CPPA_LOG_DEBUG("received "<<
//                       (int)msg_len << " bytes from " << addr);
//    }
    // replaced call to coap_dispatch with the following
    coap_queue_t *sent = NULL;
    coap_pdu_t *response;
    coap_opt_filter_t opt_filter;
    auto cleanup = [&]() {
        coap_delete_node(sent);
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    };
    memset(opt_filter, 0, sizeof(coap_opt_filter_t));
    if (node->pdu->hdr->version != COAP_DEFAULT_VERSION) {
        debug("dropped packet with unknown version %u\n", node->pdu->hdr->version);
        CPPA_LOG_DEBUG("dropped packet with unknown version "
             << node->pdu->hdr->version);
        return cleanup();
    }
    CPPA_LOG_DEBUG("message type is:");
    switch (node->pdu->hdr->type) {
        case COAP_MESSAGE_ACK: {
            CPPA_LOG_DEBUG("ACK");
            auto req = m_requests.find(node->pdu->hdr->id);
            if (req != m_requests.end()) {
                CPPA_LOG_DEBUG("deleting request info")
                m_requests.erase(req);
            }
            /* find transaction in sendqueue to stop retransmission */
            coap_remove_from_queue(&m_ctx->sendqueue, node->id, &sent);
            if (node->pdu->hdr->code == 0) {
                return cleanup();
            }
            if (sent && COAP_RESPONSE_CLASS(sent->pdu->hdr->code) == 2) {
                const str token = { sent->pdu->hdr->token_length, sent->pdu->hdr->token };
                coap_touch_observer(m_ctx, &sent->remote, &token);
            }
            break;
        }

        case COAP_MESSAGE_RST: {
            CPPA_LOG_DEBUG("RST");
            coap_log(LOG_ALERT, "got RST for message %u\n", ntohs(node->pdu->hdr->id));
            coap_remove_from_queue(&m_ctx->sendqueue, node->id, &sent);
            if (sent) {
                CPPA_LOG_DEBUG("COAP_MESSAGE_RST with "
                               "sent not implemented");
            }
            return cleanup();
        }

        case COAP_MESSAGE_NON: { /* check for unknown critical options */
            CPPA_LOG_DEBUG("NON");
            if (coap_option_check_critical(m_ctx, node->pdu, opt_filter) == 0) {
                CPPA_LOG_DEBUG("NON msg + check_critical == 0");
                return cleanup();
            }
            break;
        }

        case COAP_MESSAGE_CON: { /* check for unknown critical options */
            CPPA_LOG_DEBUG("CON");
            if (coap_option_check_critical(m_ctx, node->pdu, opt_filter) == 0) {
                CPPA_LOG_DEBUG("check_critical == 0");
                /* FIXME: send response only if we have received a request. Otherwise,
                 * send RST. */
                response = coap_new_error_response(node->pdu,
                                                   COAP_RESPONSE_CODE(402),
                                                   opt_filter);

                if (!response)
                    warn("coap_dispatch: cannot create error reponse\n");
                else {
                    if (coap_send(m_ctx, node->local_if, &node->remote, response)
                                                                == COAP_INVALID_TID) {
                        warn("coap_dispatch: error sending reponse\n");
                    }
                    coap_delete_pdu(response);
                }
                return cleanup();
            }
            break;
        }
    }
    CPPA_LOG_DEBUG("passing message to handler");
    /* Pass message to upper layer if a specific handler was
     * registered for a request that should be handled locally. */
    if (COAP_MESSAGE_IS_REQUEST(node->pdu->hdr)) {
        CPPA_LOG_DEBUG("received request");
        request_handler(m_ctx,
                        node->local_if,
                       &node->remote,
                        sent ? sent->pdu : NULL,
                        node->pdu,
                        node->id);
    }
    else if (COAP_MESSAGE_IS_RESPONSE(node->pdu->hdr)) {
        CPPA_LOG_DEBUG("received response");
        response_handler(m_ctx,
                         node->local_if,
                        &node->remote,
                         sent ? sent->pdu : NULL,
                         node->pdu,
                         node->id);
    }
    else {
        CPPA_LOG_DEBUG("received massage with invalid code");
        coap_send_message_type(m_ctx, node->local_if,
                               &node->remote,
                               node->pdu,
                               COAP_MESSAGE_RST);
    }

    return cleanup();
}

void transaction_based_peer::monitor(const actor_addr&,
                                     const node_id_ptr&,
                                     actor_id) {
    CPPA_LOG_TRACE(CPPA_MARG(node, get) << ", " << CPPA_ARG(aid));
}

void transaction_based_peer::kill_proxy(const actor_addr&,
                      const node_id_ptr&,
                      actor_id,
                      uint32_t) {
    CPPA_LOG_TRACE(CPPA_TARG(sender, to_string)
                   << ", node = " << (node ? to_string(*node) : "-invalid-")
                   << ", " << CPPA_ARG(aid)
                   << ", " << CPPA_ARG(reason));
}

void transaction_based_peer::link(const actor_addr& lhs, const actor_addr& rhs) {
    CPPA_LOG_TRACE(CPPA_TARG(lhs, to_string) << ", "
                   << CPPA_TARG(rhs, to_string));
    CPPA_LOG_ERROR_IF(!lhs, "received 'LINK' from invalid sender");
    CPPA_LOG_ERROR_IF(!rhs, "received 'LINK' with invalid receiver");
}

void transaction_based_peer::unlink(const actor_addr& lhs, const actor_addr& rhs) {
    CPPA_LOG_TRACE(CPPA_TARG(lhs, to_string) << ", "
                   << CPPA_TARG(rhs, to_string));
    CPPA_LOG_ERROR_IF(!lhs, "received 'UNLINK' from invalid sender");
    CPPA_LOG_ERROR_IF(!rhs, "received 'UNLINK' with invalid target");
}

continue_writing_result transaction_based_peer::continue_writing() {
    CPPA_LOG_TRACE("");
    throw logic_error("continue writing called in transaction based peer");
}

bool transaction_based_peer::has_unwritten_data() const {
    CPPA_LOG_TRACE("");
    return false;
}

void transaction_based_peer::enqueue(msg_hdr_cref hdr, const any_tuple& msg) {
    util::buffer wbuf;
    binary_serializer bs(&wbuf, &(m_parent->get_namespace()), nullptr);
    try { bs << hdr << msg; }
    catch (exception& e) {
        CPPA_LOG_ERROR(to_verbose_string(e));
        cerr << "*** exception in stream_based_peer::enqueue; "
                  << to_verbose_string(e)
                  << endl;
        return;
    }
    CPPA_LOG_DEBUG("serialized: " << to_string(hdr) << " " << to_string(msg));
    // todo send message
    if (hdr.receiver) {
        auto ptr = dynamic_cast<abstract_actor*>(&(*hdr.receiver));
        if (ptr) {
            auto node = ptr->node();
            auto addr = m_known_nodes.find(node);
            if (addr != m_known_nodes.end()) {
                CPPA_LOG_DEBUG("sending");
                // todo no magic numbers
                send_coap_message(&addr->second, wbuf.data(), wbuf.size(),
                                  m_options, COAP_MESSAGE_NON,m_default_method);
            }
            else {
                CPPA_LOG_DEBUG("unknown destination");
            }
        }
    }
    else {
        CPPA_LOG_DEBUG("empty receiver");
    }
    CPPA_LOG_DEBUG("end");
}

void transaction_based_peer::dispose() {
    CPPA_LOG_TRACE(CPPA_ARG(this));
    m_parent->get_namespace().erase(node());
    m_parent->del_peer(this);
    delete this;
}

void transaction_based_peer::send_coap_message(const coap_address_t* dst,
                                               void* payload, size_t size,
                                               coap_list_t* options,
                                               int type, unsigned char method) {
    auto req = new_request(m_ctx, method, options, payload, size);

    coap_tid_t tid;
    if (type == COAP_MESSAGE_CON) {
        tid = coap_send_confirmed(m_ctx,
                                  m_interface,
                                  dst, req.pdu);
    }
    else {
        tid = coap_send(m_ctx,
                        m_interface,
                        dst, req.pdu);
    }
    if (req.pdu->hdr->type != COAP_MESSAGE_CON || tid == COAP_INVALID_TID) {
        coap_delete_pdu(req.pdu);
    }
    else {
        coap_ticks(&req.timeout);
        req.timeout += wait_seconds * COAP_TICKS_PER_SECOND;
        m_requests.emplace(req.pdu->hdr->id, move(req));
    }
    if (tid == COAP_INVALID_TID) {
        throw ios_base::failure("could not send coap message");
    }
}

} // namespace io
} // namespace cppa
