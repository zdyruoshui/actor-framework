#include "pdu.h"

#include <iostream>

#include "cppa/io/coap_util.hpp"

namespace cppa {
namespace io {

coap_scope::coap_scope(coap_context_t* ctx)
    : ctx{ctx}
    , the_token{0, token_data}
    , default_method{1}
    , block{ .num = 0, .m = 0, .szx = 6 }
    , flags{0}
    , max_wait{0}
    , obs_wait{0} { }

coap_scope::~coap_scope() {
    // todo release context and strs?
}

/***  coap utility functions ***/


} // namespace io
} // namespace cppa
