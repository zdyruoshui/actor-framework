
#include <chrono>
#include <random>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <algorithm>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cppa/cppa.hpp"

#include "cppa/io/coap_util.hpp"
#include "cppa/io/transaction_based_peer.hpp"

#include "cppa/binary_serializer.hpp"
#include "cppa/binary_deserializer.hpp"

namespace cppa {
namespace io {

/***  coap utility classes ***/

/***  coap utility functions ***/

void generate_token(str* the_token, size_t bytes) {
    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    std::stringstream s;
    s << msec;
    std::random_device rdev{};
    std::independent_bits_engine<std::mt19937_64,64,std::uint_fast64_t> gen{rdev()};
    std::uniform_int_distribution<std::uint_fast64_t> dis{};
    while (s.str().size() < bytes) {
        s << dis(gen);    
    }
    std::copy_n(s.str().c_str(), bytes, the_token->s);
    the_token->length = bytes;
}

coap_context_t* get_context(const char *node, const char *port,
                            coap_endpoint_t * interface) {
    coap_context_t *ctx = nullptr;
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
        return nullptr;
    }

    /* iterate through results until success */
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        coap_address_t addr;

        if (rp->ai_addrlen <= sizeof(addr.addr)) {
            coap_address_init(&addr);
            addr.size = rp->ai_addrlen;
            memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

            interface = coap_new_endpoint(&addr, 0);
            if (!interface)
                continue;

            ctx = coap_new_context();
            if (ctx) {
                // binding succeeded
                break;
            } else {
                coap_free_endpoint(interface);
                interface = nullptr;
            }
        }
    }
  
    fprintf(stderr, "no context available for interface '%s'\n", node);

    freeaddrinfo(result);
    return ctx;
}

void message_handler(struct coap_context_t  *ctx,
                     const coap_endpoint_t*, // local_interface
                     const coap_address_t *remote,
                     coap_pdu_t *sent,
                     coap_pdu_t *received,
                     const coap_tid_t id) {

    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    coap_pdu_t *pdu = nullptr;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    unsigned char buf[4];
    size_t len;
    unsigned char *databuf;
    coap_tid_t tid;

    if (LOG_DEBUG <= coap_get_log_level()) {
        CPPA_LOGF_TRACE("process incoming response: " <<
                        (received->hdr->code >> 5) << "." <<
                        std::setw(2) << std::setfill('0') <<
                        (received->hdr->code & 0x1F));
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
            coap_send_rst(ctx, ptr->m_interface, remote, received);
        }
        return;
    }
    
    // todo: remove after testing
    std::cout << "[message handler] received packet has "
              << ((itr->first == id) ? "expected" : "unexpected")
              << " coap_tid" << std::endl;


    switch (received->hdr->type) {
    case COAP_MESSAGE_CON:
        /* acknowledge received response if confirmable (TODO: check Token) */
        coap_send_ack(ctx, ptr->m_interface, remote, received);
        break;
    case COAP_MESSAGE_RST:
        info("got RST\n");
        return;
    default:
        ;
    }

    // output the received data, if any
    if (received->hdr->code == COAP_RESPONSE_CODE(205)) {
        auto request = itr->second; // request data matching the response

        // set obs timer if we have successfully subscribed a resource
        if (sent && coap_check_option(received, COAP_OPTION_SUBSCRIPTION, &opt_iter)) {
            CPPA_LOGF_DEBUG("[message handler] observation relationship "
                            "established, set timeout to" << obs_seconds);
            set_timeout(&request.obs_wait, obs_seconds);
        }

        // Got some data, check if block option is set.
        block_opt = get_block(received, &opt_iter);
        if (!block_opt) { // no block option set
            std::cout << "[message handler] message without block opt" << std::endl;
            if (coap_get_data(received, &len, &databuf)) {
                std::cout << "[message handler] incoming data" << std::endl;
                message_header hdr;
                any_tuple msg;
                binary_deserializer bd(databuf, len,
                                       &(ptr->m_parent->get_namespace()),
                                       nullptr);
                // todo: not sure about the nullptr
                try {
                    ptr->m_meta_hdr->deserialize(&hdr, &bd);
                    ptr->m_meta_msg->deserialize(&msg, &bd);
                }
                catch (std::exception& e) {
                    CPPA_LOGF_ERROR("exception during read_message: "
                                    << detail::demangle(typeid(e))
                                    << ", what(): " << e.what());
                }
                CPPA_LOGF_DEBUG("deserialized: " << to_string(hdr)
                                                 << " " << to_string(msg));
                match(msg) (
                    on(atom("MONITOR"), arg_match) >> [&](const node_id_ptr&,
                                                          actor_id) {
                        CPPA_LOGF_DEBUG("[message_handler] received MONITOR msg");
                    },
                    on(atom("KILL_PROXY"), arg_match) >> [&](const node_id_ptr&,
                                                             actor_id, std::uint32_t) {
                        CPPA_LOGF_DEBUG("[message_handler] received KILL msg");
                    },
                    on(atom("LINK"), arg_match) >> [&](const actor_addr&) {
                        CPPA_LOGF_DEBUG("[message_handler] received LINK msg");
                    },
                    on(atom("UNLINK"), arg_match) >> [&](const actor_addr&) {
                        CPPA_LOGF_DEBUG("[message_handler] received UNLINK msg");
                    },
                    on(atom("ADD_TYPE"), arg_match) >> [&](std::uint32_t,
                                                           const std::string&) {
                        CPPA_LOGF_DEBUG("[message_handler] received TYPE msg");
                    },
                    others() >> [&] {
                        hdr.deliver(std::move(msg));
                    }
                );
            }
        } else {
            std::cout << "[message handler] message with block opt" << std::endl;
            unsigned short blktype = opt_iter.type;
            if (coap_get_data(received, &len, &databuf)) {
                // TODO: handle data
                std::cout << "[message handler] handle incoming data missing" << std::endl;
            }
            if (COAP_OPT_BLOCK_MORE(block_opt)) { // more bit is set
                debug("found the M bit, block size is %u, block nr. %u\n",
                      COAP_OPT_BLOCK_SZX(block_opt),
                      COAP_OPT_BLOCK_NUM(block_opt));

                // create pdu with request for next block
                pdu = coap_new_pdu();
                if (!pdu) {
                    throw std::runtime_error("[message handler] failed to create coap pdu");
                }
                pdu->hdr->type = request.pdu->hdr->type;
                pdu->hdr->id = coap_new_message_id(ctx);
                pdu->hdr->code = request.pdu->hdr->code;
                
                generate_token(&request.the_token, 8); // todo: is this required?
                coap_add_token(pdu, request.the_token.length, request.the_token.s);
                
                // add URI components from optlist
                auto add_options = [&pdu](coap_list_t* options) {
                    for (coap_list_t* opt = options; opt; opt = opt->next ) {
                        switch (COAP_OPTION_KEY(*(coap_option *)opt->data)) {
                        case COAP_OPTION_URI_HOST :
                        case COAP_OPTION_URI_PORT :
                        case COAP_OPTION_URI_PATH :
                        case COAP_OPTION_URI_QUERY :
                            coap_add_option(
                                pdu,
                                COAP_OPTION_KEY   (*(coap_option *)opt->data),
                                COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                                COAP_OPTION_DATA  (*(coap_option *)opt->data)
                            );
                            break;
                        default:
                            ;			/* skip other options */
                        }
                    }
                };
                add_options(ptr->m_options);
                add_options(request.options);

                // finally add updated block option from response, clear M bit
                // blocknr = (blocknr & 0xfffffff7) + 0x10;
                debug("query block %d\n", (COAP_OPT_BLOCK_NUM(block_opt) + 1));
                coap_add_option(pdu, blktype, coap_encode_var_bytes(buf,
                                ((COAP_OPT_BLOCK_NUM(block_opt) + 1) << 4) |
                                  COAP_OPT_BLOCK_SZX(block_opt)), buf);

                if (received->hdr->type == COAP_MESSAGE_CON) {
                    tid = coap_send_confirmed(ctx,
                                              ptr->m_interface,
                                              remote, pdu);
                }
                else {
                    tid = coap_send(ctx,
                                    ptr->m_interface,
                                    remote, pdu);
                }

                if (tid == COAP_INVALID_TID) {
                    debug("message_handler: error sending new request");
                    coap_delete_pdu(pdu);
//                    ptr->m_requests.erase(req);
                } else {
                    set_timeout(&ptr->m_max_wait, wait_seconds);
                    if (received->hdr->type != COAP_MESSAGE_CON) {
                        coap_delete_pdu(pdu);
//                        ptr->m_requests.erase(req);
                    }
                }
                return;
            }
            else {
                std::cout << "[message handler] message with block opt"
                             " ('more' flag is not set -> unhandled)"
                          << std::endl;
            }
        }
    } else {			/* no 2.05 */

        // check if an error was signaled and output payload if so
        if (COAP_RESPONSE_CLASS(received->hdr->code) >= 4) {
            CPPA_LOGF_ERROR((received->hdr->code >> 5) << "." <<
                            std::setw(2) << std::setfill('0') <<
                            (received->hdr->code & 0x1F));
            if (coap_get_data(received, &len, &databuf)) {
                std::cout << "[message handler] error + data -> unhandled"
                          << std::endl;
            }
            fprintf(stderr, "\n");
        }
    }

    // send new request, if needed
    if (pdu) {
        auto ret = coap_send(ctx, ptr->m_interface, remote, pdu);
        if (ret == COAP_INVALID_TID) {
            throw std::runtime_error("[message_handler] error sending response");
        }
    }
    coap_delete_pdu(pdu);

    // our job is done, we can exit at any time
    // ready = coap_check_option(received, COAP_OPTION_SUBSCRIPTION, &opt_iter) == NULL;
}

int order_opts(void *a, void *b) {
    if (!a || !b) {
        return a < b ? -1 : 1;
    }
    if (COAP_OPTION_KEY(*(coap_option *)a) < COAP_OPTION_KEY(*(coap_option *)b)) {
        return -1;
    }
    return COAP_OPTION_KEY(*(coap_option *)a) == COAP_OPTION_KEY(*(coap_option *)b);
}

coap_list_t* new_option_node(unsigned short key, unsigned int length, unsigned char *data) {
    coap_option *option;
    coap_list_t *node;

    option = reinterpret_cast<coap_option*>(coap_malloc(sizeof(coap_option) + length));
    if ( option ) {
        COAP_OPTION_KEY(*option) = key;
        COAP_OPTION_LENGTH(*option) = length;
        memcpy(COAP_OPTION_DATA(*option), data, length);

        /* we can pass NULL here as delete function since option is released automatically  */
        node = coap_new_listnode(option, NULL);

        if ( node ) {
            return node;
        }
    }
    perror("new_option_node: malloc");
    coap_free( option );
    return NULL;
}

transaction_based_peer::coap_request new_request(coap_context_t *ctx,
                                                 unsigned char method,
                                                 coap_list_t *options,
                                                 void *payload, size_t size) {
    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);

    transaction_based_peer::coap_request req;
    coap_pdu_t*  pdu{nullptr};

    req.data = payload;
    req.size = size;

    if (size) {
        req.block.num = size;
        req.block.szx = (coap_fls(size >> 4) - 1) & 0x07;
        req.flags |= FLAGS_BLOCK;

        static unsigned char buf[4];	/* hack: temporarily take encoded bytes */
        unsigned short opt;

        if (method != COAP_REQUEST_DELETE) {
            opt = method == COAP_REQUEST_GET ? COAP_OPTION_BLOCK2 : COAP_OPTION_BLOCK1;

            coap_insert(
                &req.options,
                new_option_node(
                    opt,
                    coap_encode_var_bytes(buf, (req.block.num << 4 | req.block.szx)),
                    buf
                ),
                order_opts
            );
        }
    }

    if (!(pdu = coap_new_pdu())) {
        throw std::runtime_error("failed to create coap pdu");
    }

    pdu->hdr->type = ptr->m_default_msgtype;
    pdu->hdr->id   = coap_new_message_id(ptr->m_ctx);
    pdu->hdr->code = method;

    pdu->hdr->token_length = req.the_token.length;
    if (!coap_add_token(pdu, req.the_token.length, req.the_token.s)) {
        debug("cannot add token to request\n");
    }

    coap_show_pdu(pdu);

    auto add_options = [&pdu](coap_list_t* options) {
        for (coap_list_t* opt = options; opt; opt = opt->next) {
            coap_add_option(pdu,
                            COAP_OPTION_KEY(*(coap_option *)opt->data),
                            COAP_OPTION_LENGTH(*(coap_option *)opt->data),
                            COAP_OPTION_DATA(*(coap_option *)opt->data));
        }
    };

    add_options(options);
    add_options(req.options);

    if (size) {
        if ((req.flags & FLAGS_BLOCK) == 0) {
            coap_add_data(pdu, size,
                          reinterpret_cast<unsigned char*>(payload));
        }
        else {
            coap_add_block(pdu, size,
                           reinterpret_cast<unsigned char*>(payload),
                           req.block.num, req.block.szx);
        }
    }

    req.pdu = pdu;
    return req;
}

int resolve_address(const char* server, struct sockaddr *dst) {
    struct addrinfo *res, *ainfo;
    struct addrinfo hints;
    static char addrstr[256];
    int error, len{-1};

    memset(addrstr, 0, sizeof(addrstr));
    if (server != nullptr) {
        memcpy(addrstr, server, strlen(server));
    }
    else {
        memcpy(addrstr, "localhost", 9);
    }

    memset ((char *)&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;

    error = getaddrinfo(addrstr, "", &hints, &res);

    if (error != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return error;
    }

    memset(dst, 0, sizeof(struct sockaddr));
    for (ainfo = res; ainfo != NULL; ainfo = ainfo->ai_next) {
        if (ainfo->ai_family == AF_INET || ainfo->ai_family == AF_INET6) {
            len = ainfo->ai_addrlen;
            memcpy(dst, ainfo->ai_addr, len);
            break;
        }
    }

    freeaddrinfo(res);
    return len;
}

} // namespace io
} // namespace cppa
