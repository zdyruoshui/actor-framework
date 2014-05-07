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


#include <ios>
#include <string>
#include <cstring>
#include <errno.h>
#include <iostream>

#include "cppa/config.hpp"
#include "cppa/logging.hpp"
#include "cppa/exception.hpp"

#include "cppa/io/stream.hpp"
#include "cppa/io/ip_udp_acceptor.hpp"
#include "cppa/io/ip_udp_io_stream.hpp"

#include "cppa/detail/fd_util.hpp"

#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


namespace cppa { namespace io {

using namespace ::cppa::detail::fd_util;

namespace {

struct socket_guard {

    bool m_released;
    native_socket_type m_socket;

 public:

    socket_guard(native_socket_type sfd) : m_released(false), m_socket(sfd) { }

    ~socket_guard() {
        if (!m_released) closesocket(m_socket);
    }

    void release() {
        m_released = true;
    }

};

bool accept_impl(stream_ptr_pair& result,
                 native_socket_type fd,
                 bool nonblocking) {
    /*
    sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    auto sfd = ::accept(fd, &addr, &addrlen);
    if (sfd == invalid_socket) {
        auto err = last_socket_error();
        if (nonblocking && would_block_or_temporarily_unavailable(err)) {
            // ok, try again
            return false;
        }
        throw_io_failure("accept failed");
    }
    stream_ptr ptr(ip_udp_io_stream::from_native_socket(sfd));
    result.first = ptr;
    result.second = ptr;
    */
    // TODO: is there anything else, instead of accept
    return true;
}

} // namespace <anonymous>

ip_udp_acceptor::ip_udp_acceptor(native_socket_type fd, bool nonblocking)
: m_fd(fd), m_is_nonblocking(nonblocking) { }

std::unique_ptr<acceptor> ip_udp_acceptor::create(std::uint16_t port,
                                                         const char* addr) {
    CPPA_LOGM_TRACE("ip_udp_acceptor", CPPA_ARG(port) << ", addr = "
                                       << (addr ? addr : "nullptr"));
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    std::string port_s = std::to_string(port);
    int rv = getaddrinfo(addr, port_s.c_str(), &hints, &servinfo);
    if (rv != 0) {
        std::string errstr = "invalid address: ";
        errstr += gai_strerror(rv);
        throw network_error(std::move(errstr));
    }
    // expecting getaddrinfo to return only one result
    native_socket_type sockfd = socket(servinfo->ai_family,
                                       servinfo->ai_socktype,
                                       servinfo->ai_protocol);
    if (sockfd == invalid_socket) {
        throw network_error("could not create server socket");
    }
    socket_guard sguard(sockfd);
    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) != 0) {
        throw bind_failure(errno);
    }
    freeaddrinfo(servinfo);
    // default mode is nonblocking
    nonblocking(sockfd, true);
    // ok, no exceptions
    sguard.release();
    CPPA_LOGM_DEBUG("ip_udp_acceptor", "sockfd = " << sockfd);
    return std::unique_ptr<ip_udp_acceptor>(new ip_udp_acceptor(sockfd, true));
}

ip_udp_acceptor::~ip_udp_acceptor() {
    closesocket(m_fd);
}

stream_ptr_pair ip_udp_acceptor::accept_connection() {
    if (m_is_nonblocking) {
        nonblocking(m_fd, false);
        m_is_nonblocking = false;
    }
    stream_ptr_pair result;
    accept_impl(result, m_fd, m_is_nonblocking);
    return result;
}

optional<stream_ptr_pair> ip_udp_acceptor::try_accept_connection() {
    if (!m_is_nonblocking) {
        nonblocking(m_fd, true);
        m_is_nonblocking = true;
    }
    stream_ptr_pair result;
    if (accept_impl(result, m_fd, m_is_nonblocking)) {
        return result;
    }
    return none;
}

} } // namespace cppa::io
