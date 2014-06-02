#include "pdu.h"

#include "cppa/io/coap_util.hpp"

namespace cppa {
namespace io {

coap_pdu_t* coap_new_request(coap_context_t *ctx, method_t m,
                             coap_list_t* options, str *the_token,
                             coap_block_t* block, int flags,
                             void* data, size_t size) {
    coap_pdu_t *pdu;
    coap_list_t *opt;

    if ( ! ( pdu = coap_new_pdu() ) )
    return NULL;

    pdu->hdr->type = msgtype;
    pdu->hdr->id = coap_new_message_id(ctx);
    pdu->hdr->code = m;

    pdu->hdr->token_length = the_token->length;
    if ( !coap_add_token(pdu, the_token->length, the_token->s)) {
        debug("cannot add token to request\n");
    }

    coap_show_pdu(pdu);

    for (opt = options; opt; opt = opt->next) {
        coap_add_option(pdu, COAP_OPTION_KEY(*(coap_option *)opt->data),
                        COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                        COAP_OPTION_DATA(*(coap_option *)opt->data));
    }

    if (size > 0) {
    if ((flags & FLAGS_BLOCK) == 0)
        coap_add_data(pdu, size, (unsigned char *) data);
    else
        coap_add_block(pdu, size, (unsigned char *) data,
                       block->num, block->szx);
    }

    return pdu;
}

} // namespace io
} // namespace cppa
