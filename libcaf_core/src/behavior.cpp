/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/none.hpp"

#include "caf/behavior.hpp"
#include "caf/message_handler.hpp"

namespace caf {

behavior::behavior(const message_handler& mh) : m_impl(mh.as_behavior_impl()) {
  // nop
}

void behavior::assign(message_handler other) {
  m_impl.swap(other.m_impl);
}

void behavior::assign(behavior other) {
  m_impl.swap(other.m_impl);
}

void behavior::assign(detail::behavior_impl* ptr) {
  CAF_ASSERT(ptr != nullptr);
  m_impl.reset(ptr);
}

} // namespace caf
