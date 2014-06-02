#ifndef COAP_UTIL_HPP
#define COAP_UTIL_HPP

#include "coap.h"

namespace cppa {
namespace io {

namespace {

using method_t = unsigned char;

constexpr int FLAGS_BLOCK = 0x01;
constexpr unsigned char msgtype = COAP_MESSAGE_CON;

}

coap_pdu_t* coap_new_request(coap_context_t *ctx, method_t m,
                             coap_list_t *options, str *the_token,
                             coap_block_t *block, int flags,
                             void* data, size_t size);

} // namespace io
} // namespace cppa

#endif // COAP_UTIL_HPP
