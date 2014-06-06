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
}

transaction_based_peer::transaction_based_peer(middleman* parent,
                                               coap_context_t* ctx,
                                               coap_endpoint_t* interface,
                                               node_id_ptr peer_ptr)
        : super{interface->handle, interface->handle, peer_ptr}
        , m_parent{parent}
        , m_state{(peer_ptr) ? wait_for_msg_size : wait_for_process_info}
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
    coap_register_response_handler(m_ctx, message_handler);
    set_timeout(&m_max_wait, wait_seconds);
}
  
void transaction_based_peer::io_failed(event_bitmask) {
    CPPA_LOG_TRACE("node = " << (has_node() ? to_string(node()) : "nullptr")
                   << " mask = " << mask);
}

continue_reading_result transaction_based_peer::continue_reading() {
    CPPA_LOG_TRACE("");
    static unsigned char buf[COAP_MAX_PDU_SIZE];
    ssize_t bytes_read = -1;
    coap_address_t remote;
    coap_address_init(&remote);
    bytes_read = coap_network_read(m_interface, &remote, buf, sizeof(buf));
    if (bytes_read < 0) {
        CPPA_LOG_ERROR("coap_read: recvfrom\n");
    } else {
        coap_handle_message(m_ctx, m_interface, &remote, buf, (size_t)bytes_read);
    }
    return continue_reading_result::continue_later;
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
}

void transaction_based_peer::dispose() {
    CPPA_LOG_TRACE(CPPA_ARG(this));
    m_parent->get_namespace().erase(node());
    m_parent->del_peer(this);
    delete this;
}

void transaction_based_peer::send_coap_message(coap_address_t* dst,
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
