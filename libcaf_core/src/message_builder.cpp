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

#include <vector>

#include "caf/message_builder.hpp"
#include "caf/message_handler.hpp"
#include "caf/uniform_type_info.hpp"

#include "caf/detail/message_data.hpp"

namespace caf {

class message_builder::dynamic_msg_data : public detail::message_data {
 public:
  dynamic_msg_data() : m_type_token(0xFFFFFFFF) {
    // nop
  }

  dynamic_msg_data(const dynamic_msg_data& other)
      : detail::message_data(other),
        m_type_token(0xFFFFFFFF) {
    for (auto& e : other.m_elements) {
      add_to_type_token(e->ti->type_nr());
      m_elements.push_back(e->copy());
    }
  }

  dynamic_msg_data(std::vector<uniform_value>&& data)
      : m_elements(std::move(data)),
        m_type_token(0xFFFFFFFF) {
    for (auto& e : m_elements) {
      add_to_type_token(e->ti->type_nr());
    }
  }

  ~dynamic_msg_data();

  const void* at(size_t pos) const override {
    CAF_ASSERT(pos < size());
    return m_elements[pos]->val;
  }

  void* mutable_at(size_t pos) override {
    CAF_ASSERT(pos < size());
    return m_elements[pos]->val;
  }

  size_t size() const override {
    return m_elements.size();
  }

  dynamic_msg_data* copy() const override {
    return new dynamic_msg_data(*this);
  }

  bool match_element(size_t pos, uint16_t typenr,
                     const std::type_info* rtti) const override {
    CAF_ASSERT(typenr != 0 || rtti != nullptr);
    auto uti = m_elements[pos]->ti;
    if (uti->type_nr() != typenr) {
      return false;
    }
    return typenr != 0 || uti->equal_to(*rtti);
  }

  const char* uniform_name_at(size_t pos) const override {
    return m_elements[pos]->ti->name();
  }

  uint16_t type_nr_at(size_t pos) const override {
    return m_elements[pos]->ti->type_nr();
  }

  uint32_t type_token() const override {
    return m_type_token;
  }

  void append(uniform_value&& what) {
    add_to_type_token(what->ti->type_nr());
    m_elements.push_back(std::move(what));
  }

  void add_to_type_token(uint16_t typenr) {
    m_type_token = (m_type_token << 6) | typenr;
  }

  void clear() {
    m_elements.clear();
    m_type_token = 0xFFFFFFFF;
  }

  std::vector<uniform_value> m_elements;
  uint32_t m_type_token;
};

message_builder::dynamic_msg_data::~dynamic_msg_data() {
  // avoid weak-vtables warning
}

message_builder::message_builder() {
  init();
}

message_builder::~message_builder() {
  // nop
}

void message_builder::init() {
  // this should really be done by delegating
  // constructors, but we want to support
  // some compilers without that feature...
  m_data.reset(new dynamic_msg_data);
}

void message_builder::clear() {
  data()->clear();
}

size_t message_builder::size() const {
  return data()->m_elements.size();
}

bool message_builder::empty() const {
  return size() == 0;
}

message_builder& message_builder::append(uniform_value what) {
  data()->append(std::move(what));
  return *this;
}

message message_builder::to_message() const {
  // this const_cast is safe, because the message is
  // guaranteed to detach its data before modifying it
  return message{const_cast<dynamic_msg_data*>(data())};
}

message message_builder::move_to_message() {
  message result;
  result.vals().reset(static_cast<dynamic_msg_data*>(m_data.release()), false);
  return result;
}

optional<message> message_builder::apply(message_handler handler) {
  // avoid detaching of m_data by moving the data to a message object,
  // calling message::apply and moving the data back
  message::data_ptr ptr;
  ptr.reset(static_cast<dynamic_msg_data*>(m_data.release()), false);
  message msg{std::move(ptr)};
  auto res = msg.apply(std::move(handler));
  m_data.reset(msg.vals().release(), false);
  return res;
}

message_builder::dynamic_msg_data* message_builder::data() {
  // detach if needed, i.e., assume further non-const
  // operations on m_data can cause race conditions if
  // someone else holds a reference to m_data
  if (!m_data->unique()) {
    intrusive_ptr<ref_counted> tmp{std::move(m_data)};
    m_data.reset(static_cast<dynamic_msg_data*>(tmp.get())->copy());
  }
  return static_cast<dynamic_msg_data*>(m_data.get());
}

const message_builder::dynamic_msg_data* message_builder::data() const {
  return static_cast<const dynamic_msg_data*>(m_data.get());
}

} // namespace caf
