// result-test.cpp - Unit tests for Result<T,E> type

#include "ot/common.h"
#include "vendor/doctest.h"

TEST_CASE("Result basic operations") {
  SUBCASE("ok() creates success result") {
    auto result = BoolResult<int>::ok(42);
    CHECK(result.is_ok());
    CHECK(!result.is_err());
    CHECK(result);  // operator bool
    CHECK(result.value() == 42);
    CHECK(*result == 42);  // operator*
  }

  SUBCASE("err() creates error result") {
    auto result = BoolResult<int>::err(false);
    CHECK(!result.is_ok());
    CHECK(result.is_err());
    CHECK(!result);  // operator bool
    CHECK(result.error() == false);
  }

  SUBCASE("value_or() returns value when ok") {
    auto result = BoolResult<int>::ok(42);
    CHECK(result.value_or(100) == 42);
  }

  SUBCASE("value_or() returns default when err") {
    auto result = BoolResult<int>::err(false);
    CHECK(result.value_or(100) == 100);
  }
}

TEST_CASE("Result with custom error type") {
  enum ErrorCode { INVALID_INPUT = 1, OUT_OF_RANGE = 2 };

  SUBCASE("custom error type") {
    auto result = Result<int, ErrorCode>::err(INVALID_INPUT);
    CHECK(result.is_err());
    CHECK(result.error() == INVALID_INPUT);
  }

  SUBCASE("success with custom error type") {
    auto result = Result<int, ErrorCode>::ok(123);
    CHECK(result.is_ok());
    CHECK(result.value() == 123);
  }
}

TEST_CASE("Result copy and move operations") {
  SUBCASE("copy constructor - ok") {
    auto result1 = BoolResult<int>::ok(42);
    auto result2 = result1;
    CHECK(result2.is_ok());
    CHECK(result2.value() == 42);
  }

  SUBCASE("copy constructor - err") {
    auto result1 = BoolResult<int>::err(false);
    auto result2 = result1;
    CHECK(result2.is_err());
    CHECK(result2.error() == false);
  }

  SUBCASE("copy assignment - ok") {
    auto result1 = BoolResult<int>::ok(42);
    auto result2 = BoolResult<int>::err(false);
    result2 = result1;
    CHECK(result2.is_ok());
    CHECK(result2.value() == 42);
  }

  SUBCASE("copy assignment - err") {
    auto result1 = BoolResult<int>::err(false);
    auto result2 = BoolResult<int>::ok(100);
    result2 = result1;
    CHECK(result2.is_err());
    CHECK(result2.error() == false);
  }

  SUBCASE("move constructor - ok") {
    auto result1 = BoolResult<int>::ok(42);
    auto result2 = static_cast<BoolResult<int>&&>(result1);
    CHECK(result2.is_ok());
    CHECK(result2.value() == 42);
  }

  SUBCASE("move assignment - ok") {
    auto result1 = BoolResult<int>::ok(42);
    auto result2 = BoolResult<int>::err(false);
    result2 = static_cast<BoolResult<int>&&>(result1);
    CHECK(result2.is_ok());
    CHECK(result2.value() == 42);
  }
}

TEST_CASE("parse_int tests") {
  SUBCASE("valid positive number") {
    auto result = parse_int("123");
    CHECK(result.is_ok());
    CHECK(result.value() == 123);
  }

  SUBCASE("valid negative number") {
    auto result = parse_int("-456");
    CHECK(result.is_ok());
    CHECK(result.value() == -456);
  }

  SUBCASE("valid zero") {
    auto result = parse_int("0");
    CHECK(result.is_ok());
    CHECK(result.value() == 0);
  }

  SUBCASE("explicit positive sign") {
    auto result = parse_int("+789");
    CHECK(result.is_ok());
    CHECK(result.value() == 789);
  }

  SUBCASE("empty string") {
    auto result = parse_int("");
    CHECK(result.is_err());
  }

  SUBCASE("null pointer") {
    auto result = parse_int(nullptr);
    CHECK(result.is_err());
  }

  SUBCASE("non-numeric characters") {
    auto result = parse_int("123abc");
    CHECK(result.is_err());
  }

  SUBCASE("only sign no digits") {
    auto result = parse_int("-");
    CHECK(result.is_err());
  }

  SUBCASE("only plus sign") {
    auto result = parse_int("+");
    CHECK(result.is_err());
  }

  SUBCASE("spaces not allowed") {
    auto result = parse_int(" 123");
    CHECK(result.is_err());
  }

  SUBCASE("trailing spaces not allowed") {
    auto result = parse_int("123 ");
    CHECK(result.is_err());
  }

  SUBCASE("max int value") {
    auto result = parse_int("2147483647");
    CHECK(result.is_ok());
    CHECK(result.value() == 2147483647);
  }

  SUBCASE("min int value") {
    auto result = parse_int("-2147483648");
    CHECK(result.is_ok());
    CHECK(result.value() == -2147483648);
  }

  SUBCASE("overflow positive") {
    auto result = parse_int("2147483648");
    CHECK(result.is_err());
  }

  SUBCASE("overflow negative") {
    auto result = parse_int("-2147483649");
    CHECK(result.is_err());
  }

  SUBCASE("very large number") {
    auto result = parse_int("99999999999999999");
    CHECK(result.is_err());
  }
}

TEST_CASE("Result pointer operator") {
  struct TestStruct {
    int value;
    int get_value() const { return value; }
  };

  SUBCASE("arrow operator on ok result") {
    auto result = BoolResult<TestStruct>::ok(TestStruct{42});
    CHECK(result->value == 42);
    CHECK(result->get_value() == 42);
  }
}

TEST_CASE("Result with different types") {
  SUBCASE("Result with string error") {
    auto make_result = [](bool success) -> Result<int, const char*> {
      if (success) {
        return Result<int, const char*>::ok(42);
      } else {
        return Result<int, const char*>::err("something went wrong");
      }
    };

    auto ok_result = make_result(true);
    CHECK(ok_result.is_ok());
    CHECK(ok_result.value() == 42);

    auto err_result = make_result(false);
    CHECK(err_result.is_err());
    CHECK(strcmp(err_result.error(), "something went wrong") == 0);
  }
}