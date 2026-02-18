#pragma once

#include <coco/platform/File_native.hpp>


using namespace coco;

// drivers for FileTest
struct Drivers {
    Loop_native loop;
    File_native file{loop};
    File_native::Buffer buffer1{file, 128, File::HeaderType::NONE};
    File_native::Buffer buffer2{file, 128, File::HeaderType::OFFSET_4};
};

Drivers drivers;
