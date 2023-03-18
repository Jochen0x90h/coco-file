#include "File_Win32.hpp"
#include <iostream>
#include <filesystem>


namespace coco {

File_Win32::~File_Win32() {
	CloseHandle(this->file);
}

File::State File_Win32::state() {
	return this->p.state;
}

Awaitable<File::State> File_Win32::untilState(State state) {
	if (this->p.state == state)
		return {};
	return {this->stateTasks, state};
}

int File_Win32::getBufferCount() {
	return this->buffers.count();
}

File_Win32::Buffer &File_Win32::getBuffer(int index) {
	return this->buffers.get(index);
}

bool File_Win32::open(String name, Mode mode) {
	// open file using Win32
	int access = ((mode & Mode::READ) != 0 ? GENERIC_READ : 0) | ((mode & Mode::WRITE) != 0 ? GENERIC_WRITE : 0);
	int disposition = (mode & Mode::TRUNCATE) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
	std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t *>(name.data()), name.size()));
	HANDLE file = CreateFileW(path.c_str(),
		access,
		FILE_SHARE_READ,
		nullptr, // security
		disposition,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		// error: return to disabled state
		this->p.state = State::DISABLED;

		// resume all coroutines waiting for idle state
		this->stateTasks.resumeAll([](State state) {
			return state == State::DISABLED;
		});
		return false;
	}
	this->file = file;

	// add file to completion port of event loop
	Loop_Win32::CompletionHandler *handler = this;
	CreateIoCompletionPort(
		file,
		loop.port,
		ULONG_PTR(handler),
		0);

	// set state
	this->p.state = State::READY;

	// set state of buffers to ready
	for (auto &buffer : this->buffers) {
		buffer.setReady(0);
	}

	// resume all coroutines waiting for ready state
	this->stateTasks.resumeAll([](State state) {
		return state == State::READY;
	});

	return true;
}

// todo: test what happens when buffers are busy when we call close()
void File_Win32::close() {
	// close file
	CloseHandle(this->file);
	this->file = INVALID_HANDLE_VALUE;

	// set state
	this->p.state = State::DISABLED;

	// set state of buffers to disabled
	for (auto &buffer : this->buffers) {
		buffer.setDisabled();
	}

	// resume all coroutines waiting for disabled state
	this->stateTasks.resumeAll([](State state) {
		return state == State::DISABLED;
	});
}

uint64_t File_Win32::size() {
	DWORD high;
	auto low = GetFileSize(this->file, &high);
	return (uint64_t(high) << 32) | low;
}

void File_Win32::resize(uint64_t size) {
	auto high = LONG(size >> 32);
	SetFilePointer(this->file, LONG(size), &high, FILE_BEGIN);
	SetEndOfFile(this->file);
}

void File_Win32::seek(uint64_t offset) {
	this->p.offset = offset;
}

void File_Win32::handle(OVERLAPPED *overlapped) {
	for (auto &buffer : this->buffers) {
		buffer.handle(overlapped);
	}
}


// Buffer

File_Win32::Buffer::Buffer(File_Win32 &file, int capacity)
	: BufferImpl(new uint8_t[capacity], capacity, file.p.state)
	, file(file)
{
	file.buffers.add(*this);
	this->overlapped.OffsetHigh = 0xffffffff;
}

File_Win32::Buffer::~Buffer() {
	delete [] this->buffer.data;
}

bool File_Win32::Buffer::setHeader(const uint8_t *data, int size) {
	assert((size & 3) == 0 && size <= 8);

	if (size == 0) {
		// use file offset
		this->overlapped.OffsetHigh = 0xffffffff;
		return true;
	} else if (size == 4) {
		this->overlapped.Offset = *reinterpret_cast<const uint32_t *>(data);
		this->overlapped.OffsetHigh = 0;
		return true;
	} else {
		*reinterpret_cast<uint64_t *>(&this->overlapped.Offset) = *reinterpret_cast<const uint64_t *>(data);
		return true;
	}
	return false;
}

bool File_Win32::Buffer::startInternal(int size, Op op) {
	if (this->p.state != State::READY) {
		assert(false);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);

	// initialize overlapped
	this->overlapped.Internal = 0;
	this->overlapped.InternalHigh = 0;
	if (this->overlapped.OffsetHigh == 0xffffffff) {
		// header was not set: use file offset
		*reinterpret_cast<uint64_t *>(&this->overlapped.Offset) = this->file.p.offset;
	}
	this->overlapped.hEvent = nullptr;

	// get data and size to read/write
	auto data = this->buffer.data;
	//int size = this->p.size;
	int result;
	if ((op & Op::READ) != 0) {
		// read
		result = ReadFile(this->file.file, data, size, nullptr, &this->overlapped);
	} else {
		// write
		result = WriteFile(this->file.file, data, size, nullptr, &this->overlapped);
	}

	if (result == 0) {
		int error = GetLastError();
		if (error != ERROR_IO_PENDING) {
			// "real" error
			setReady(0);
			return false;
		}
	}
	this->file.p.offset = *reinterpret_cast<uint64_t *>(&this->overlapped.Offset) + size;

	// set state
	setBusy();

	return true;
}

void File_Win32::Buffer::cancel() {
	if (this->p.state != State::BUSY)
		return;

	auto result = CancelIoEx(this->file.file, &this->overlapped);
	if (!result) {
		auto e = GetLastError();
		std::cerr << "cancel error " << e << std::endl;
	}

	// set state and resume all coroutines waiting for cancelled state
	//setCancelled();
}

void File_Win32::Buffer::handle(OVERLAPPED *overlapped) {
	if (overlapped == &this->overlapped) {
		DWORD transferred;
		auto result = GetOverlappedResult(this->file.file, overlapped, &transferred, false);
		if (!result) {
			// "real" error or cancelled (ERROR_OPERATION_ABORTED): return zero size
			auto error = GetLastError();
			transferred = 0;
		}

		// transfer finished
		this->overlapped.OffsetHigh = 0xffffffff;
		setReady(transferred);
	}
}

} // namespace coco
