#include "test.hpp"

#include "caf/all.hpp"

using namespace std;
using namespace caf;

int main() {
  CAF_TEST(test_from_string);
  {
    string str = "@<>+@i32 ( 42 )";
    auto val = from_string<message>(str);
    if (!val) {
      CAF_PRINTERR("from_string returned 'none'");
    } else {
      CAF_CHECK(val == make_message(42));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), str);
    }
  }
  {
    string str = "@<>+@atom ( 'atom' )";
    auto val = from_string<message>(str);
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for atom value");
    } else {
      CAF_CHECK(val == make_message(atom("atom")));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), str);
    }
  }
  {
    auto true_str = "@<>+bool ( true )";
    auto false_str = "@<>+bool ( false )";
    auto true_val = from_string<message>(true_str);
    auto false_val = from_string<message>(false_str);
    if (!true_val) {
      CAF_PRINTERR("from_string returned 'none' for true_val");
    } else {
      CAF_CHECK(true_val == make_message(true));
      CAF_CHECK(true_val == from_string<message>(to_string(*true_val)));
      CAF_CHECK_EQUAL(to_string(*true_val), true_str);
    }
    if (!false_val) {
      CAF_PRINTERR("from_string returned 'none' for false_val");
    } else {
      CAF_CHECK(false_val == make_message(false));
      CAF_CHECK(false_val == from_string<message>(to_string(*false_val)));
      CAF_CHECK_EQUAL(to_string(*false_val), false_str);

    }
  }
  {
    uint32_t a = 5;
    uint64_t b = 9;
    auto str = "@<>+@u32+@u64 ( 5, 9 )";
    auto val = from_string<message>(str);
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for mixed case");
    } else {
      CAF_CHECK(val == make_message(a, b));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), str);
    }
  }
  {
    auto str = "@<>+@atom+@i32+@i32 ( 'add', 5, 10 )";
    auto val = from_string<message>(str);
    if (!val) {
      CAF_PRINTERR("from_string returned 'none'");
    } else {
      CAF_CHECK(val == make_message(atom("add"), 5, 10));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), str);
    }
  }
  {
    auto val = from_string<message>("42");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none'");
    } else {
      CAF_CHECK(val == make_message(42));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), "@<>+@i32 ( 42 )");
    }
  }
  {
    auto val = from_string<message>("'atom' 'atom'");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none'");
    }
    CAF_CHECK(val == make_message(atom("atom"), atom("atom")));
    CAF_CHECK(val == from_string<message>(to_string(*val)));
    CAF_CHECK_EQUAL(to_string(*val), "@<>+@atom+@atom ( 'atom', 'atom' )");
  }
  {
    auto val = from_string<message>("\"string\"");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for boolean");
    } else {
      CAF_CHECK(val == make_message("string"));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), "@<>+@str ( \"string\" )");
    }
  }
  {
    auto val = from_string<message>("true");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for boolean");
    } else {
      CAF_CHECK(val == make_message(true));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK(to_string(*val) == "@<>+bool ( true )");
    }
  }
  {
    auto val = from_string<message>("'add' 5 10");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for mixed values");
    } else {
      CAF_CHECK(val == make_message(atom("add"), 5, 10));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
      CAF_CHECK_EQUAL(to_string(*val), "@<>+@atom+@i32+@i32 ( 'add', 5, 10 )");
    }
  }
  {
    auto val = from_string<message>("3.131f");
    if (!val) {
      CAF_PRINTERR("from_string returned 'none' for float value");
    } else {
      CAF_CHECK(val == make_message(3.131f));
      CAF_CHECK(val == from_string<message>(to_string(*val)));
    }
  }
  return CAF_TEST_RESULT();
}

