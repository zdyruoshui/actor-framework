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
#include <cstring>
#include <errno.h>
#include <iostream>

#include "cppa/config.hpp"
#include "cppa/logging.hpp"
#include "cppa/exception.hpp"
#include "cppa/detail/fd_util.hpp"
#include "cppa/io/ip_udp_io_stream.hpp"

#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

namespace cppa { namespace io {

using namespace ::cppa::detail::fd_util;

ip_udp_io_stream::ip_udp_io_stream(native_socket_type fd) : m_fd(fd) { }
    
ip_udp_io_stream::~ip_udp_io_stream() {
    closesocket(m_fd);
}

native_socket_type ip_udp_io_stream::read_handle() const {
    return m_fd;
}

native_socket_type ip_udp_io_stream::write_handle() const {
    return m_fd;
}

void ip_udp_io_stream::read(void *vbuf, size_t len) {
    auto buf = reinterpret_cast<char*>(vbuf);
    size_t rd = 0;
    while (rd < len) {
        struct sockaddr_storage their_addr;
        socklen_t addr_len = sizeof(their_addr);
        auto recv_result = recvfrom(m_fd, buf + rd, len - rd, 0,
                                    (struct sockaddr *) &their_addr, &addr_len);
        char s[INET6_ADDRSTRLEN];
        CPPA_LOGF_DEBUG("received " << recv_result << " bytes from " <<
                        inet_ntop(their_addr.ss_family,
                                  get_in_addr((struct sockaddr *)&their_addr),
                                  s, sizeof s));
        handle_read_result(recv_result, true);
        if (recv_result > 0) {
            rd += static_cast<size_t>(recv_result);
        }
        if (rd < len) {
            fd_set rdset;
            FD_ZERO(&rdset);
            FD_SET(m_fd, &rdset);
            if (select(m_fd +1 , &rdset, nullptr, nullptr, nullptr) < 0) {
                throw network_error("select() failed");
            }
        }
    }
}

size_t ip_udp_io_stream::read_some(void* buf, size_t len) {
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof(their_addr);
    auto recv_result = recvfrom(m_fd, buf, len, 0,
                                (struct sockaddr *) &their_addr, &addr_len);
    char s[INET6_ADDRSTRLEN];
    CPPA_LOGF_DEBUG("received " << recv_result << " bytes from " <<
                    inet_ntop(their_addr.ss_family,
                              get_in_addr((struct sockaddr *)&their_addr),
                              s, sizeof s));
    handle_read_result(recv_result, true);
    return (recv_result > 0) ? static_cast<size_t>(recv_result) : 0;
}

void ip_udp_io_stream::write(const void *vbuf, size_t len) {
    auto buf = reinterpret_cast<const char*>(vbuf);
    size_t written = 0;
//    while (written < len) {
//        auto send_result = sendto(m_fd, buf + written, len - written, <#int#>, <#const struct sockaddr *#>, <#socklen_t#>)
//    }
}

size_t ip_udp_io_stream::write_some(const void *buf, size_t len) {
    
}

io::stream_ptr ip_udp_io_stream::connect_to(const char *host,
                                            std::uint16_t port) {
    CPPA_LOGF_TRACE(CPPA_ARG(host) << ", " << CPPA_ARG(port));
    CPPA_LOGF_INFO("try to connect to " << host << " on port " << port);
    int rv;
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    std::string port_s = std::to_string(port);
    rv = getaddrinfo(host, port_s.c_str(), &hints, &servinfo);
    if (rv != 0) {
        std::string errstr = "no such host: ";
        errstr += host;
        throw network_error(std::move(errstr));
    }
    native_socket_type fd = socket(servinfo->ai_family,
                                   servinfo->ai_socktype,
                                   servinfo->ai_protocol);
    if (fd == invalid_socket) {
        throw network_error("socket creation failed");
    }
    
 }

} } // namespace cppa::io
