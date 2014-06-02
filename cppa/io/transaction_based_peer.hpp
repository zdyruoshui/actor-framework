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

#ifndef TRANSACTION_BASED_PEER_HPP
#define TRANSACTION_BASED_PEER_HPP

#include <memory>

#include "coap.h"

#include "cppa/io/peer.hpp"

#include "cppa/io/transaction_reader.hpp"
#include "cppa/io/transaction_writer.hpp"

namespace cppa {
namespace io {

class transaction_based_peer : peer {
    
    using super = peer;

    friend class middleman_impl;
    
 public:
 
    transaction_based_peer(middleman* parent,
                           coap_context_t* ctx,
                           node_id_ptr peer_ptr = nullptr);
    
    void enqueue(msg_hdr_cref hdr, const any_tuple& msg) override;

    continue_reading_result continue_reading() override;

    continue_writing_result continue_writing() override;

    void dispose() override;

    void io_failed(event_bitmask mask) override;

    inline void enqueue(const any_tuple& msg) {
        enqueue({invalid_actor_addr, nullptr}, msg);
    }
    
 private:

    enum read_state {
        // connection just established; waiting for process information
        wait_for_process_info,
        // wait for the size of the next message
        wait_for_msg_size,
        // currently reading a message
        read_message
    };

    middleman* m_parent;
    coap_context_t* m_ctx;
    
    read_state m_state;


    const uniform_type_info* m_meta_hdr;
    const uniform_type_info* m_meta_msg;

    util::buffer m_rd_buf;
    util::buffer m_wr_buf;

    partial_function m_content_handler;

    type_lookup_table m_incoming_types;
    type_lookup_table m_outgoing_types;

    void monitor(const actor_addr& sender, const node_id_ptr& node, actor_id aid);

    void kill_proxy(const actor_addr& sender, const node_id_ptr& node, actor_id aid, std::uint32_t reason);

    void link(const actor_addr& sender, const actor_addr& ptr);

    void unlink(const actor_addr& sender, const actor_addr& ptr);

    void deliver(msg_hdr_cref hdr, any_tuple msg);

    void enqueue_impl(msg_hdr_cref hdr, const any_tuple& msg);

    void add_type_if_needed(const std::string& tname);

    int send_coap_message_with_type(msg_hdr_cref hdr, const any_tuple& msg, int type);

    coap_pdu_t* coap_new_request(coap_context_t *ctx, method_t m, coap_list_t *options);

    static unsigned char m_token_data[8];
    str m_the_token;
    coap_block_t m_block;
    int m_flags;

};

} // namspace io
} // namspace cppa


#endif // TRANSACTION_BASED_PEER_HPP
