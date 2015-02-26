#include "test.hpp"

#include <iostream>

#include "caf/all.hpp"

using namespace std;
using namespace caf;

int main() {
  CAF_TEST(test_from_string);
  {
    auto val = from_string<message>("@<>+@i32 ( 42 )");
    CAF_CHECK(val == make_message(42));
    CAF_CHECK(val && val == from_string<message>(to_string(*val)));
  }
  {
    string str = "atom";
    auto val = from_string<message>("@<>+@atom ( '" + str + "' )");
    CAF_CHECK(val == make_message(atom("atom")));
    CAF_CHECK(val && val == from_string<message>(to_string(*val)));
  }
  {
    auto true_val = from_string<message>("@<>+bool ( true )");
    auto false_val = from_string<message>("@<>+bool ( false )");
    cout << "expect @<>+@bool (true): " << to_string(*true_val) << endl;
    cout << "expect @<>+@bool (false): " << to_string(*false_val) << endl;
    CAF_CHECK(true_val   == make_message(true));
    CAF_CHECK(false_val  == make_message(false));
    CAF_CHECK(true_val && true_val ==
              from_string<message>(to_string(*true_val)));
    CAF_CHECK(false_val && false_val ==
              from_string<message>(to_string(*false_val)));
  }
  {
    uint32_t a = 5;
    uint64_t b = 9;
    auto val = from_string<message>("@<>+@u32+@u64 (5, 9)");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(a, b));
  }
  {
    auto val = from_string<message>("@<>+@atom+@i32+@i32 ( 'add' 5 10 )");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(atom("add"), 5, 10));
  }
  {
    auto val = from_string<message>("42");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(42));
  }
  {
    auto val = from_string<message>("'atom' 'atom'");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(atom("atom"), atom("atom")));
  }
  {
    auto val = from_string<message>("\"string\"");
    CAF_CHECK(val.valid());
    CAF_CHECK(val && val == make_message("string"));
  }
  {
    auto val = from_string<message>("true");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(true)); 
  }
  {
    auto val = from_string<message>("'add' 5 10");
    CAF_CHECK(val.valid());
    CAF_CHECK(val == make_message(atom("add"), 5, 10));
    CAF_CHECK(val && val == from_string<message>(to_string(*val)));
  }
  return CAF_TEST_RESULT();
}
