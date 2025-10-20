// address.hpp - a type safe address container and relevant types
#ifndef OT_SHARED_ADDRESS_HPP
#define OT_SHARED_ADDRESS_HPP

#include "ot/common.h"

template <typename Tag> class Address {
private:
  intptr_t addr;

public:
  // Default constructor - null address
  Address() : addr(0) {}

  // Constructor from raw pointer value
  explicit Address(intptr_t raw_addr) : addr(raw_addr) {}

  // Constructor from void pointer
  explicit Address(void *ptr) : addr(reinterpret_cast<intptr_t>(ptr)) {}

  // Constructor from typed pointer
  template <typename T>
  explicit Address(T *ptr) : addr(reinterpret_cast<intptr_t>(ptr)) {}

  // Get raw address value
  intptr_t raw() const { return addr; }

  // Convert to pointer of specified type
  template <typename T> T *as() const { return reinterpret_cast<T *>(addr); }

  // Convert to void pointer
  void *as_ptr() const { return reinterpret_cast<void *>(addr); }

  // Check if address is null
  bool is_null() const { return addr == 0; }

  // Explicit bool conversion for if statements
  explicit operator bool() const { return addr != 0; }

  // Arithmetic operations
  Address operator+(intptr_t offset) const { return Address(addr + offset); }

  Address operator-(intptr_t offset) const { return Address(addr - offset); }

  Address &operator+=(intptr_t offset) {
    addr += offset;
    return *this;
  }

  Address &operator-=(intptr_t offset) {
    addr -= offset;
    return *this;
  }

  // Distance between addresses (same type only)
  intptr_t operator-(const Address &other) const { return addr - other.addr; }

  // Comparison operators
  bool operator==(const Address &other) const { return addr == other.addr; }

  bool operator!=(const Address &other) const { return addr != other.addr; }

  bool operator<(const Address &other) const { return addr < other.addr; }

  bool operator<=(const Address &other) const { return addr <= other.addr; }

  bool operator>(const Address &other) const { return addr > other.addr; }

  bool operator>=(const Address &other) const { return addr >= other.addr; }

  // Alignment check
  bool is_aligned(size_t alignment) const {
    return (addr & (alignment - 1)) == 0;
  }

  // Align address up to boundary
  Address align_up(size_t alignment) const {
    intptr_t mask = alignment - 1;
    return Address((addr + mask) & ~mask);
  }

  // Align address down to boundary
  Address align_down(size_t alignment) const {
    intptr_t mask = alignment - 1;
    return Address(addr & ~mask);
  }

  // Get page offset (useful for paging)
  intptr_t page_offset(size_t page_size = 4096) const {
    return addr & (page_size - 1);
  }

  // Get page base address
  Address page_base(size_t page_size = 4096) const {
    return align_down(page_size);
  }
};

struct PageTag {};

typedef Address<PageTag> PageAddr;

#endif