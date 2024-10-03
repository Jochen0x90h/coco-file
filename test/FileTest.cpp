#include <FileTest.hpp>
#include <coco/Loop.hpp>
#include <coco/debug.hpp>
#include <coco/BufferWriter.hpp>


using namespace coco;

Coroutine write(Loop &loop, File &file) {
	auto &buffer = file.getBuffer(0);

	// wait until file is ready (open completes successfully)
	co_await file.untilReadyOrDisabled();
	if (!file.ready()) {
		// open failed, device is disabled again
		debug::setRed();

		// exit event loop
		loop.exit();

		co_return;
	}

	co_await buffer.write("foo");

	// write data with file offset
	buffer.setHeader(uint32_t(3));
	co_await buffer.write("bar");

	// read back
	file.seek(0);
	buffer.clearHeader();
	co_await buffer.read(6);
	if (buffer.size() == 6) {
		// size is ok
		debug::setGreen();
		if (buffer.string() == "foobar")
			// content is ok
			debug::setGreen();
		else
			debug::setRed();
	} else {
		debug::setRed();
	}

	co_await loop.sleep(1s);

	// read past the end
	co_await buffer.read(6);
	if (buffer.size() == 0) {
		// size is ok
		debug::setGreen();
	} else {
		debug::setRed();
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
	auto &buffer = file.getBuffer(0);

	file.seek(0);
	buffer.clearHeader();

	// start read
	synchronousRead(loop, buffer);

	// run event loop until exit() gets called
	loop.run();

	if (buffer.ready() && buffer.size() == 6) {
		// size is ok
		debug::setGreen();
		if (buffer.string() == "foobar")
			// content is ok
			debug::setGreen();
		else
			debug::setRed();
	} else {
		debug::setRed();
	}
}

// check if open() works with different file name representations
void checkCompile(File &file) {
	std::filesystem::path n1 = "foo.txt";
	file.open(n1, File::Mode::READ_WRITE | File::Mode::TRUNCATE);

	char n2[8] = "foo.txt";
	file.open(n2, File::Mode::READ_WRITE | File::Mode::TRUNCATE);
}

int main() {
	// asynchronous
	drivers.file.open("foo.txt", File::Mode::READ_WRITE | File::Mode::TRUNCATE);
	write(drivers.loop, drivers.file);
	drivers.loop.run();

	// synchronous
	drivers.file.open("foo.txt", File::Mode::READ);
	synchronousRead(drivers.loop, drivers.file);

	return 0;
}
