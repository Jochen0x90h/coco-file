#include <FileTest.hpp>
#include <coco/convert.hpp>
#include <coco/debug.hpp>
#include <coco/BufferWriter.hpp>
#include <coco/Loop.hpp>


using namespace coco;

Coroutine write(Loop &loop, File &file) {
    // buffer 0 uses global file offset, buffer 1 has file offset in header
    auto &buffer0 = file.getBuffer(0);
    auto &buffer1 = file.getBuffer(1);

    // wait until file is ready (open completes successfully)
    co_await file.untilReadyOrDisabled();
    if (!file.ready()) {
        // open failed, device is disabled again
        debug::setRed();
        debug::out << "Error: Open failed\n";

        // exit event loop
        loop.exit();

        co_return;
    }

    co_await buffer0.write("foo");

    // write data with file offset
    buffer1.header<uint32_t>() = 3;
    co_await buffer1.write("bar");

    // check file size
    auto size = file.size();
    if (size == 6) {
        debug::setGreen();
        debug::out << "File size OK\n";
    } else {
        debug::setRed();
        debug::out << "Error: File size is " << dec(size) << ", expected 6\n";
    }


    // read back
    file.seek(0);
    co_await buffer0.read(6);
    if (buffer0.size() == 6) {
        // size is ok
        debug::setGreen();
        debug::out << "Buffer size OK\n";
        if (buffer0.string() == "foobar") {
            // content is ok
            debug::setGreen();
            debug::out << "File content OK\n";
        } else {
            debug::setRed();
            debug::out << "Error: File content is " << buffer0.string() << ", expected foobar\n";
        }
    } else {
        debug::setRed();
        debug::out << "Error: Buffer size is " << dec(buffer0.size()) << ", expected 6\n";
    }

    co_await loop.sleep(1s);

    // read past the end
    co_await buffer0.read(6);
    if (buffer0.size() == 0) {
        // size is ok
        debug::setGreen();
        debug::out << "Buffer size OK\n";
    } else {
        debug::setRed();
        debug::out << "Error: Buffer size is " << dec(buffer0.size()) << ", expected 0\n";
    }

    // close file
    file.close();
    co_await file.untilDisabled();

    // exit event loop
    loop.exit();
}


Coroutine synchronousRead(Loop &loop, Buffer &buffer) {
    co_await buffer.read(6);
    loop.exit();
}

void synchronousRead(Loop &loop, File &file) {
    // buffer uses global file offset
    auto &buffer = file.getBuffer(0);

    file.seek(0);

    // start read
    synchronousRead(loop, buffer);

    // run event loop until exit() gets called
    loop.run();

    if (buffer.ready()) {
        if (buffer.size() == 6) {
            // size is ok
            debug::setGreen();
            debug::out << "Buffer size OK\n";
            if (buffer.string() == "foobar") {
                // content is ok
                debug::setGreen();
                debug::out << "File content OK\n";
            } else {
                debug::setRed();
                debug::out << "Error: File content is " << buffer.string() << ", expected foobar\n";
            }
        } else {
            debug::setRed();
            debug::out << "Error: Buffer size is " << dec(buffer.size()) << ", expected 6\n";
        }
    } else {
        debug::out << "Error: Buffer not ready\n";
    }
}

// check if open() works with different file name representations
void checkCompile(File &file) {
    std::filesystem::path n1 = "foo.txt";
    file.open(n1, File::Mode::NEW);

    char n2[8] = "foo.txt";
    file.open(n2, File::Mode::NEW);
}

int main() {
    debug::out << "FileTest\n";

    // asynchronous
    if (!drivers.file.open("foo.txt", File::Mode::NEW)) {
        debug::out << "Error: " << drivers.file.error().message() << '\n';
        return 1;
    }
    write(drivers.loop, drivers.file);
    drivers.loop.run();

    // synchronous
    drivers.file.open("foo.txt", File::Mode::READ);
    synchronousRead(drivers.loop, drivers.file);

    return 0;
}
