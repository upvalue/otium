#ifndef OT_LIB_BUFFER_VIEW_HPP
#define OT_LIB_BUFFER_VIEW_HPP

#include "ot/lib/array-view.hpp"
#include <stdint.h>

// BufferView is a specialized ArrayView for binary data (uint8_t)
using BufferView = ArrayView<uint8_t>;

#endif // OT_LIB_BUFFER_VIEW_HPP
