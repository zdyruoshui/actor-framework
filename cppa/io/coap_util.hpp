#ifndef COAP_UTIL_HPP
#define COAP_UTIL_HPP

#include <atomic>

#include "coap.h"

#include "cppa/io/transaction_based_peer.hpp"

namespace cppa {
namespace io {

namespace {

constexpr int FLAGS_BLOCK = 0x01;

constexpr unsigned int wait_seconds = 90;   /* default timeout in seconds */
constexpr unsigned int obs_seconds = 30;    /* default observe time */

}

/***  coap utility functions ***/

inline int check_token(coap_pdu_t *received, str* the_token) {
    return received->hdr->token_length == the_token->length &&
        memcmp(received->hdr->token, the_token->s, the_token->length) == 0;
}

inline void set_timeout(coap_tick_t *timer, const unsigned int seconds) {
    coap_ticks(timer);
    *timer += seconds * COAP_TICKS_PER_SECOND;
}

inline coap_opt_t* get_block(coap_pdu_t *pdu, coap_opt_iterator_t *opt_iter) {
    coap_opt_filter_t f;

    assert(pdu);

    memset(f, 0, sizeof(coap_opt_filter_t));
    coap_option_setb(f, COAP_OPTION_BLOCK1);
    coap_option_setb(f, COAP_OPTION_BLOCK2);

    coap_option_iterator_init(pdu, opt_iter, f);
    return coap_option_next(opt_iter);
}

void generate_token(str* the_token, size_t bytes);

coap_context_t* get_context(const char *node, const char *port,
                            coap_endpoint_t * interface);

void message_handler(struct coap_context_t  *ctx,
                     const coap_endpoint_t *local_interface,
                     const coap_address_t *remote,
                     coap_pdu_t *sent,
                     coap_pdu_t *received,
                     const coap_tid_t id);

int order_opts(void *a, void *b);

coap_list_t* new_option_node(unsigned short key,
                             unsigned int length,
                             unsigned char *data);

transaction_based_peer::coap_request new_request(coap_context_t *ctx,
                                                 unsigned char method,
                                                 coap_list_t *options,
                                                 void* payload, size_t size);

int resolve_address(const char* server, struct sockaddr *dst);

} // namespace io
} // namespace cppa

#endif // COAP_UTIL_HPP
