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

#ifndef COAP_WRAPPER_HPP
#define COAP_WRAPPER_HPP

#include <cstdio>
#include <string>

#include "coap.h"


namespace cppa {
namespace io {

namespace {
    constexpr unsigned int wait_seconds = 90;   // default timeout in seconds
    constexpr unsigned int obs_seconds = 30;    // default observe time
}

inline void set_timeout(coap_tick_t *timer, const unsigned int seconds) {
    coap_ticks(timer);
    *timer += seconds * COAP_TICKS_PER_SECOND;
}

/*
 * Based on the coap implementation:
 * http://libcoap.sourceforge.net
 */
class coap_client_wrapper {

    typedef unsigned char method_t;
    struct content_type_t {
        unsigned char code;
        char *media_type;
    };

 public:

    /**
     * @brief Returns the internal file descriptor. This descriptor is needed
     *        for socket multiplexing using select().
     */
    virtual native_socket_type write_handle() const = 0;

    /**
     * @brief Writes @p num_bytes bytes from @p buf to the data sink.
     * @note This member function blocks until @p num_bytes were successfully
     *       written.
     * @throws std::ios_base::failure
     */
    virtual void write(const void* buf, size_t num_bytes) = 0;

    /**
     * @brief Tries to write up to @p num_bytes bytes from @p buf.
     * @returns The number of written bytes.
     * @throws std::ios_base::failure
     */
    virtual size_t write_some(const void* buf, size_t num_bytes) = 0;


 private:

    coap_client_wrapper();

    int append_to_output(const unsigned char* data, size_t len);
    void close_output();
    coap_pdu_t* new_ack(coap_context_t* ctx, coap_queue_t* node);
    coap_pdu_t* new_response(coap_context_t* ctx,
                             coap_queue_t* node,
                             unsigned int code);
    coap_pdu_t* coap_new_request(coap_context_t* ctx,
                                 method_t m,
                                 coap_list_t* options);
    coap_tid_t clear_obs(coap_context_t* ctx,
                         const coap_address_t* remote);
    int resolve_address(const str* server,
                        struct sockaddr* dst);
    static inline coap_opt_t* get_block(coap_pdu_t* pdu,
                                        coap_opt_iterator_t* opt_iter);
    inline int check_token(coap_pdu_t* received);
    void message_handler(struct coap_context_t  *ctx, 
                         const coap_address_t *remote,
                         coap_pdu_t* sent,
                         coap_pdu_t* received,
                         const coap_tid_t id);
    int join(coap_context_t* ctx, char* group_name);
    int order_opts(void* a, void* b);
    coap_list_t* new_option_node(unsigned short key,
                                 unsigned int length,
                                 unsigned char *data);
//    void cmdline_content_type(char *arg, unsigned short key);
//    void cmdline_uri(char *arg);
//    int cmdline_blocksize(char *arg);
//    void set_blocksize();
//    void cmdline_subscribe(char *arg);
//    int cmdline_proxy(char *arg);
//    inline void cmdline_token(char *arg);
//    void cmdline_option(char *arg);
////    extern int  check_segment(const unsigned char *s,
////                              size_t length);
////    extern void decode_segment(const unsigned char *seg,
////                               size_t length,
////                               unsigned char *buf);
//    int cmdline_input(char *text, str *buf);
//    int cmdline_input_from_file(char *filename, str *buf);
    method_t cmdline_method(char* arg);
    coap_context_t* get_context(const char* node,
                                const char* port);

    // found outside of main (mostly static)
    // unsigned char _token_data[8]; the_token = { 0, _token_data };
    std::string m_token;
    coap_list_t* m_optlist;
    coap_uri_t m_uri; // Request URI
    std::string m_proxy;
    unsigned short m_proxy_port; // = COAP_DEFAULT_PORT;
    int m_ready;
    std::string m_output_file; // str output_file = { 0, NULL };
    FILE* m_file;
    std::string m_payload;   // str payload = { 0, NULL };
    unsigned char m_msgtype; //= COAP_MESSAGE_CON;
    method_t m_method; // = 1;
    coap_block_t m_block; // = { .num = 0, .m = 0, .szx = 6 };
    coap_tick_t m_max_wait; // global timeout (changed by set_timeout())
    coap_tick_t m_obs_wait; // = 0; //timeout for current subscription

    // found inside main
    coap_context_t* m_ctx;
    coap_address_t m_dst;
    std::string m_addr; //static char addr[INET6_ADDRSTRLEN];
    void* m_addrptr; // = NULL;
    fd_set m_readfds;
    struct timeval m_tv;
    int m_result;
    coap_tick_t m_now;
    coap_queue_t* m_nextpdu;
    coap_pdu_t* m_pdu;
    std::string m_server; // static str server
    unsigned short m_port; // = COAP_DEFAULT_PORT;
    std::string m_port_str; // char port_str[NI_MAXSERV] = "0";
    int m_opt
    int m_res;
    char* m_group; // = NULL;
    coap_log_t m_log_level; // = LOG_WARNING;
    coap_tid_t m_tid; // = COAP_INVALID_TID;

};

} // namespace io
} // namespace cppa

#endif // COAP_WRAPPER_HPP
