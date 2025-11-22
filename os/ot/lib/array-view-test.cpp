// array-view-test.cpp - Unit tests for ArrayView template

#include "ot/lib/array-view.hpp"
#include "ot/lib/buffer-view.hpp"
#include "vendor/doctest.h"

TEST_CASE("ArrayView construction") {
  SUBCASE("default constructor") {
    ArrayView<int> view;
    CHECK(view.empty());
    CHECK(view.size() == 0);
    CHECK(view.ptr == nullptr);
    CHECK(view.len == 0);
  }

  SUBCASE("from pointer and length") {
    int arr[] = {1, 2, 3, 4, 5};
    ArrayView<int> view(arr, 5);
    CHECK(!view.empty());
    CHECK(view.size() == 5);
    CHECK(view.ptr == arr);
    CHECK(view.len == 5);
  }

  SUBCASE("zero-length array") {
    int arr[] = {1, 2, 3};
    ArrayView<int> view(arr, 0);
    CHECK(view.empty());
    CHECK(view.size() == 0);
  }
}

TEST_CASE("ArrayView element access") {
  int arr[] = {10, 20, 30, 40, 50};
  ArrayView<int> view(arr, 5);

  SUBCASE("operator[]") {
    CHECK(view[0] == 10);
    CHECK(view[2] == 30);
    CHECK(view[4] == 50);
  }

  SUBCASE("bounds-checked get") {
    int val;
    CHECK(view.get(0, val));
    CHECK(val == 10);

    CHECK(view.get(4, val));
    CHECK(val == 50);

    CHECK(!view.get(5, val)); // out of bounds
    CHECK(!view.get(100, val)); // out of bounds
  }

  SUBCASE("data() pointer") {
    CHECK(view.data() == arr);
  }
}

TEST_CASE("ArrayView iteration") {
  int arr[] = {1, 2, 3, 4, 5};
  ArrayView<int> view(arr, 5);

  SUBCASE("iterators") {
    CHECK(view.begin() == arr);
    CHECK(view.end() == arr + 5);
  }

  SUBCASE("range-based for loop") {
    int sum = 0;
    for (int val : view) {
      sum += val;
    }
    CHECK(sum == 15); // 1+2+3+4+5
  }
}

TEST_CASE("ArrayView copy_to") {
  int arr[] = {10, 20, 30};
  ArrayView<int> view(arr, 3);

  SUBCASE("successful copy") {
    int buffer[5];
    CHECK(view.copy_to(buffer, 5));
    CHECK(buffer[0] == 10);
    CHECK(buffer[1] == 20);
    CHECK(buffer[2] == 30);
  }

  SUBCASE("exact size buffer") {
    int buffer[3];
    CHECK(view.copy_to(buffer, 3));
    CHECK(buffer[0] == 10);
    CHECK(buffer[1] == 20);
    CHECK(buffer[2] == 30);
  }

  SUBCASE("buffer too small") {
    int buffer[2];
    CHECK(!view.copy_to(buffer, 2));
  }
}

TEST_CASE("ArrayView equality") {
  int arr1[] = {1, 2, 3};
  int arr2[] = {1, 2, 3};
  int arr3[] = {1, 2, 4};
  int arr4[] = {1, 2};

  SUBCASE("equal arrays") {
    ArrayView<int> view1(arr1, 3);
    ArrayView<int> view2(arr2, 3);
    CHECK(view1.equals(view2));
  }

  SUBCASE("different values") {
    ArrayView<int> view1(arr1, 3);
    ArrayView<int> view3(arr3, 3);
    CHECK(!view1.equals(view3));
  }

  SUBCASE("different lengths") {
    ArrayView<int> view1(arr1, 3);
    ArrayView<int> view4(arr4, 2);
    CHECK(!view1.equals(view4));
  }

  SUBCASE("empty arrays") {
    ArrayView<int> view1;
    ArrayView<int> view2;
    CHECK(view1.equals(view2));
  }
}

TEST_CASE("ArrayView with different types") {
  SUBCASE("uint8_t (bytes)") {
    uint8_t bytes[] = {0x12, 0x34, 0x56};
    ArrayView<uint8_t> view(bytes, 3);
    CHECK(view.size() == 3);
    CHECK(view[0] == 0x12);
    CHECK(view[1] == 0x34);
    CHECK(view[2] == 0x56);
  }

  SUBCASE("size_t") {
    size_t nums[] = {100, 200, 300};
    ArrayView<size_t> view(nums, 3);
    CHECK(view.size() == 3);
    CHECK(view[0] == 100);
    CHECK(view[1] == 200);
    CHECK(view[2] == 300);
  }

  SUBCASE("uint32_t") {
    uint32_t pixels[] = {0xFF0000, 0x00FF00, 0x0000FF};
    ArrayView<uint32_t> view(pixels, 3);
    CHECK(view.size() == 3);
    CHECK(view[0] == 0xFF0000);
    CHECK(view[1] == 0x00FF00);
    CHECK(view[2] == 0x0000FF);
  }
}

TEST_CASE("BufferView typedef") {
  SUBCASE("BufferView is ArrayView<uint8_t>") {
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    BufferView view(data, 4);
    CHECK(view.size() == 4);
    CHECK(view[0] == 0xDE);
    CHECK(view[1] == 0xAD);
    CHECK(view[2] == 0xBE);
    CHECK(view[3] == 0xEF);
  }

  SUBCASE("empty buffer") {
    BufferView view;
    CHECK(view.empty());
    CHECK(view.size() == 0);
  }
}

TEST_CASE("ArrayView const correctness") {
  int arr[] = {1, 2, 3};
  const ArrayView<int> view(arr, 3);

  SUBCASE("const methods work") {
    CHECK(view.size() == 3);
    CHECK(view[0] == 1);
    CHECK(!view.empty());
  }

  SUBCASE("const iteration") {
    int sum = 0;
    for (const auto &val : view) {
      sum += val;
    }
    CHECK(sum == 6);
  }
}
