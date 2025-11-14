// string-test.cpp - Unit tests for ou::string class

#include "ot/lib/string.hpp"
#include "vendor/doctest.h"

TEST_CASE("string construction") {
  SUBCASE("default constructor") {
    ou::string s;
    CHECK(s.length() == 0);
    CHECK(s.empty());
    CHECK(strcmp(s.c_str(), "") == 0);
  }

  SUBCASE("from C string") {
    ou::string s("hello");
    CHECK(s.length() == 5);
    CHECK(!s.empty());
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("from C string with length") {
    ou::string s("hello world", 5);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("copy constructor") {
    ou::string s1("test");
    ou::string s2(s1);
    CHECK(s2.length() == 4);
    CHECK(strcmp(s2.c_str(), "test") == 0);
    CHECK(strcmp(s1.c_str(), "test") == 0); // original unchanged
  }

  SUBCASE("move constructor") {
    ou::string s1("test");
    ou::string s2(static_cast<ou::string &&>(s1));
    CHECK(s2.length() == 4);
    CHECK(strcmp(s2.c_str(), "test") == 0);
    CHECK(s1.length() == 0); // moved from
  }
}

TEST_CASE("string assignment") {
  SUBCASE("copy assignment") {
    ou::string s1("hello");
    ou::string s2;
    s2 = s1;
    CHECK(s2.length() == 5);
    CHECK(strcmp(s2.c_str(), "hello") == 0);
    CHECK(strcmp(s1.c_str(), "hello") == 0); // original unchanged
  }

  SUBCASE("move assignment") {
    ou::string s1("hello");
    ou::string s2;
    s2 = static_cast<ou::string &&>(s1);
    CHECK(s2.length() == 5);
    CHECK(strcmp(s2.c_str(), "hello") == 0);
    CHECK(s1.length() == 0); // moved from
  }

  SUBCASE("C string assignment") {
    ou::string s;
    s = "world";
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "world") == 0);
  }
}

TEST_CASE("string append operations") {
  SUBCASE("append C string") {
    ou::string s("hello");
    s.append(" world");
    CHECK(s.length() == 11);
    CHECK(strcmp(s.c_str(), "hello world") == 0);
  }

  SUBCASE("append C string with length") {
    ou::string s("hello");
    s.append(" world!", 6);
    CHECK(s.length() == 11);
    CHECK(strcmp(s.c_str(), "hello world") == 0);
  }

  SUBCASE("append another string") {
    ou::string s1("hello");
    ou::string s2(" world");
    s1.append(s2);
    CHECK(s1.length() == 11);
    CHECK(strcmp(s1.c_str(), "hello world") == 0);
  }

  SUBCASE("operator+= with C string") {
    ou::string s("hello");
    s += " world";
    CHECK(s.length() == 11);
    CHECK(strcmp(s.c_str(), "hello world") == 0);
  }

  SUBCASE("operator+= with string") {
    ou::string s1("hello");
    ou::string s2(" world");
    s1 += s2;
    CHECK(s1.length() == 11);
    CHECK(strcmp(s1.c_str(), "hello world") == 0);
  }

  SUBCASE("operator+= with char") {
    ou::string s("hello");
    s += '!';
    CHECK(s.length() == 6);
    CHECK(strcmp(s.c_str(), "hello!") == 0);
  }

  SUBCASE("push_back") {
    ou::string s("test");
    s.push_back('!');
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "test!") == 0);
  }
}

TEST_CASE("string insert operations") {
  SUBCASE("insert at beginning") {
    ou::string s("world");
    s.insert(0, 1, 'H');
    CHECK(s.length() == 6);
    CHECK(strcmp(s.c_str(), "Hworld") == 0);
  }

  SUBCASE("insert in middle") {
    ou::string s("helo");
    s.insert(2, 1, 'l');
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("insert at end") {
    ou::string s("hello");
    s.insert(5, 1, '!');
    CHECK(s.length() == 6);
    CHECK(strcmp(s.c_str(), "hello!") == 0);
  }

  SUBCASE("insert multiple characters") {
    ou::string s("he");
    s.insert(2, 3, 'l');
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "helll") == 0);
  }

  SUBCASE("insert zero characters") {
    ou::string s("hello");
    s.insert(2, 0, 'x');
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("insert beyond end (clamps to end)") {
    ou::string s("hello");
    s.insert(100, 1, '!');
    CHECK(s.length() == 6);
    CHECK(strcmp(s.c_str(), "hello!") == 0);
  }

  SUBCASE("insert into empty string") {
    ou::string s;
    s.insert(0, 3, 'a');
    CHECK(s.length() == 3);
    CHECK(strcmp(s.c_str(), "aaa") == 0);
  }
}

TEST_CASE("string access operations") {
  SUBCASE("operator[] const") {
    const ou::string s("hello");
    CHECK(s[0] == 'h');
    CHECK(s[4] == 'o');
  }

  SUBCASE("operator[] non-const") {
    ou::string s("hello");
    s[0] = 'H';
    CHECK(s[0] == 'H');
    CHECK(strcmp(s.c_str(), "Hello") == 0);
  }

  SUBCASE("at()") {
    ou::string s("test");
    CHECK(s.at(0) == 't');
    CHECK(s.at(3) == 't');
  }
}

TEST_CASE("string substr") {
  SUBCASE("substr middle portion") {
    ou::string s("hello world");
    ou::string sub = s.substr(0, 5);
    CHECK(sub.length() == 5);
    CHECK(strcmp(sub.c_str(), "hello") == 0);
  }

  SUBCASE("substr to end") {
    ou::string s("hello world");
    ou::string sub = s.substr(6, 100);
    CHECK(sub.length() == 5);
    CHECK(strcmp(sub.c_str(), "world") == 0);
  }

  SUBCASE("substr beyond length") {
    ou::string s("hello");
    ou::string sub = s.substr(10, 5);
    CHECK(sub.length() == 0);
    CHECK(strcmp(sub.c_str(), "") == 0);
  }

  SUBCASE("substr from position to end (one-arg)") {
    ou::string s("hello world");
    ou::string sub = s.substr(6);
    CHECK(sub.length() == 5);
    CHECK(strcmp(sub.c_str(), "world") == 0);
  }

  SUBCASE("substr from beginning to end (one-arg)") {
    ou::string s("hello");
    ou::string sub = s.substr(0);
    CHECK(sub.length() == 5);
    CHECK(strcmp(sub.c_str(), "hello") == 0);
  }

  SUBCASE("substr from middle to end (one-arg)") {
    ou::string s("abcdefgh");
    ou::string sub = s.substr(3);
    CHECK(sub.length() == 5);
    CHECK(strcmp(sub.c_str(), "defgh") == 0);
  }

  SUBCASE("substr beyond end returns empty (one-arg)") {
    ou::string s("hello");
    ou::string sub = s.substr(10);
    CHECK(sub.length() == 0);
    CHECK(strcmp(sub.c_str(), "") == 0);
  }

  SUBCASE("substr at end position (one-arg)") {
    ou::string s("hello");
    ou::string sub = s.substr(5);
    CHECK(sub.length() == 0);
    CHECK(strcmp(sub.c_str(), "") == 0);
  }
}

TEST_CASE("string compare") {
  SUBCASE("equal strings") {
    ou::string s1("hello");
    ou::string s2("hello");
    CHECK(s1.compare(s2) == 0);
    CHECK(s1.compare("hello") == 0);
  }

  SUBCASE("less than") {
    ou::string s1("abc");
    ou::string s2("abd");
    CHECK(s1.compare(s2) < 0);
    CHECK(s1.compare("abd") < 0);
  }

  SUBCASE("greater than") {
    ou::string s1("xyz");
    ou::string s2("abc");
    CHECK(s1.compare(s2) > 0);
    CHECK(s1.compare("abc") > 0);
  }
}

TEST_CASE("string clear and capacity") {
  SUBCASE("clear") {
    ou::string s("hello");
    s.clear();
    CHECK(s.length() == 0);
    CHECK(s.empty());
    CHECK(strcmp(s.c_str(), "") == 0);
  }

  SUBCASE("reserve increases capacity") {
    ou::string s;
    s.reserve(100);
    CHECK(s.capacity() >= 100);
    CHECK(s.length() == 0);
  }

  SUBCASE("ensure_capacity") {
    ou::string s;
    s.ensure_capacity(50);
    CHECK(s.capacity() >= 50);
  }
}

TEST_CASE("string erase operations") {
  SUBCASE("erase from beginning") {
    ou::string s("hello world");
    s.erase(0, 6);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "world") == 0);
  }

  SUBCASE("erase from middle") {
    ou::string s("hello world");
    s.erase(5, 6);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase single character") {
    ou::string s("hello");
    s.erase(1, 1);
    CHECK(s.length() == 4);
    CHECK(strcmp(s.c_str(), "hllo") == 0);
  }

  SUBCASE("erase to end of string") {
    ou::string s("hello world");
    s.erase(5, 100);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase entire string") {
    ou::string s("hello");
    s.erase(0, 5);
    CHECK(s.length() == 0);
    CHECK(strcmp(s.c_str(), "") == 0);
  }

  SUBCASE("erase beyond string does nothing") {
    ou::string s("hello");
    s.erase(10, 5);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase zero length does nothing") {
    ou::string s("hello");
    s.erase(2, 0);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase at end position") {
    ou::string s("hello");
    s.erase(5, 1);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase from empty string") {
    ou::string s;
    s.erase(0, 5);
    CHECK(s.length() == 0);
    CHECK(strcmp(s.c_str(), "") == 0);
  }

  SUBCASE("multiple erase operations") {
    ou::string s("abcdefgh");
    s.erase(2, 2); // "abefgh"
    CHECK(s.length() == 6);
    CHECK(strcmp(s.c_str(), "abefgh") == 0);
    s.erase(0, 2); // "efgh"
    CHECK(s.length() == 4);
    CHECK(strcmp(s.c_str(), "efgh") == 0);
    s.erase(2, 2); // "ef"
    CHECK(s.length() == 2);
    CHECK(strcmp(s.c_str(), "ef") == 0);
  }

  SUBCASE("erase from position to end (one-arg)") {
    ou::string s("hello world");
    s.erase(5);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase from beginning to end (one-arg)") {
    ou::string s("hello");
    s.erase(0);
    CHECK(s.length() == 0);
    CHECK(strcmp(s.c_str(), "") == 0);
  }

  SUBCASE("erase from middle to end (one-arg)") {
    ou::string s("abcdefgh");
    s.erase(3);
    CHECK(s.length() == 3);
    CHECK(strcmp(s.c_str(), "abc") == 0);
  }

  SUBCASE("erase beyond end does nothing (one-arg)") {
    ou::string s("hello");
    s.erase(10);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }

  SUBCASE("erase at end position (one-arg)") {
    ou::string s("hello");
    s.erase(5);
    CHECK(s.length() == 5);
    CHECK(strcmp(s.c_str(), "hello") == 0);
  }
}
