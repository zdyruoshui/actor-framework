
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

using namespace std;

namespace cppa {
namespace io {

/***  coap utility classes ***/

/***  coap utility functions ***/

void generate_token(str* token, size_t bytes) {
//    vector<uint32_t> buf(bytes);
//    random_device rdev{};
////    independent_bits_engine<mt19937_64,64,uint_fast64_t> gen{rdev()};
////    uniform_int_distribution<uint_fast64_t> dis{};
//    independent_bits_engine<mt19937,32,uint_fast32_t> gen{rdev()};
//    uniform_int_distribution<uint_fast32_t> dis{};
//    for (size_t i = 0; i < (bytes); ++i) {
//        buf[i] = dis(gen);
//    }
//    memcpy(data, buf.data(), bytes);
    std::random_device r;
    std::uniform_int_distribution<uint8_t> dist;
    std::generate_n(token->s, bytes, std::bind(dist,std::ref(r)));
    token->length = bytes;
}

coap_context_t* get_context(const char *node, const char *port,
                            coap_endpoint_t ** interface) {
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

            *interface = coap_new_endpoint(&addr, 0);
            if (!interface)
                continue;

            ctx = coap_new_context();
            if (ctx) {
                // binding succeeded
                break;
            } else {
                coap_free_endpoint(*interface);
                *interface = nullptr;
            }
        }
    }
  
    if (!ctx) {
        fprintf(stderr, "no context available for interface '%s'\n", node);
    }

    freeaddrinfo(result);
    return ctx;
}

void request_handler(struct coap_context_t  *ctx,
                     const coap_endpoint_t *,
                     const coap_address_t *remote,
                     coap_pdu_t *,
                     coap_pdu_t *received,
                     const coap_tid_t) {
    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    coap_pdu_t *pdu = nullptr;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    // send ACK to CON messages
    switch (received->hdr->type) {
        case COAP_MESSAGE_CON:
            // (from libcoap)
            // acknowledge received response if confirmable (TODO: check Token)
            coap_send_ack(ctx, ptr->m_interface, remote, received);
            break;
        case COAP_MESSAGE_RST:
            CPPA_LOGF_DEBUG("got RST\n");
            return;
        default:
            // other messages do not require special responses
            break;
    }
    // Check if block option is set.
    block_opt = get_block(received, &opt_iter);
    if (!block_opt) { // no block option set
        cout << "[request_handler] message without block opt" << endl;
        size_t len;
        unsigned char *databuf;
        if (coap_get_data(received, &len, &databuf)) {
            cout << "[request_handler] incoming data" << endl;
            message_header hdr;
            any_tuple msg;
            binary_deserializer bd(databuf, len,
                                   &(ptr->m_parent->get_namespace()),
                                   nullptr);
            try {
                ptr->m_meta_hdr->deserialize(&hdr, &bd);
                ptr->m_meta_msg->deserialize(&msg, &bd);
            }
            catch (exception& e) {
                CPPA_LOGF_ERROR("exception during read_message: "
                                << detail::demangle(typeid(e))
                                << ", what(): " << e.what());
            }
            CPPA_LOGF_DEBUG("deserialized: " << to_string(hdr)
                                             << " " << to_string(msg));
            match(msg) (
                on(atom("HANDSHAKE"), arg_match) >> [&](node_id_ptr node) {
                    cout << "[request_handler] recieved handshake message '"
                         <<  to_string(node) << "'" << endl;
                    ptr->set_node(node);
                    if (!ptr->m_parent->register_peer(ptr->node(), ptr)) {
                        CPPA_LOGF_ERROR("multiple incoming connections "
                                        "from the same node");
                        return;
                    }
                    if (ptr->m_known_nodes.find(node) == ptr->m_known_nodes.end()) {
                        coap_address_t addr;
                        coap_address_init(&addr);
                        memcpy(&addr, remote, sizeof(coap_address_t));
                        ptr->m_known_nodes.emplace(node, move(addr));
                    }
                    // answer handshake
                    util::buffer snd_buf(COAP_MAX_PDU_SIZE, COAP_MAX_PDU_SIZE);
                    binary_serializer bs(&snd_buf, &(ptr->m_parent->get_namespace()));
                    bs << message_header{};
                    bs << make_any_tuple(atom("HANDSHAKE"), ptr->m_parent->node());
                    ptr->send_coap_message(remote,
                                           snd_buf.data(), snd_buf.size(),
                                           nullptr,
                                           COAP_MESSAGE_CON, received->hdr->code);
                    cout << "[request_handler] sent answer" << endl;
                },
                on(atom("MONITOR"), arg_match) >> [&](const node_id_ptr&,
                                                      actor_id) {
                    CPPA_LOGF_DEBUG("[request_handler] received MONITOR msg");
                },
                on(atom("KILL_PROXY"), arg_match) >> [&](const node_id_ptr&,
                                                         actor_id, uint32_t) {
                    CPPA_LOGF_DEBUG("[request_handler] received KILL msg");
                },
                on(atom("LINK"), arg_match) >> [&](const actor_addr&) {
                    CPPA_LOGF_DEBUG("[request_handler] received LINK msg");
                },
                on(atom("UNLINK"), arg_match) >> [&](const actor_addr&) {
                    CPPA_LOGF_DEBUG("[request_handler] received UNLINK msg");
                },
                on(atom("ADD_TYPE"), arg_match) >> [&](uint32_t,
                                                       const string&) {
                    CPPA_LOGF_DEBUG("[request_handler] received TYPE msg");
                },
                others() >> [&] {
                    hdr.deliver(move(msg));
                }
            );
        }
    }
    else {
        CPPA_LOGF_ERROR("[request handler] currently ignoring "
                        "block option messages");
        return;
    }
    coap_delete_pdu(pdu);
}

void response_handler(struct coap_context_t  *ctx,
                      const coap_endpoint_t*, // local_interface
                      const coap_address_t *remote,
                      coap_pdu_t *sent,
                      coap_pdu_t *received,
                      const coap_tid_t) {

    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    coap_pdu_t *pdu = nullptr;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    unsigned char buf[4];
    size_t len;
    unsigned char *databuf;
    coap_tid_t tid;

    CPPA_LOGF_DEBUG("process incoming response: " <<
                    (received->hdr->code >> 5)    <<
                    "."                           <<
                    setw(2) << setfill('0')       <<
                    (received->hdr->code & 0x1F));

    // check if this is a response to an original request
    auto itr = find_if(begin(ptr->m_requests), end(ptr->m_requests),
                 [&] (const map<unsigned short,
                                     transaction_based_peer::coap_request>::value_type& t) {
        return received->hdr->token_length == t.second.the_token.length &&
          memcmp(received->hdr->token, t.second.the_token.s, t.second.the_token.length) == 0;
    });
    if (itr == end(ptr->m_requests)) { // new request
        // drop if this was just some message, or send RST in case of notification
        if (!sent && (received->hdr->type == COAP_MESSAGE_CON ||
                      received->hdr->type == COAP_MESSAGE_NON)) {
            coap_send_rst(ctx, ptr->m_interface, remote, received);
        }
        return;
    }
    else { // is response to an earlier request

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
                cout << "[message handler] message without block opt" << endl;
                if (coap_get_data(received, &len, &databuf)) {
                    cout << "[message handler] incoming data" << endl;
//                    switch(ptr->m_state) {
//                        case transaction_based_peer::read_state::wait_for_process_info: {
//                            cout << "[message handler] handshake" << endl;
//                            uint32_t process_id;
//                            node_id::host_id_type host_id;
//                            memcpy(&process_id, databuf, sizeof(uint32_t));
//                            memcpy(host_id.data(), databuf + sizeof(uint32_t),
//                                   node_id::host_id_size);
//                            ptr->set_node(new node_id(process_id, host_id));
//    //                        if (ptr->m_parent->node() == ptr->node()) {
//    //                            cout << "*** middleman warning: "
//    //                                         "incoming connection from self"
//    //                                 << endl;
//    //                            return;
//    //                        }
//    //                        CPPA_LOGF_DEBUG("read process info: " << to_string(node()));
//                            if (!ptr->m_parent->register_peer(ptr->node(), ptr)) {
//                                CPPA_LOGF_ERROR("multiple incoming connections "
//                                                "from the same node");
//                                return;
//                            }
//                            // initialization done
//                            ptr->m_state = transaction_based_peer::read_state::read_message;
//                            ptr->m_rd_buf.clear();
//    //                        ptr->m_rd_buf.final_size(sizeof(uint32_t));
//                            // send back own information with ACK
//                            break;
//                        }
//                        case transaction_based_peer::read_state::read_message: {
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
                            catch (exception& e) {
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
                                                                         actor_id, uint32_t) {
                                    CPPA_LOGF_DEBUG("[message_handler] received KILL msg");
                                },
                                on(atom("LINK"), arg_match) >> [&](const actor_addr&) {
                                    CPPA_LOGF_DEBUG("[message_handler] received LINK msg");
                                },
                                on(atom("UNLINK"), arg_match) >> [&](const actor_addr&) {
                                    CPPA_LOGF_DEBUG("[message_handler] received UNLINK msg");
                                },
                                on(atom("ADD_TYPE"), arg_match) >> [&](uint32_t,
                                                                       const string&) {
                                    CPPA_LOGF_DEBUG("[message_handler] received TYPE msg");
                                },
                                others() >> [&] {
                                    hdr.deliver(move(msg));
                                }
                            );
//                            break;
//                        }
//                    }
                }
            } else {
                cout << "[message handler] message with block opt" << endl;
                unsigned short blktype = opt_iter.type;
                if (coap_get_data(received, &len, &databuf)) {
                    // TODO: handle data
                    cout << "[message handler] handle incoming data missing" << endl;
                }
                if (COAP_OPT_BLOCK_MORE(block_opt)) { // more bit is set
                    debug("found the M bit, block size is %u, block nr. %u\n",
                          COAP_OPT_BLOCK_SZX(block_opt),
                          COAP_OPT_BLOCK_NUM(block_opt));

                    // create pdu with request for next block
                    pdu = coap_new_pdu();
                    if (!pdu) {
                        throw runtime_error("[message handler] failed to create coap pdu");
                    }
                    pdu->hdr->type = request.pdu->hdr->type;
                    pdu->hdr->id = coap_new_message_id(ctx);
                    pdu->hdr->code = request.pdu->hdr->code;

                    request.the_token.length = 8;
                    generate_token(&request.the_token);
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
                    cout << "[message handler] message with block opt"
                                 " ('more' flag is not set -> unhandled)"
                              << endl;
                }
            }
        } else {			/* no 2.05 */

            // check if an error was signaled and output payload if so
            if (COAP_RESPONSE_CLASS(received->hdr->code) >= 4) {
                CPPA_LOGF_ERROR((received->hdr->code >> 5) << "." <<
                                setw(2) << setfill('0') <<
                                (received->hdr->code & 0x1F));
                if (coap_get_data(received, &len, &databuf)) {
                    cout << "[message handler] error + data -> unhandled"
                              << endl;
                }
                fprintf(stderr, "\n");
            }
        }

        // send new request, if needed <<-- WHY???
        // if (pdu) {
        //     auto ret = coap_send(ctx, ptr->m_interface, remote, pdu);
        //     if (ret == COAP_INVALID_TID) {
        //         throw runtime_error("[message_handler] error sending response");
        //     }
        // }
        coap_delete_pdu(pdu);

        // our job is done, we can exit at any time
        // ready = coap_check_option(received, COAP_OPTION_SUBSCRIPTION, &opt_iter) == NULL;
    }
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
    cout << "[new_request] new request with " << size << " payload"  << endl;
    auto ptr = reinterpret_cast<transaction_based_peer*>(ctx->app);
    transaction_based_peer::coap_request req;
    coap_pdu_t*  pdu{nullptr};
    req.data = payload;
    req.size = size;
    // currently on block messages
//    if (size) {
//        req.block.num = size;
//        req.block.szx = (coap_fls(size >> 4) - 1) & 0x07;
//        req.flags |= FLAGS_BLOCK;
//        unsigned char buf[4];	/* hack: temporarily take encoded bytes */
//        unsigned short opt;
//        if (method != COAP_REQUEST_DELETE) {
//            opt = method == COAP_REQUEST_GET ? COAP_OPTION_BLOCK2 : COAP_OPTION_BLOCK1;
//            coap_insert(
//                &req.options,
//                new_option_node(
//                    opt,
//                    coap_encode_var_bytes(buf, (req.block.num << 4 | req.block.szx)),
//                    buf
//                ),
//                order_opts
//            );
//        }
//    }
    if (!(pdu = coap_new_pdu())) {
        throw runtime_error("failed to create coap pdu");
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
    if (size > 0) {
        coap_add_data(pdu, size,
                      reinterpret_cast<unsigned char*>(payload));
//        if ((req.flags & FLAGS_BLOCK) == 0) {
//            coap_add_data(pdu, size,
//                          reinterpret_cast<unsigned char*>(payload));
//        }
//        else {
//            coap_add_block(pdu, size,
//                           reinterpret_cast<unsigned char*>(payload),
//                           req.block.num, req.block.szx);
//        }
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

void parse_uri(const char *arg, str& proxy, coap_list_t* optlist, coap_uri_t& uri) {
    unsigned char portbuf[2];
#define BUFSIZE 40
    unsigned char _buf[BUFSIZE];
    unsigned char *buf = _buf;
    size_t buflen;
    int res;

    if (proxy.length) {		/* create Proxy-Uri from argument */
        size_t len = strlen(arg);
        while (len > 270) {
            coap_insert(&optlist,
                        new_option_node(COAP_OPTION_PROXY_URI,
                        270, (unsigned char *)arg),
                        order_opts);
            len -= 270;
            arg += 270;
        }

        coap_insert(&optlist,
                    new_option_node(COAP_OPTION_PROXY_URI,
                    len, (unsigned char *)arg),
                    order_opts);
    } else {			/* split arg into Uri-* options */
        coap_split_uri((unsigned char *)arg, strlen(arg), &uri );

        if (!is_default_port(&uri)) {
            coap_insert(&optlist,
                        new_option_node(COAP_OPTION_URI_PORT,
                        coap_encode_var_bytes(portbuf, uri.port),
                                              portbuf),
                        order_opts);
        }

        if (uri.path.length) {
            buflen = BUFSIZE;
            res = coap_split_path(uri.path.s, uri.path.length, buf, &buflen);

            while (res--) {
                coap_insert(&optlist, new_option_node(COAP_OPTION_URI_PATH,
                            COAP_OPT_LENGTH(buf),
                            COAP_OPT_VALUE(buf)),
                            order_opts);

                buf += COAP_OPT_SIZE(buf);
            }
        }

        if (uri.query.length) {
            buflen = BUFSIZE;
            buf = _buf;
            res = coap_split_query(uri.query.s, uri.query.length, buf, &buflen);

            while (res--) {
                coap_insert(&optlist, new_option_node(COAP_OPTION_URI_QUERY,
                            COAP_OPT_LENGTH(buf),
                            COAP_OPT_VALUE(buf)),
                            order_opts);

                buf += COAP_OPT_SIZE(buf);
            }
        }
    }
}

} // namespace io
} // namespace cppa
