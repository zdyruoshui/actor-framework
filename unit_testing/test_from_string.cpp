#include "test.hpp"

#include "caf/all.hpp"

#include <iostream>

using namespace std;
using namespace caf;

int main() {
  // test of notation with signature
  // bool
  // int 
  // atom
  // string
  // double
  // float
  CAF_TEST(test_from_string);
  {
    auto val = from_string<message>("@<>+@i32 ( 42 )");
    CAF_CHECK(val == make_message(42));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
  }
  {
    string str = "atom";
    auto val = from_string<message>("@<>+@atom ( '" + str + "' )");
    CAF_CHECK(val == make_message(atom("atom")));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
  }
  {
    auto val = from_string<message>("@<>+bool ( true )");
    CAF_CHECK(val == make_message(true));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
  }
  {
    uint32_t a = 5;
    uint64_t b = 9;
    auto val = from_string<message>("@<>+@u32+@u64 (5, 9)");
    CAF_CHECK(val == make_message(a, b));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
  }
  {
    auto val = from_string<message>("@<>+@atom+@i32+@i32 ( 'add' 5 10 )");
    CAF_CHECK(val == make_message(atom("add"), 5, 10));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
  }
  // test of smart notation
//  {
//  // segfaults, wtf?
//    auto val = from_string<message>("42");
//    CAF_CHECK(val == make_message("42"));
//    CAF_CHECK(val == from_string<message>("@<>+int ( 42 )"));
//  }
  {
    auto val = from_string<message>("'atom' 'atom'");
    CAF_CHECK(val == make_message(atom("atom"), atom("atom")));
    CAF_CHECK(val == from_string<message>("@<>+@atom+@atom ( 'atom', 'atom' )"));
  }
  {
    auto val = from_string<message>("true");
    CAF_CHECK(val == make_message(true));
    CAF_CHECK(val == from_string<message>("@<>+bool ( true )"));
  }
  {
    auto val = from_string<message>("'add' 5 10");
    CAF_CHECK(val == make_message(atom("add"), 5, 10));
  }
  return CAF_TEST_RESULT();
}
