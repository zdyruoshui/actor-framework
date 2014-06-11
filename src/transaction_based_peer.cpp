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
                                               node_id_ptr peer_ptr)
        : super{interface->handle, interface->handle, peer_ptr}
        , m_parent{parent}
        , m_state{(peer_ptr) ? read_message : wait_for_process_info}
        , m_ctx{ctx}
        , m_default_msgtype{COAP_MESSAGE_CON}
        , m_default_method{1}
        , m_max_wait{0}
        , m_interface{interface}
        , m_options{nullptr} {
    m_rd_buf.final_size( m_state == wait_for_process_info
                       ? sizeof(uint32_t) + node_id::host_id_size
                       : sizeof(uint32_t));
    m_meta_hdr = uniform_typeid<message_header>();
    m_meta_msg = uniform_typeid<any_tuple>();

    coap_set_app_data(m_ctx, this);
    coap_register_option(ctx, COAP_OPTION_BLOCK2);
    coap_register_response_handler(m_ctx, response_handler);
    set_timeout(&m_max_wait, wait_seconds);


//    std::stringstream s;
//    auto pinf = m_parent->node();
//    std::uint32_t process_id = pinf->process_id();
//    s << process_id << "-" << to_string(pinf->host_id());
//    std::cout << "my ids:" << s.str() << std::endl;
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
    std::cout << "[continue_reading] rcvd " << msg_len << " bytes" << std::endl;
//    coap_handle_message(m_ctx, m_interface, &remote, msg, static_cast<size_t>(msg_len));

//    int coap_handle_message(coap_context_t *ctx,
//                            const coap_endpoint_t *local_interface,
//                            const coap_address_t *remote, unsigned char *msg,
//                            size_t msg_len)
    coap_queue_t *node;
    // msg_len is > 0, otherwise there would have been an error earlier
    if (static_cast<size_t>(msg_len) < sizeof(coap_hdr_t) ) {
        CPPA_LOG_DEBUG("coap_handle_message: discarded invalid frame");
        return continue_reading_result::continue_later;
    }
    if (static_cast<size_t>(msg_len) < sizeof(coap_hdr_t) ) {
        CPPA_LOG_DEBUG("coap_handle_message: discarded invalid frame");
        return continue_reading_result::continue_later;
    }
    // check version identifier
    if (((*msg >> 6) & 0x03) != COAP_DEFAULT_VERSION) {
        CPPA_LOG_DEBUG("coap_read: unknown protocol version " << ((*msg >> 6) & 0x03));
        return continue_reading_result::continue_later;
    }
    node = coap_new_node();
    if (!node) {
        return continue_reading_result::continue_later;
    }
    node->pdu = coap_pdu_init(0, 0, 0, msg_len);
    if (!node->pdu) {
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    }
    coap_ticks(&node->t);
    node->local_if = (coap_endpoint_t *) m_interface;
    memcpy(&node->remote, &remote, sizeof(coap_address_t));
    if (!coap_pdu_parse(msg, msg_len, node->pdu)) {
        warn("discard malformed PDU\n");
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    }
    // and add new node to receive queue
    coap_transaction_id(&node->remote, node->pdu, &node->id);
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 40
#endif
    unsigned char addr[INET6_ADDRSTRLEN+8];
    if (coap_print_addr(&remote, addr, INET6_ADDRSTRLEN+8)) {
        CPPA_LOG_DEBUG("[continue_reading] received "<< (int)msg_len << " bytes from " << addr);
//        debug("** received %d bytes from %s:\n", (int)msg_len, addr);
    }
    coap_show_pdu(node->pdu);
    // replaced call to coap_dispatch(coap_context_t *context, coap_queue_t *rcvd)
    // with the following
    coap_queue_t *sent = NULL;
//    coap_pdu_t *response;
    coap_opt_filter_t opt_filter;
    auto cleanup = [&]() {
        coap_delete_node(sent);
        coap_delete_node(node);
        return continue_reading_result::continue_later;
    };

    memset(opt_filter, 0, sizeof(coap_opt_filter_t));

    if (node->pdu->hdr->version != COAP_DEFAULT_VERSION) {
        debug("dropped packet with unknown version %u\n", node->pdu->hdr->version);
        return cleanup();
    }

    switch (node->pdu->hdr->type) {
    case COAP_MESSAGE_ACK:
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

    case COAP_MESSAGE_RST:
        coap_log(LOG_ALERT, "got RST for message %u\n", ntohs(node->pdu->hdr->id));
        coap_remove_from_queue(&m_ctx->sendqueue, node->id, &sent);
        if (sent) {
            CPPA_LOG_DEBUG("[continue_reading] COAP_MESSAGE_RST with "
                           "sent not implemented");
//            coap_resource_t *r, *tmp;
//            str token = { 0, NULL };
//            /* remove observer for this resource, if any
//             * get token from sent and try to find a matching resource. Uh!
//             */
//            COAP_SET_STR(&token, sent->pdu->hdr->token_length, sent->pdu->hdr->token);
//            HASH_ITER(hh, context->resources, r, tmp) {
//                coap_delete_observer(r, &sent->remote, &token);
//                coap_cancel_all_messages(context, &sent->remote, token.s, token.length);
//            }
        }
        return cleanup();

    case COAP_MESSAGE_NON: /* check for unknown critical options */
        if (coap_option_check_critical(m_ctx, node->pdu, opt_filter) == 0) {
            return cleanup();
        }
        break;

    case COAP_MESSAGE_CON: /* check for unknown critical options */
        CPPA_LOG_DEBUG("[continue_reading] received COAP_MESSAGE_CON");
        CPPA_LOG_DEBUG("[continue_reading] passing to message handler");
//        if (coap_option_check_critical(m_ctx, node->pdu, opt_filter) == 0) {
//            response = coap_new_error_response(node->pdu,
//                                               COAP_RESPONSE_CODE(402),
//                                               opt_filter);
//            if (!response) {
//                warn("coap_dispatch: cannot create error reponse\n");
//            }
//            else {
//                if (coap_send(m_ctx, node->local_if, &node->remote, response) ==
//                    COAP_INVALID_TID) {
//                    warn("coap_dispatch: error sending reponse\n");
//                }
//                coap_delete_pdu(response);
//            }
//            return cleanup();
//        }
        break;
    }

    /* Pass message to upper layer if a specific handler was
     * registered for a request that should be handled locally. */
    if (COAP_MESSAGE_IS_REQUEST(node->pdu->hdr)) {
        CPPA_LOG_DEBUG("[continue_reading] received request");
        request_handler(m_ctx,
                        node->local_if,
                       &node->remote,
                        sent ? sent->pdu : NULL,
                        node->pdu,
                        node->id);
    }
    else if (COAP_MESSAGE_IS_RESPONSE(node->pdu->hdr)) {
        CPPA_LOG_DEBUG("[continue_reading] received response");
        response_handler(m_ctx,
                         node->local_if,
                        &node->remote,
                         sent ? sent->pdu : NULL,
                         node->pdu,
                         node->id);
    }
    else {
        CPPA_LOG_DEBUG("[continue_reading] received massage with invalid code");
        debug("dropped message with invalid code\n");
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
                      std::uint32_t) {
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
    throw std::logic_error("continue writing called in transaction based peer");
}

bool transaction_based_peer::has_unwritten_data() const {
    CPPA_LOG_TRACE("");
    return false;
}

void transaction_based_peer::enqueue(msg_hdr_cref hdr, const any_tuple& msg) {
    uint32_t size = 0;
    util::buffer wbuf;
    auto before = static_cast<uint32_t>(wbuf.size());
    binary_serializer bs(&wbuf, &(m_parent->get_namespace()), nullptr);
    wbuf.write(sizeof(uint32_t), &size);
    try { bs << hdr << msg; }
    catch (std::exception& e) {
        CPPA_LOG_ERROR(to_verbose_string(e));
        std::cerr << "*** exception in stream_based_peer::enqueue; "
                  << to_verbose_string(e)
                  << std::endl;
        return;
    }
    CPPA_LOG_DEBUG("serialized: " << to_string(hdr) << " " << to_string(msg));
    size =   static_cast<std::uint32_t>((wbuf.size() - before))
           - static_cast<std::uint32_t>(sizeof(std::uint32_t));
    // update size in buffer
    memcpy(wbuf.offset_data(before), &size, sizeof(std::uint32_t));
    // todo send message
    // match to destination somehow
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
        m_requests.emplace(req.pdu->hdr->id, std::move(req));
    }
    if (tid == COAP_INVALID_TID) {
        throw std::ios_base::failure("could not send coap message");
    }
}

} // namespace io
} // namespace cppa
