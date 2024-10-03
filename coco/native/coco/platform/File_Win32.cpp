#include "File_Win32.hpp"
//#include <iostream>
#include <filesystem>


namespace coco {

File_Win32::~File_Win32() {
	CloseHandle(this->file);
}

//StateTasks<const Device::State, Device::Events> &File_Win32::getStateTasks() {
//	return makeConst(this->st);
//}

// todo: test what happens when buffers are busy when we call close()
void File_Win32::close() {
	// close file
	CloseHandle(this->file);
	this->file = INVALID_HANDLE_VALUE;

	// set state
	this->st.state = State::CLOSING;

	// disable buffers
	for (auto &buffer : this->buffers) {
		buffer.setDisabled();
	}

	// set state and resume all coroutines waiting for state change
	this->st.set(State::DISABLED, Events::ENTER_CLOSING | Events::ENTER_DISABLED);
}

int File_Win32::getBufferCount() {
	return this->buffers.count();
}

File_Win32::Buffer &File_Win32::getBuffer(int index) {
	return this->buffers.get(index);
}

bool File_Win32::open(String name, Mode mode) {
	if (this->st.state != State::DISABLED) {
		assert(false);
		return false;
	}

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
		//int e = WSAGetLastError();
		return false;
	}

	// add file to completion port of event loop
	Loop_Win32::CompletionHandler *handler = this;
	if (CreateIoCompletionPort(
		file,
		loop.port,
		ULONG_PTR(handler),
		0) == nullptr)
	{
		//int e = WSAGetLastError();
		CloseHandle(file);
		return false;
	}
	this->file = file;

	// set state
	this->st.state = State::OPENING;

	// enable buffers
	for (auto &buffer : this->buffers) {
		buffer.setReady(0);
	}

	// set state and resume all coroutines waiting for state change
	this->st.set(State::READY, Events::ENTER_OPENING | Events::ENTER_READY);

	return true;
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
	this->offset = offset;
}

void File_Win32::handle(OVERLAPPED *overlapped) {
	for (auto &buffer : this->transfers) {
		if (overlapped == &buffer.overlapped) {
			buffer.handle(overlapped);
			break;
		}
	}
}


// Buffer

File_Win32::Buffer::Buffer(File_Win32 &device, int size)
	: coco::Buffer(new uint8_t[size], size, device.st.state)
	, device(device)
{
	device.buffers.add(*this);
}

File_Win32::Buffer::~Buffer() {
	delete [] this->p.data;
}

bool File_Win32::Buffer::start(Op op) {
	if (this->st.state != State::READY) {
		assert(this->st.state != State::BUSY);
		return false;
	}

	// check if READ or WRITE flag is set
	assert((op & Op::READ_WRITE) != 0);
	this->op = op;

	// check if header size is ok
	int headerSize = this->p.headerSize;
	if ((headerSize & 3) != 0 || headerSize > 8) {
		assert(false);
		return false;
	}

	// add to list of pending transfers
	this->device.transfers.add(*this);

	// start if device is ready
	if (this->device.st.state == Device::State::READY)
		start();

	// set state
	setBusy();

	return true;
}

bool File_Win32::Buffer::cancel() {
	if (this->st.state != State::BUSY)
		return false;

	auto result = CancelIoEx(this->device.file, &this->overlapped);
	if (!result) {
		auto e = GetLastError();
		//std::cerr << "cancel error " << e << std::endl;
	}
	return true;
}

void File_Win32::Buffer::start() {
	// initialize overlapped
	memset(&this->overlapped, 0, sizeof(OVERLAPPED));
	auto header = this->p.data;
	int headerSize = this->p.headerSize;
	if (headerSize == 0) {
		// use file offset
		*reinterpret_cast<uint64_t *>(&this->overlapped.Offset) = this->device.offset;
	} else {
		// use offset in buffer header
		memcpy(&this->overlapped.Offset, header, headerSize);
	}

	// get data and size to read/write
	auto data = this->p.data + headerSize;
	int size = this->p.size - headerSize;
	int result;
	if ((op & Op::WRITE) == 0) {
		// read
		result = ReadFile(this->device.file, data, size, nullptr, &this->overlapped);
	} else {
		// write
		result = WriteFile(this->device.file, data, size, nullptr, &this->overlapped);
	}

	if (result == 0) {
		int error = GetLastError();
		if (error != ERROR_IO_PENDING) {
			// "real" error
			setReady(0);
			//return false;
		}
	}
	this->device.offset = *reinterpret_cast<uint64_t *>(&this->overlapped.Offset) + size;
}

void File_Win32::Buffer::handle(OVERLAPPED *overlapped) {
	DWORD transferred;
	auto result = GetOverlappedResult(this->device.file, overlapped, &transferred, false);
	if (!result) {
		// "real" error or cancelled (ERROR_OPERATION_ABORTED): return zero size
		auto error = GetLastError();
		transferred = 0;
	}

	// remove from list of active transfers
	remove2();

	// transfer finished
	setReady(transferred);
}

} // namespace coco
