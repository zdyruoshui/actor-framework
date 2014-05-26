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


#include <netdb.h>

#include "cppa/io/coap_client_wrapper.hpp"

using namespace std;

namespace {
    constexpr size_t token_size = 8;
}

namespace cppa {
namespace io {

coap_client_wrapper::coap_client_wrapper()
    : m_optlist{nullptr}
    , m_proxy_port{COAP_DEFAULT_PORT}
    , m_msgtype{COAP_MESSAGE_CON}
    , m_method{1}
    , m_block{ .num = 0, .m = 0, .szx = 6 }
    , m_max_wait{0}
    , m_obs_wait{0}
    , m_ctx{nullptr}
    , m_addrptr{nullptr}
    , m_nextpdu{nullptr}
    , m_pdu{nullptr}
    , m_port{COAP_DEFAULT_PORT}
    , m_group{nullptr}
    , m_log_level{LOG_WARNING}
    , m_tid{COAP_INVALID_TID}
{
    m_token.resize(token_size);
    m_addr.resize(INET6_ADDRSTRLEN);
    m_port_str.resize(NI_MAXSERV);
}

} // namespace io
} // namespace cppa
