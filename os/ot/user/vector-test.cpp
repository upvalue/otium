// vector-test.cpp - Unit tests for ou::vector class

#include "ot/user/vector.hpp"
#include "vendor/doctest.h"

TEST_CASE("vector construction") {
  SUBCASE("default constructor") {
    ou::vector<int> v;
    CHECK(v.size() == 0);
    CHECK(v.empty());
  }
}

TEST_CASE("vector push_back operations") {
  SUBCASE("push_back integers") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    CHECK(v.size() == 3);
    CHECK(!v.empty());
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("push_back moves") {
    ou::vector<ou::string> v;
    ou::string s1("hello");
    v.push_back(static_cast<ou::string &&>(s1));
    CHECK(v.size() == 1);
    CHECK(strcmp(v[0].c_str(), "hello") == 0);
  }

  SUBCASE("push_back many elements triggers reallocation") {
    ou::vector<int> v;
    for (int i = 0; i < 100; i++) {
      v.push_back(i);
    }
    CHECK(v.size() == 100);
    for (int i = 0; i < 100; i++) {
      CHECK(v[i] == i);
    }
  }
}

TEST_CASE("vector access operations") {
  SUBCASE("operator[]") {
    ou::vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    CHECK(v[0] == 10);
    CHECK(v[1] == 20);
    CHECK(v[2] == 30);
  }

  SUBCASE("operator[] modification") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v[0] = 100;
    v[1] = 200;
    CHECK(v[0] == 100);
    CHECK(v[1] == 200);
  }

  SUBCASE("back()") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    CHECK(v.back() == 3);
    v.back() = 99;
    CHECK(v.back() == 99);
    CHECK(v[2] == 99);
  }

  SUBCASE("const back()") {
    ou::vector<int> v;
    v.push_back(42);
    const ou::vector<int> &cv = v;
    CHECK(cv.back() == 42);
  }
}

TEST_CASE("vector pop_back operations") {
  SUBCASE("pop_back reduces size") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.pop_back();
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
  }

  SUBCASE("pop_back until empty") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.pop_back();
    v.pop_back();
    CHECK(v.size() == 0);
    CHECK(v.empty());
  }

  SUBCASE("pop_back on empty vector") {
    ou::vector<int> v;
    v.pop_back(); // Should not crash
    CHECK(v.empty());
  }
}

TEST_CASE("vector insert operations") {
  SUBCASE("insert at beginning") {
    ou::vector<int> v;
    v.push_back(2);
    v.push_back(3);
    v.insert(0, 1);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("insert in middle") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(3);
    v.insert(1, 2);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("insert at end") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.insert(2, 3);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("insert beyond end (clamps to end)") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.insert(100, 3);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("insert into empty vector") {
    ou::vector<int> v;
    v.insert(0, 42);
    CHECK(v.size() == 1);
    CHECK(v[0] == 42);
  }

  SUBCASE("insert multiple elements at beginning") {
    ou::vector<int> v;
    v.push_back(4);
    v.push_back(5);
    v.insert(0, 3, 1);
    CHECK(v.size() == 5);
    CHECK(v[0] == 1);
    CHECK(v[1] == 1);
    CHECK(v[2] == 1);
    CHECK(v[3] == 4);
    CHECK(v[4] == 5);
  }

  SUBCASE("insert multiple elements in middle") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(5);
    v.insert(2, 2, 3);
    CHECK(v.size() == 5);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
    CHECK(v[3] == 3);
    CHECK(v[4] == 5);
  }

  SUBCASE("insert multiple elements at end") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.insert(2, 3, 9);
    CHECK(v.size() == 5);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 9);
    CHECK(v[3] == 9);
    CHECK(v[4] == 9);
  }

  SUBCASE("insert zero elements") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.insert(1, 0, 99);
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
  }

  SUBCASE("insert multiple beyond end (clamps to end)") {
    ou::vector<int> v;
    v.push_back(1);
    v.insert(100, 2, 5);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 5);
    CHECK(v[2] == 5);
  }

  SUBCASE("insert strings") {
    ou::vector<ou::string> v;
    v.push_back(ou::string("a"));
    v.push_back(ou::string("c"));
    v.insert(1, ou::string("b"));
    CHECK(v.size() == 3);
    CHECK(strcmp(v[0].c_str(), "a") == 0);
    CHECK(strcmp(v[1].c_str(), "b") == 0);
    CHECK(strcmp(v[2].c_str(), "c") == 0);
  }
}

TEST_CASE("vector erase operations") {
  SUBCASE("erase from beginning") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.erase(0);
    CHECK(v.size() == 2);
    CHECK(v[0] == 2);
    CHECK(v[1] == 3);
  }

  SUBCASE("erase from middle") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.erase(1);
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 3);
  }

  SUBCASE("erase from end") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.erase(2);
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
  }

  SUBCASE("erase beyond end does nothing") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.erase(5);
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
  }

  SUBCASE("erase from single element vector") {
    ou::vector<int> v;
    v.push_back(42);
    v.erase(0);
    CHECK(v.size() == 0);
    CHECK(v.empty());
  }

  SUBCASE("erase multiple elements from beginning") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(5);
    v.erase(0, 2);
    CHECK(v.size() == 3);
    CHECK(v[0] == 3);
    CHECK(v[1] == 4);
    CHECK(v[2] == 5);
  }

  SUBCASE("erase multiple elements from middle") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(5);
    v.erase(1, 2);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 4);
    CHECK(v[2] == 5);
  }

  SUBCASE("erase multiple elements to end") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(5);
    v.erase(3, 10);
    CHECK(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
  }

  SUBCASE("erase zero elements does nothing") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.erase(1, 0);
    CHECK(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
  }

  SUBCASE("erase entire vector") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.erase(0, 3);
    CHECK(v.size() == 0);
    CHECK(v.empty());
  }

  SUBCASE("erase strings") {
    ou::vector<ou::string> v;
    v.push_back(ou::string("a"));
    v.push_back(ou::string("b"));
    v.push_back(ou::string("c"));
    v.erase(1);
    CHECK(v.size() == 2);
    CHECK(strcmp(v[0].c_str(), "a") == 0);
    CHECK(strcmp(v[1].c_str(), "c") == 0);
  }

  SUBCASE("multiple consecutive erases") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.push_back(5);
    v.erase(1); // [1, 3, 4, 5]
    CHECK(v.size() == 4);
    CHECK(v[1] == 3);
    v.erase(2); // [1, 3, 5]
    CHECK(v.size() == 3);
    CHECK(v[2] == 5);
    v.erase(0); // [3, 5]
    CHECK(v.size() == 2);
    CHECK(v[0] == 3);
    CHECK(v[1] == 5);
  }
}

TEST_CASE("vector clear operations") {
  SUBCASE("clear empties vector") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.clear();
    CHECK(v.size() == 0);
    CHECK(v.empty());
  }

  SUBCASE("clear on empty vector") {
    ou::vector<int> v;
    v.clear();
    CHECK(v.empty());
  }

  SUBCASE("push after clear") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.clear();
    v.push_back(3);
    CHECK(v.size() == 1);
    CHECK(v[0] == 3);
  }
}

TEST_CASE("vector iterators") {
  SUBCASE("begin and end") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    int sum = 0;
    for (int *it = v.begin(); it != v.end(); ++it) {
      sum += *it;
    }
    CHECK(sum == 6);
  }

  SUBCASE("range-based for loop") {
    ou::vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    int sum = 0;
    for (int val : v) {
      sum += val;
    }
    CHECK(sum == 60);
  }

  SUBCASE("modify via iterators") {
    ou::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    for (int &val : v) {
      val *= 2;
    }
    CHECK(v[0] == 2);
    CHECK(v[1] == 4);
    CHECK(v[2] == 6);
  }
}
