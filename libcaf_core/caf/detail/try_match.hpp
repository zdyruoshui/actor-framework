/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2014                                                  *
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

#ifndef CAF_DETAIL_MATCHES_HPP
#define CAF_DETAIL_MATCHES_HPP

#include <array>
#include <numeric>
#include <typeinfo>

#include "caf/atom.hpp"
#include "caf/message.hpp"
#include "caf/wildcard_position.hpp"

#include "caf/detail/type_list.hpp"
#include "caf/detail/pseudo_tuple.hpp"

namespace caf {
namespace detail {

bool match_element(const atom_value&, const std::type_info* type,
                   const message_iterator& iter, void** storage);

bool match_atom_constant(const atom_value&, const std::type_info* type,
                         const message_iterator& iter, void** storage);

struct meta_element {
  atom_value v;
  const std::type_info* type;
  bool (*fun)(const atom_value&, const std::type_info*,
              const message_iterator&, void**);
};

template <class T>
struct meta_element_factory {
  static meta_element create() {
    return {static_cast<atom_value>(0), &typeid(T), match_element};
  }
};

template <atom_value V>
struct meta_element_factory<atom_constant<V>> {
  static meta_element create() {
    return {V, &typeid(atom_value), match_atom_constant};
  }
};

template <>
struct meta_element_factory<anything> {
  static meta_element create() {
    return {static_cast<atom_value>(0), nullptr, nullptr};
  }
};

template <class TypeList>
struct meta_elements;

template <class... Ts>
struct meta_elements<type_list<Ts...>> {
  std::array<meta_element, sizeof...(Ts)> arr;
  meta_elements() : arr{{meta_element_factory<Ts>::create()...}} {
    // nop
  }
};

bool try_match(const message& msg,
               const meta_element* pattern_begin,
               size_t pattern_size,
               void** out = nullptr);

} // namespace detail
} // namespace caf

#endif // CAF_DETAIL_MATCHES_HPP
