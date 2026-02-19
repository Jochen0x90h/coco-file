#pragma once

#if defined(_WIN32)
#include "File_Win32.hpp"
namespace coco {
using File_native = File_Win32;
}
#elif defined(__linux__)
#include "File_io_uring.hpp"
namespace coco {
using File_native = File_io_uring;
}
#endif
