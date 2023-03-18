#include <FileTest.hpp>
#include <coco/Loop.hpp>
#include <coco/debug.hpp>
#include <coco/BufferWriter.hpp>


using namespace coco;

Coroutine write(Loop &loop, File &file) {
	auto &buffer = file.getBuffer(0);

	// wait until file is ready (open completes successfully)
	co_await buffer.untilNotBusy();
	if (!buffer.ready()) {
		// open failed, device is disabled again
		debug::setRed();

		// exit event loop
		co_await loop.yield();
		loop.exit();

		co_return;
	}


	co_await buffer.writeString("foo");

	// write data with file offset
	buffer.setHeader(uint32_t(3));
	co_await buffer.writeString("bar");

	// read back
	file.seek(0);
	buffer.clearHeader();
	co_await buffer.read(6);
	if (buffer.transferred() == 6) {
		// size is ok
		debug::setGreen();
		if (buffer.transferredString() == "foobar")
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
	if (buffer.transferred() == 0) {
		// size is ok
		debug::setGreen();
	} else {
		debug::setRed();
	}

	// exit event loop
	loop.exit();
}

void synchronousRead(Loop &loop, File &file) {
	auto &buffer = file.getBuffer(0);

	// synchronous read
	file.seek(0);
	buffer.clearHeader();
	buffer.start(6, Buffer::Op::READ);

	// recursively run event loop until state of buffer changes (note that other stuff is still called from the event loop)
	loop.run(buffer.state());

	if (buffer.ready() && buffer.transferred() == 6) {
		// size is ok
		debug::setGreen();
		if (buffer.transferredString() == "foobar")
			// content is ok
			debug::setGreen();
		else
			debug::setRed();
	} else {
		debug::setRed();
	}

}

int main() {
	debug::init();
	Drivers drivers;

	// check if it compiles
	std::filesystem::path n1 = "foo.txt";
	drivers.file.open(n1, File::Mode::READ_WRITE | File::Mode::TRUNCATE);

	char n2[8] = "foo.txt";
	drivers.file.open(n2, File::Mode::READ_WRITE | File::Mode::TRUNCATE);
	drivers.file.close();

	drivers.file.open("foo.txt", File::Mode::READ_WRITE | File::Mode::TRUNCATE);

	write(drivers.loop, drivers.file);

	drivers.loop.run();

	synchronousRead(drivers.loop, drivers.file);

	return 0;
}
