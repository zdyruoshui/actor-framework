#ifndef COAP_UTIL_HPP
#define COAP_UTIL_HPP

#include "coap.h"

#include <atomic>

namespace cppa {
namespace io {

namespace {

using method_t = unsigned char;

constexpr int FLAGS_BLOCK = 0x01;
constexpr unsigned char msgtype = COAP_MESSAGE_CON;

constexpr unsigned int wait_seconds = 90;   /* default timeout in seconds */
constexpr unsigned int obs_seconds = 30;    /* default observe time */

}

struct coap_scope {

    coap_scope(coap_context_t *ctx);
    ~coap_scope();

    coap_context_t*      ctx;

    static unsigned char token_data[8];
    str                  the_token;

    method_t             default_method;
    coap_block_t         block;
    int                  flags;

    coap_tick_t          max_wait;   /* global timeout (changed by set_timeout()) */
    coap_tick_t          obs_wait;   /* timeout for current subscription */

    std::atomic<bool>    ready;
};

/***  coap utility functions ***/

} // namespace io
} // namespace cppa

#endif // COAP_UTIL_HPP
