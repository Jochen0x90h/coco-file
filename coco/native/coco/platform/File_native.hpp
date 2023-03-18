#pragma once

#ifdef _WIN32
#include "File_Win32.hpp"
namespace coco {
using File_native = File_Win32;
}
#endif
