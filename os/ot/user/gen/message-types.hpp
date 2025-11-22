#pragma once
#include "ot/common.h"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/lib/array-view.hpp"
#include "ot/lib/buffer-view.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"

// Auto-generated message types

// Array type: FibArray
// Element type: uint
using FibArray = ou::vector<uintptr_t>;
using FibArrayView = ArrayView<uintptr_t>;

// Helper functions for FibArray
namespace FibArrayHelper {

  inline void pack(const FibArray& arr, MPackWriter& writer) {
    writer.array(arr.size());
    for (const auto& elem : arr) {
      writer.u64(elem);
    }
  }

  inline Result<FibArray, ErrorCode> unpack(MPackReader& reader) {
    if (!reader.is_array()) {
      return Result<FibArray, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
    }
    uint32_t count = reader.array_size();
    FibArray result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
      uint64_t val;
      if (!reader.read_u64(val)) {
        return Result<FibArray, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
      }
      result.push_back(val);
    }
    return Result<FibArray, ErrorCode>::ok(static_cast<FibArray&&>(result));
  }
}

// Struct type: FibResult (owning version)
struct FibResult {
  uintptr_t value;
  bool is_cached;

  // Serialize to messagepack
  void pack(MPackWriter& writer) const {
    writer.map(2);
    writer.str("value");
    writer.u64(value);
    writer.str("is_cached");
    writer.boolean(is_cached);
  }

  // Deserialize from messagepack
  static Result<FibResult, ErrorCode> unpack(MPackReader& reader) {
    FibResult result;

    if (!reader.is_map()) {
      return Result<FibResult, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
    }

    size_t map_size = reader.map_size();
    for (size_t i = 0; i < map_size; i++) {
      StringView key;
      if (!reader.read_string(key)) {
        return Result<FibResult, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
      }

      if (key.equals("value")) {
        uint64_t val;
        if (!reader.read_u64(val)) {
          return Result<FibResult, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
        }
        result.value = val;
      }
       else if (key.equals("is_cached")) {
        bool val;
        if (!reader.read_bool(val)) {
          return Result<FibResult, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
        }
        result.is_cached = val;
      }
    }

    return Result<FibResult, ErrorCode>::ok(static_cast<FibResult&&>(result));
  }
};

// Struct type: FibResultView (zero-copy view version)
struct FibResultView {
  uintptr_t value;
  bool is_cached;

  // Deserialize from messagepack (zero-copy where possible)
  static Result<FibResultView, ErrorCode> unpack_view(MPackReader& reader) {
    FibResultView result;

    if (!reader.is_map()) {
      return Result<FibResultView, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
    }

    size_t map_size = reader.map_size();
    for (size_t i = 0; i < map_size; i++) {
      StringView key;
      if (!reader.read_string(key)) {
        return Result<FibResultView, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
      }

      if (key.equals("value")) {
        uint64_t val;
        if (!reader.read_u64(val)) {
          return Result<FibResultView, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
        }
        result.value = val;
      }
       else if (key.equals("is_cached")) {
        bool val;
        if (!reader.read_bool(val)) {
          return Result<FibResultView, ErrorCode>::err(ErrorCode::IPC__INVALID_MSG);
        }
        result.is_cached = val;
      }
    }

    return Result<FibResultView, ErrorCode>::ok(result);
  }
};

