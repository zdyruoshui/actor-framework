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

namespace cppa {
  
void coap_publish(cppa::actor whom, std::uint16_t port, const char* addr) {
    coap_set_log_level(LOG_DEBUG);
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
        throw std::ios_base::failure("Cannot create socket");
    }
    auto mm = get_middleman();
    mm->run_later([mm, ctx, interface] {
        auto new_peer = new io::transaction_based_peer(mm, ctx, interface, nullptr);
        get_middleman()->continue_reader(new_peer);
    });
}


actor coap_remote_actor(const char* host, std::uint16_t port) {
    auto tmp = detail::coap_remote_actor_impl(host, port);
    actor res;
    // actually safe, because remote_actor_impl throws on type mismatch
    detail::raw_access::unsafe_assign(res, tmp);
    return res;
}
    
namespace detail {
    
abstract_actor_ptr coap_remote_actor_impl(const char* host, std::uint16_t port) {
    coap_set_log_level(LOG_DEBUG);
    coap_context_t  *ctx{nullptr};
    coap_address_t dst;
    coap_address_init(&dst);
    void *addrptr = NULL;
    char port_str[NI_MAXSERV] = "0";
    coap_endpoint_t* interface{nullptr};
    auto res = io::resolve_address(host, &dst.addr.sa);
    if (res < 0) {
        throw std::ios_base::failure("cannot resolve address of remote actor");
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
        throw std::ios_base::failure("Cannot create socket");
    }

    auto mm = get_middleman();

    // ############### handshake ###############
    // ### send our info ###
    unsigned char rcv_buf[COAP_MAX_PDU_SIZE];
    util::buffer snd_buf(COAP_MAX_PDU_SIZE,COAP_MAX_PDU_SIZE);
    ssize_t bytes_read = -1;
    coap_address_t remote;

    coap_list_t *optlist = nullptr;
    // static coap_uri_t uri;
    // static str proxy = { 0, nullptr };
    // // static unsigned short proxy_port = COAP_DEFAULT_PORT;
    // std::string teh_uri("coap://[::1]/");
    // io::parse_uri(teh_uri.c_str(), proxy, optlist, uri);

    // char addr[INET6_ADDRSTRLEN];
    // if (!proxy.length && addrptr
    //      && (inet_ntop(dst.addr.sa.sa_family, addrptr, addr, sizeof(addr)) != 0)
    //      && (strlen(addr) != uri.host.length
    //      || memcmp(addr, uri.host.s, uri.host.length) != 0)) {
    //      /* add Uri-Host */
    //     coap_insert(&optlist,
    //                 io::new_option_node(COAP_OPTION_URI_HOST,
    //                                     uri.host.length, uri.host.s),
    //                                     io::order_opts);
    // }

    // send CON message
    auto pinf = mm->node();
    std::uint32_t process_id = pinf->process_id();
    binary_serializer bs(&snd_buf);
    bs.write_raw(sizeof(process_id), reinterpret_cast<void*>(&process_id));
    bs.write_raw(pinf->host_id().size(), pinf->host_id().data());
    auto pdu = coap_new_pdu();
    unsigned char token_data[8];
    str token = {0, token_data};
    io::generate_token(&token);
    std::cout << "token '" << token.s << "' (" << token.length << " bytes)" << std::endl;
    pdu->hdr->type = COAP_MESSAGE_CON;
    pdu->hdr->id   = coap_new_message_id(ctx);
    pdu->hdr->code = 0x01; // todo change this
    pdu->hdr->token_length = token.length;
    coap_add_token(pdu, token.length, token.s);
    coap_show_pdu(pdu);
    for (coap_list_t* opt = optlist; opt; opt = opt->next) {
        coap_add_option(pdu, COAP_OPTION_KEY   (*(coap_option *)opt->data),
                             COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                             COAP_OPTION_DATA  (*(coap_option *)opt->data));
    }
    // coap_add_data(pdu, snd_buf.size(),
    //               reinterpret_cast<unsigned char*>(snd_buf.data()));
    std::cout << "starting handshake with CON message" << std::endl;
    coap_show_pdu(pdu);
    /*auto tid = */ coap_send_confirmed(ctx, interface, &dst, pdu);

    //todo: handle retransmit

    // receive ACK (maybe piggybacked)
    actor_id remote_aid{0};
    std::uint32_t peer_pid;
    node_id::host_id_type peer_node_id;
    for(bool done = false; !done;) {
        // ### wait ACK ###
        // receive ACK + info
        snd_buf.clear();
        coap_address_init(&remote);
        bytes_read = coap_network_read(interface, &remote,
                                       rcv_buf, COAP_MAX_PDU_SIZE);
        if (bytes_read < 0) {
            throw std::runtime_error("waiting for handshake replay, "
                                     "but received empty packet");
        }
        binary_deserializer bds(reinterpret_cast<void*>(rcv_buf), bytes_read, nullptr);
//        bds.read_raw();
        // ### if piggy backed ###
        // read othres info
        // ### else wait for their info ###
        // ### send ACK ###
    }
    // #########################################

    auto new_peer = new io::transaction_based_peer(mm, ctx, interface, nullptr);
    auto pinfptr = make_counted<node_id>(peer_pid, peer_node_id);
    if (*pinf == *pinfptr) {
        // this is a local actor, not a remote actor
        CPPA_LOGF_WARNING("remote_actor() called to access a local actor");
        auto ptr = get_actor_registry()->get(remote_aid);
        return ptr;
    }
    struct remote_actor_result { remote_actor_result* next; actor value; };
    std::mutex qmtx;
    std::condition_variable qcv;
    intrusive::single_reader_queue<remote_actor_result> q;
    mm->run_later([mm, /* io,*/ pinfptr, remote_aid, &q, &qmtx, &qcv, &new_peer] {
        CPPA_LOGC_TRACE("cppa", "remote_actor$create_connection", "");
        // todo: check if peer exists
        mm->continue_reader(new_peer);
        auto res = mm->get_namespace().get_or_put(pinfptr, remote_aid);
        q.synchronized_enqueue(qmtx, qcv, new remote_actor_result{0, res});
    });
    std::unique_ptr<remote_actor_result> result(q.synchronized_pop(qmtx, qcv));
    CPPA_LOGF_DEBUG(CPPA_MARG(result, get));
    return raw_access::get(result->value);
}
    
} // namespace detail

} // namespace cppa
