#include "pdu.h"

#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cppa/io/coap_util.hpp"
#include "cppa/io/transaction_based_peer.hpp"

namespace cppa {
namespace io {

/***  coap utility functions ***/

coap_context_t* get_context(const char* node, const char *port,
                            coap_endpoint_t* local_interface) {
    coap_context_t *ctx = NULL;
    int s;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Coap uses UDP */
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ALL;

    s = getaddrinfo(node, port, &hints, &result);
    if ( s != 0 ) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return NULL;
    }

    /* iterate through results until success */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        coap_address_t addr;

        if (rp->ai_addrlen <= sizeof(addr.addr)) {
            coap_address_init(&addr);
            addr.size = rp->ai_addrlen;
            memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

            local_interface = coap_new_endpoint(&addr, 0);
            if (!local_interface)
                continue;

            ctx = coap_new_context();
            if (ctx) {
                /* TODO: output address:port for successful binding */
                break;
            } else {
                coap_free_endpoint(local_interface);
                local_interface = NULL;
            }
        }
    }

    if (!ctx) {
        throw std::ios_base::failure("could not create context "
                                     "for a local interfaces");
    }
    freeaddrinfo(result);
    return ctx;
}

void message_handler(struct coap_context_t  *ctx,
                     const coap_address_t *remote,
                     coap_pdu_t *sent,
                     coap_pdu_t *received,
                     const coap_tid_t id) {

    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    coap_pdu_t *pdu = NULL;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    unsigned char buf[4];
    coap_list_t *option;
    size_t len;
    unsigned char *databuf;
    coap_tid_t tid;

    if (LOG_DEBUG <= coap_get_log_level()) {
        debug("** process incoming %d.%02d response:\n",
              (received->hdr->code >> 5), received->hdr->code & 0x1F);
        coap_show_pdu(received);
    }

    // check if this is a response to an original request
    auto itr = std::find_if(std::begin(ptr->m_requests), std::end(ptr->m_requests),
                 [&] (const std::map<unsigned short,transaction_based_peer::coap_request>::value_type& t) {
        return received->hdr->token_length == t.second.the_token.length &&
          memcmp(received->hdr->token, t.second.the_token.s, t.second.the_token.length) == 0;
    });
    if (itr == std::end(ptr->m_requests)) {
        // drop if this was just some message, or send RST in case of notification
        if (!sent && (received->hdr->type == COAP_MESSAGE_CON ||
                      received->hdr->type == COAP_MESSAGE_NON)) {
            coap_send_rst(ctx, ptr->m_coap_scope.local_interface, remote, received);
        }
        return;
    }

    switch (received->hdr->type) {
    case COAP_MESSAGE_CON:
        /* acknowledge received response if confirmable (TODO: check Token) */
        coap_send_ack(ctx, ptr->m_coap_scope.local_interface, remote, received);
        break;
    case COAP_MESSAGE_RST:
        info("got RST\n");
        return;
    default:
        ;
    }

    // output the received data, if any, todo: what is
    if (received->hdr->code == COAP_RESPONSE_CODE(205)) {
        // request data matching the response
        auto req = itr->second;

        // set obs timer if we have successfully subscribed a resource
        if (sent && coap_check_option(received, COAP_OPTION_SUBSCRIPTION, &opt_iter)) {
            debug("observation relationship established, set timeout to %d\n", obs_seconds);
            set_timeout(&req.obs_wait, obs_seconds);
        }

        // Got some data, check if block option is set. Behavior is undefined if
        // both, Block1 and Block2 are present.
        block_opt = get_block(received, &opt_iter);
        if (!block_opt) {
            // There is no block option set, just read the data and we are done.
            if (coap_get_data(received, &len, &databuf)) {
                // TODO: handle data
                std::cout << "[message handler] handle incoming data missing" << std::endl;
//                append_to_output(databuf, len);
            }

        } else {
            unsigned short blktype = opt_iter.type;

            /* TODO: check if we are looking at the correct block number */
            if (coap_get_data(received, &len, &databuf)) {
                // TODO: handle data
                std::cout << "[message handler] handle incoming data missing" << std::endl;
//                append_to_output(databuf, len);
            }

            if (COAP_OPT_BLOCK_MORE(block_opt)) {
                /* more bit is set */
                debug("found the M bit, block size is %u, block nr. %u\n",
                      COAP_OPT_BLOCK_SZX(block_opt),
                      COAP_OPT_BLOCK_NUM(block_opt));

                // create pdu with request for next block
                // first, create bare PDU w/o any option
                pdu = coap_new_request(ctx, ptr->m_coap_scope.default_method, nullptr);
                if ( pdu ) {
                    // add URI components from optlist
                    for (option = ptr->m_coap_scope.options; option; option = option->next ) {
                        switch (COAP_OPTION_KEY(*(coap_option *)option->data)) {
                        case COAP_OPTION_URI_HOST :
                        case COAP_OPTION_URI_PORT :
                        case COAP_OPTION_URI_PATH :
                        case COAP_OPTION_URI_QUERY :
                            coap_add_option(
                                pdu,
                                COAP_OPTION_KEY   (*(coap_option *)option->data),
                                COAP_OPTION_LENGTH(*(coap_option *)option->data),
                                COAP_OPTION_DATA  (*(coap_option *)option->data)
                            );
                            break;
                        default:
                            ;			/* skip other options */
                        }
                    }

                    // finally add updated block option from response, clear M bit
                    // blocknr = (blocknr & 0xfffffff7) + 0x10;
                    debug("query block %d\n", (COAP_OPT_BLOCK_NUM(block_opt) + 1));
                    coap_add_option(pdu, blktype, coap_encode_var_bytes(buf,
                                    ((COAP_OPT_BLOCK_NUM(block_opt) + 1) << 4) |
                                    COAP_OPT_BLOCK_SZX(block_opt)), buf);

                    if (received->hdr->type == COAP_MESSAGE_CON) {
                        tid = coap_send_confirmed(ctx,
                                                  ptr->m_coap_scope.local_interface,
                                                  remote, pdu);
                    }
                    else {
                        tid = coap_send(ctx,
                                        ptr->m_coap_scope.local_interface,
                                        remote, pdu);
                    }

                    if (tid == COAP_INVALID_TID) {
                        debug("message_handler: error sending new request");
                        coap_delete_pdu(pdu);
                    } else {
                        set_timeout(&max_wait, wait_seconds);
                        if (received->hdr->type != COAP_MESSAGE_CON) {
                            coap_delete_pdu(pdu);
                        }
                    }

                    return;
                }
            }
        }
    } else {			/* no 2.05 */

        // check if an error was signaled and output payload if so
        if (COAP_RESPONSE_CLASS(received->hdr->code) >= 4) {
            fprintf(stderr, "%d.%02d",
                    (received->hdr->code >> 5), received->hdr->code & 0x1F);
            if (coap_get_data(received, &len, &databuf)) {
                fprintf(stderr, " ");
                while(len--)
                fprintf(stderr, "%c", *databuf++);
            }
            fprintf(stderr, "\n");
        }
    }

    // finally send new request, if needed
    if (pdu && coap_send(ctx, ptr->m_coap_scope.local_interface,
                         remote, pdu) == COAP_INVALID_TID) {
        debug("message_handler: error sending response");
    }
    coap_delete_pdu(pdu);

    // our job is done, we can exit at any time
    //ready = coap_check_option(received, COAP_OPTION_SUBSCRIPTION, &opt_iter) == NULL;
}

coap_pdu_t* coap_new_request(coap_context_t *ctx, method_t m,
                             coap_list_t *options,
                             void* payload, size_t length) {
    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    transaction_based_peer::coap_request req;

    coap_pdu_t *pdu;
    coap_list_t *opt;

    if (!(pdu = coap_new_pdu())) {
        throw std::runtime_error("failed to create coap pdu");
    }

    pdu->hdr->type = msgtype;
    pdu->hdr->id = coap_new_message_id(ptr->m_coap_scope.ctx);
    pdu->hdr->code = m;

    pdu->hdr->token_length = req.the_token.length;
    if (!coap_add_token(pdu, req.the_token.length, req.the_token.s)) {
        debug("cannot add token to request\n");
    }

    coap_show_pdu(pdu);

    for (opt = options; opt; opt = opt->next) {
        coap_add_option(pdu,
                        COAP_OPTION_KEY(*(coap_option *)opt->data),
                        COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                        COAP_OPTION_DATA(*(coap_option *)opt->data));
    }

    if (length) {
        if ((ptr->m_coap_scope.flags & FLAGS_BLOCK) == 0) {
            coap_add_data(pdu, length, reinterpret_cast<unsigned char*>(payload));
        }
        else {
            coap_add_block(pdu,
                           length, reinterpret_cast<unsigned char*>(payload),
                           req.block.num, req.block.szx);
        }
    }

    // add request to queue?
    // return request with pdu inside?
    // what was different before?
    // what doe the coap_add_XYZ functions do in the background?

    return pdu;

//    if (size > 0) {
//        if ((m_coap_scope.flags & FLAGS_BLOCK) == 0) {
//            coap_add_data(pdu, size, (unsigned char *) data);
//        }
//        else {
//            coap_add_block(pdu, size, (unsigned char *) data,
//                           req.block.num, req.block.szx);
//        }
//    }
//    coap_tid_t tid;
//    if (type == COAP_MESSAGE_CON) {
//        tid = coap_send_confirmed(m_coap_scope.ctx,
//                                  m_coap_scope.local_interface,
//                                  &dst, pdu);
//    }
//    else {
//        tid = coap_send(m_coap_scope.ctx,
//                        m_coap_scope.local_interface,
//                        &dst, pdu);
//    }
//    if (pdu->hdr->type != COAP_MESSAGE_CON || tid == COAP_INVALID_TID) {
//        coap_delete_pdu(pdu);
//    }
//    else {
//        coap_ticks(&req.timeout);
//        req.timeout += wait_seconds * COAP_TICKS_PER_SECOND;
//        m_requests.emplace(pdu->hdr->id, std::move(req));
//    }
}

} // namespace io
} // namespace cppa
