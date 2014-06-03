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
  
transaction_based_peer::transaction_based_peer(middleman* parent,
                                               coap_context_t* ctx,
                                               node_id_ptr peer_ptr)
        : super{ctx->sockfd, ctx->sockfd, peer_ptr}
        , m_parent{parent}
        , m_state{(peer_ptr) ? wait_for_msg_size : wait_for_process_info}
        , coap{ctx} {
    m_rd_buf.final_size( m_state == wait_for_process_info
                       ? sizeof(uint32_t) + node_id::host_id_size
                       : sizeof(uint32_t));
    // state == wait_for_msg_size iff peer was created using remote_peer()
    // in this case, this peer must be erased if no proxy of it remains
    // stop_on_last_proxy_exited(m_state == wait_for_msg_size);
    m_meta_hdr = uniform_typeid<message_header>();
    m_meta_msg = uniform_typeid<any_tuple>();

    coap_register_option(ctx, COAP_OPTION_BLOCK2);
    coap_register_response_handler(ctx, message_handler);
}
  
void transaction_based_peer::io_failed(event_bitmask mask) {
    CPPA_LOG_TRACE("node = " << (has_node() ? to_string(node()) : "nullptr")
                   << " mask = " << mask);
//    // make sure this code is executed only once by filtering for read failure
//    if (mask == event::read && has_node()) {
//        // kill all proxies
//        auto& children = m_parent->get_namespace().proxies(node());
//        for (auto& kvp : children) {
//            auto ptr = kvp.second.promote();
//            if (ptr) {
//                send_as(ptr, ptr, atom("KILL_PROXY"),
//                        exit_reason::remote_link_unreachable);
//            }
//        }
//        m_parent->get_namespace().erase(node());
//    }
}

continue_reading_result transaction_based_peer::continue_reading() {
    CPPA_LOG_TRACE("");
//    for (;;) {
//        try { m_rd_buf.append_from(m_in.get()); }
//        catch (std::exception&) {
//            return continue_reading_result::failure;
//        }
//        if (!m_rd_buf.full()) {
//            // try again later
//            return continue_reading_result::continue_later;
//        }
//        switch (m_state) {
//            case wait_for_process_info: {
//                //DEBUG("peer_connection::continue_reading: "
//                //      "wait_for_process_info");
//                uint32_t process_id;
//                node_id::host_id_type host_id;
//                memcpy(&process_id, m_rd_buf.data(), sizeof(uint32_t));
//                memcpy(host_id.data(), m_rd_buf.offset_data(sizeof(uint32_t)),
//                       node_id::host_id_size);
//                set_node(new node_id(process_id, host_id));
//                if (*m_parent->node() == node()) {
//                    std::cerr << "*** middleman warning: "
//                                 "incoming connection from self"
//                              << std::endl;
//                    return continue_reading_result::failure;
//                }
//                CPPA_LOG_DEBUG("read process info: " << to_string(node()));
//                if (!m_parent->register_peer(node(), this)) {
//                    CPPA_LOG_ERROR("multiple incoming connections "
//                                   "from the same node");
//                    return continue_reading_result::failure;
//                }
//                // initialization done
//                m_state = wait_for_msg_size;
//                m_rd_buf.clear();
//                m_rd_buf.final_size(sizeof(uint32_t));
//                break;
//            }
//            case wait_for_msg_size: {
//                //DEBUG("peer_connection::continue_reading: wait_for_msg_size");
//                uint32_t msg_size;
//                memcpy(&msg_size, m_rd_buf.data(), sizeof(uint32_t));
//                m_rd_buf.clear();
//                m_rd_buf.final_size(msg_size);
//                m_state = read_message;
//                break;
//            }
//            case read_message: {
//                //DEBUG("peer_connection::continue_reading: read_message");
//                message_header hdr;
//                any_tuple msg;
//                binary_deserializer bd(m_rd_buf.data(),
//                                       m_rd_buf.size(),
//                                       &(m_parent->get_namespace()),
//                                       &m_incoming_types);
//                try {
//                    m_meta_hdr->deserialize(&hdr, &bd);
//                    m_meta_msg->deserialize(&msg, &bd);
//                }
//                catch (std::exception& e) {
//                    CPPA_LOG_ERROR("exception during read_message: "
//                                   << detail::demangle(typeid(e))
//                                   << ", what(): " << e.what());
//                    return continue_reading_result::failure;
//                }
//                CPPA_LOG_DEBUG("deserialized: " << to_string(hdr) << " " << to_string(msg));
//                match(msg) (
//                    // monitor messages are sent automatically whenever
//                    // actor_proxy_cache creates a new proxy
//                    // note: aid is the *original* actor id
//                    on(atom("MONITOR"), arg_match) >> [&](const node_id_ptr& node, actor_id aid) {
//                        monitor(hdr.sender, node, aid);
//                    },
//                    on(atom("KILL_PROXY"), arg_match) >> [&](const node_id_ptr& node, actor_id aid, std::uint32_t reason) {
//                        kill_proxy(hdr.sender, node, aid, reason);
//                    },
//                    on(atom("LINK"), arg_match) >> [&](const actor_addr& ptr) {
//                        link(hdr.sender, ptr);
//                    },
//                    on(atom("UNLINK"), arg_match) >> [&](const actor_addr& ptr) {
//                        unlink(hdr.sender, ptr);
//                    },
//                    on(atom("ADD_TYPE"), arg_match) >> [&](std::uint32_t id, const std::string& name) {
//                        auto imap = get_uniform_type_info_map();
//                        auto uti = imap->by_uniform_name(name);
//                        m_incoming_types.emplace(id, uti);
//                    },
//                    others() >> [&] {
//                        deliver(hdr, std::move(msg));
//                    }
//                );
//                m_rd_buf.clear();
//                m_rd_buf.final_size(sizeof(uint32_t));
//                m_state = wait_for_msg_size;
//                break;
//            }
//        }
//        // try to read more (next iteration)
//    }
}

void transaction_based_peer::monitor(const actor_addr&,
                   const node_id_ptr& node,
                   actor_id aid) {
    CPPA_LOG_TRACE(CPPA_MARG(node, get) << ", " << CPPA_ARG(aid));
//    if (!node) {
//        CPPA_LOG_ERROR("received MONITOR from invalid peer");
//        return;
//    }
//    auto entry = get_actor_registry()->get_entry(aid);
//    auto pself = m_parent->node();

//    if (*node == *pself) {
//        CPPA_LOG_ERROR("received 'MONITOR' from pself");
//    }
//    else if (entry.first == nullptr) {
//        if (entry.second == exit_reason::not_exited) {
//            CPPA_LOG_ERROR("received MONITOR for unknown "
//                           "actor id: " << aid);
//        }
//        else {
//            CPPA_LOG_DEBUG("received MONITOR for an actor "
//                           "that already finished "
//                           "execution; reply KILL_PROXY");
//            // this actor already finished execution;
//            // reply with KILL_PROXY message
//            // get corresponding peer
//            enqueue(make_any_tuple(atom("KILL_PROXY"), pself, aid, entry.second));
//        }
//    }
//    else {
//        CPPA_LOG_DEBUG("attach functor to " << entry.first.get());
//        auto mm = m_parent();
//        entry.first->attach_functor([=](uint32_t reason) {
//            mm->run_later([=] {
//                CPPA_LOGC_TRACE("cppa::io::peer",
//                                "monitor$kill_proxy_helper",
//                                "reason = " << reason);
//                auto p = mm->get_peer(*node);
//                if (p) p->enqueue(make_any_tuple(atom("KILL_PROXY"), pself, aid, reason));
//            });
//        });
//    }
}

void transaction_based_peer::kill_proxy(const actor_addr& sender,
                      const node_id_ptr& node,
                      actor_id aid,
                      std::uint32_t reason) {
    CPPA_LOG_TRACE(CPPA_TARG(sender, to_string)
                   << ", node = " << (node ? to_string(*node) : "-invalid-")
                   << ", " << CPPA_ARG(aid)
                   << ", " << CPPA_ARG(reason));
//    if (!node) {
//        CPPA_LOG_ERROR("node = nullptr");
//        return;
//    }
//    if (sender != nullptr) {
//        CPPA_LOG_ERROR("sender != nullptr");
//        return;
//    }
//    auto proxy = m_parent->get_namespace().get(*node, aid);
//    if (proxy) {
//        CPPA_LOG_DEBUG("received KILL_PROXY for " << aid
//                       << ":" << to_string(*node));
//        send_as(proxy, proxy, atom("KILL_PROXY"), reason);
//    }
//    else {
//        CPPA_LOG_INFO("received KILL_PROXY for " << aid
//                      << ":" << to_string(*node)
//                      << "but didn't found a matching instance "
//                      << "in proxy cache");
//    }
}
  
void transaction_based_peer::deliver(msg_hdr_cref hdr, any_tuple msg) {
    CPPA_LOG_TRACE("");
//    if (hdr.sender && hdr.sender.is_remote()) {
//        // is_remote() is guaranteed to return true if and only if
//        // the actor is derived from actor_proxy, hence we do not
//        // need to use a dynamic_cast here
//        auto ptr = static_cast<actor_proxy*>(detail::raw_access::get(hdr.sender));
//        ptr->deliver(hdr, std::move(msg));
//    }
//    else hdr.deliver(std::move(msg));
}

void transaction_based_peer::link(const actor_addr& lhs, const actor_addr& rhs) {
    // this message is sent from default_actor_proxy in link_to and
    // establish_backling to cause the original actor (sender) to establish
    // a link to ptr as well
    CPPA_LOG_TRACE(CPPA_TARG(lhs, to_string) << ", "
                   << CPPA_TARG(rhs, to_string));
    CPPA_LOG_ERROR_IF(!lhs, "received 'LINK' from invalid sender");
    CPPA_LOG_ERROR_IF(!rhs, "received 'LINK' with invalid receiver");
//    if (!lhs || !rhs) return;
//    auto locally_link_proxy = [](const actor_addr& proxy, const actor_addr& addr) {
//        // again, no need to to use a dynamic_cast here
//        auto ptr = static_cast<actor_proxy*>(detail::raw_access::get(proxy));
//        ptr->local_link_to(addr);
//    };
//    switch ((lhs.is_remote() ? 0x10 : 0x00) | (rhs.is_remote() ? 0x01 : 0x00)) {
//        case 0x00: // both local
//        case 0x11: // both remote
//            detail::raw_access::get(lhs)->link_to(rhs);
//            break;
//        case 0x10: // sender is remote
//            locally_link_proxy(lhs, rhs);
//            break;
//        case 0x01: // receiver is remote
//            locally_link_proxy(rhs, lhs);
//            break;
//        default: CPPA_LOG_ERROR("logic error");
//    }
}

void transaction_based_peer::unlink(const actor_addr& lhs, const actor_addr& rhs) {
    CPPA_LOG_TRACE(CPPA_TARG(lhs, to_string) << ", "
                   << CPPA_TARG(rhs, to_string));
    CPPA_LOG_ERROR_IF(!lhs, "received 'UNLINK' from invalid sender");
    CPPA_LOG_ERROR_IF(!rhs, "received 'UNLINK' with invalid target");
//    if (!lhs || !rhs) return;
//    auto locally_unlink_proxy = [](const actor_addr& proxy, const actor_addr& addr) {
//        // again, no need to to use a dynamic_cast here
//        auto ptr = static_cast<actor_proxy*>(detail::raw_access::get(proxy));
//        ptr->local_unlink_from(addr);
//    };
//    switch ((lhs.is_remote() ? 0x10 : 0x00) | (rhs.is_remote() ? 0x01 : 0x00)) {
//        case 0x00: // both local
//        case 0x11: // both remote
//            detail::raw_access::get(lhs)->unlink_from(rhs);
//            break;
//        case 0x10: // sender is remote
//            locally_unlink_proxy(lhs, rhs);
//            break;
//        case 0x01: // receiver is remote
//            locally_unlink_proxy(rhs, lhs);
//            break;
//        default: CPPA_LOG_ERROR("logic error");
//    }
}

continue_writing_result transaction_based_peer::continue_writing() {
    CPPA_LOG_TRACE("");
    throw std::logic_error("continue writing called in transaction based peer");
}

void transaction_based_peer::add_type_if_needed(const std::string& tname) {
//    if (m_outgoing_types.id_of(tname) == 0) {
//        auto id = m_outgoing_types.max_id() + 1;
//        auto imap = get_uniform_type_info_map();
//        auto uti = imap->by_uniform_name(tname);
//        m_outgoing_types.emplace(id, uti);
//        enqueue_impl({invalid_actor_addr, nullptr}, make_any_tuple(atom("ADD_TYPE"), id, tname));
//    }
}

void transaction_based_peer::enqueue_impl(msg_hdr_cref hdr, const any_tuple& msg) {
    CPPA_LOG_TRACE("");
//    auto tname = msg.tuple_type_names();
//    add_type_if_needed((tname) ? *tname : detail::get_tuple_type_names(*msg.vals()));
//    uint32_t size = 0;
//    auto& wbuf = write_buffer();
//    auto before = static_cast<uint32_t>(wbuf.size());
//    binary_serializer bs(&wbuf, &(parent()->get_namespace()), &m_outgoing_types);
//    wbuf.write(sizeof(uint32_t), &size);
//    try { bs << hdr << msg; }
//    catch (std::exception& e) {
//        CPPA_LOG_ERROR(to_verbose_string(e));
//        std::cerr << "*** exception in stream_based_peer::enqueue; "
//                  << to_verbose_string(e)
//                  << std::endl;
//        return;
//    }
//    CPPA_LOG_DEBUG("serialized: " << to_string(hdr) << " " << to_string(msg));
//    size =   static_cast<std::uint32_t>((wbuf.size() - before))
//           - static_cast<std::uint32_t>(sizeof(std::uint32_t));
//    // update size in buffer
//    memcpy(wbuf.offset_data(before), &size, sizeof(std::uint32_t));
}

void transaction_based_peer::enqueue(msg_hdr_cref hdr, const any_tuple& msg) {
//    enqueue_impl(hdr, msg);
//    register_for_writing();
}

void transaction_based_peer::dispose() {
//    CPPA_LOG_TRACE(CPPA_ARG(this));
//    m_parent->get_namespace().erase(node());
//    m_parent->del_peer(this);
//    delete this;
}

int transaction_based_peer::send_coap_message(coap_address_t& dst,
                                              void* data, size_t size,
                                              coap_list_t* options,
                                              int type, method_t method) {
    coap_pdu_t *pdu;
    coap_list_t *opt;

    if (!(pdu = coap_new_pdu())) {
        throw std::runtime_error("failed to create coap pdu");
    }

    pdu->hdr->type = msgtype;
    pdu->hdr->id = coap_new_message_id(m_coap_scope.ctx);
    pdu->hdr->code = method;

    pdu->hdr->token_length = m_coap_scope.the_token.length;
    if (!coap_add_token(pdu, m_coap_scope.the_token.length, m_coap_scope.the_token.s)) {
      debug("cannot add token to request\n");
    }

    coap_show_pdu(pdu);

    for (opt = options; opt; opt = opt->next) {
      coap_add_option(pdu,
                      COAP_OPTION_KEY(*(coap_option *)opt->data),
                      COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                      COAP_OPTION_DATA(*(coap_option *)opt->data));
    }

    if (size > 0) {
      if ((m_coap_scope.flags & FLAGS_BLOCK) == 0) {
          coap_add_data(pdu, size, (unsigned char *) data);
      }
      else {
          coap_add_block(pdu, size, (unsigned char *) data,
                         m_coap_scope.block.num, m_coap_scope.block.szx);
      }
    }

    coap_tid_t tid;
    if (type == COAP_MESSAGE_CON) {
        tid = coap_send_confirmed(m_coap_scope.ctx, &dst, pdu);
    }
    else {
      tid = coap_send(m_coap_scope.ctx, &dst, pdu);
    }
    if (pdu->hdr->type != COAP_MESSAGE_CON || tid == COAP_INVALID_TID) {
        coap_delete_pdu(pdu);
    }
    else {
        coap_tick_t timeout;
        coap_ticks(&timeout);
        timeout += wait_seconds * COAP_TICKS_PER_SECOND;
        m_requests.emplace(pdu->hdr->id, coap_request{tid, pdu, timeout});
    }
}

} // namespace io
} // namespace cppa
