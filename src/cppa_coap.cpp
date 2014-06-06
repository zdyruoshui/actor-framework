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

#include "coap.h"
#include "netdb.h"

#include "cppa/io/coap_util.hpp"
#include "cppa/io/cppa_coap.hpp"

namespace cppa {
  
void coap_publish(cppa::actor whom, std::uint16_t port, const char* addr) {
//    actor a{detail::raw_access::unsafe_cast(detail::raw_access::get(whom))};
//    CPPA_LOGF_TRACE(CPPA_TARG(whom, to_string) << ", " << CPPA_MARG(aptr, get));
//    if (!whom) return;
//    get_actor_registry()->put(whom->id(), detail::raw_access::get(whom));
//    auto mm = get_middleman();
    coap_endpoint_t* interface{nullptr};
    char addr_str[NI_MAXHOST] = "::";
    coap_context_t* ctx = io::get_context(addr_str, std::to_string(port).c_str(), interface);
    if (ctx == nullptr || interface == nullptr) {
        coap_free_context(ctx);
        throw std::ios_base::failure("Cannot create socket");
    }
    auto mm = get_middleman();
    auto new_peer = new io::transaction_based_peer(mm, ctx, interface, nullptr);
    mm->run_later([mm, new_peer] {
        get_middleman()->continue_reader(new_peer);
    });
}


actor remote_actor(const char* host, std::uint16_t port) {
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
        ctx = io::get_context("0.0.0.0", port_str, interface);
        break;
    case AF_INET6:
        addrptr = &dst.addr.sin6.sin6_addr;

        /* create context for IPv6 */
        ctx = io::get_context("::", port_str, interface);
        break;
    default:
        ;
    }
    if (ctx == nullptr || interface == nullptr) {
        coap_free_context(ctx);
        throw std::ios_base::failure("Cannot create socket");
    }
    
    auto mm = get_middleman();
    auto tb_peer = new io::transaction_based_peer(mm, ctx, interface, nullptr);
    // pass host to peer
    // endpoint for sending messages
}

} // namespace cppa
