#pragma once

#include <coco/platform/File_native.hpp>


using namespace coco;

// drivers for FileTest
struct Drivers {
    Loop_native loop;
    File_native file{loop};
    File_native::Buffer buffer{file, 128};
};

Drivers drivers;
