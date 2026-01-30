#include "File_Win32.hpp"
//#include <iostream>
#include <filesystem>


namespace coco {

File_Win32::~File_Win32() {
    CloseHandle(file_);
}

bool File_Win32::open(String name, Mode mode) {
    if (file_ != INVALID_HANDLE_VALUE)
        return false;

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
        loop_.port,
        ULONG_PTR(handler),
        0) == nullptr)
    {
        //int e = WSAGetLastError();
        CloseHandle(file);
        return false;
    }
    file_ = file;

    // set state
    st.set(State::READY);

    // enable buffers
    for (auto &buffer : buffers_) {
        buffer.setReady(0);
    }

    // resume all coroutines waiting for state change
    st.notify(Events::ENTER_OPENING | Events::ENTER_READY);

    return true;
}

uint64_t File_Win32::size() {
    DWORD high;
    auto low = GetFileSize(file_, &high);
    return (uint64_t(high) << 32) | low;
}

void File_Win32::resize(uint64_t size) {
    auto high = LONG(size >> 32);
    SetFilePointer(file_, LONG(size), &high, FILE_BEGIN);
    SetEndOfFile(file_);
}

void File_Win32::seek(uint64_t offset) {
    offset_ = offset;
}

int File_Win32::getBufferCount() {
    return buffers_.count();
}

File_Win32::Buffer &File_Win32::getBuffer(int index) {
    return buffers_.get(index);
}

// todo: test what happens when buffers are busy when we call close()
void File_Win32::close() {
    if (file_ == INVALID_HANDLE_VALUE)
        return;

    // close file
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;

    // set state
    st.set(State::DISABLED);

    // disable buffers
    for (auto &buffer : buffers_) {
        buffer.setDisabled();
    }

    // resume all coroutines waiting for state change
    st.notify(Events::ENTER_CLOSING | Events::ENTER_DISABLED);
}

void File_Win32::handle(OVERLAPPED *overlapped) {
    for (auto &buffer : transfers_) {
        if (overlapped == &buffer.overlapped_) {
            buffer.handle(overlapped);
            break;
        }
    }
}


// File_Win32::Buffer

File_Win32::Buffer::Buffer(File_Win32 &device, int capacity, HeaderType headerType)
    : coco::Buffer(&overlapped_.Offset, 8, int(headerType), new uint8_t[capacity], capacity, device.st.state)
    , device_(device)
{
    device.buffers_.add(*this);

    // we don't use the event object
    overlapped_.hEvent = nullptr;
}

File_Win32::Buffer::~Buffer() {
    delete [] data_;
}

bool File_Win32::Buffer::start(Op op) {
    if (st.state != State::READY) {
        assert(st.state != State::BUSY);
        return false;
    }

    // check if READ or WRITE flag is set
    assert((op & Op::READ_WRITE) != 0);
    op_ = op;

    // add to list of pending transfers
    device_.transfers_.add(*this);

    // start if device is ready
    if (device_.st.state == Device::State::READY)
        start();

    // set state
    setBusy();

    return true;
}

bool File_Win32::Buffer::cancel() {
    if (st.state != State::BUSY)
        return false;

    auto result = CancelIoEx(device_.file_, &overlapped_);
    if (!result) {
        auto e = GetLastError();
        //std::cerr << "cancel error " << e << std::endl;
    }
    return true;
}

void File_Win32::Buffer::start() {
    auto &device = device_;

    // initialize overlapped
    overlapped_.Internal = 0;
    overlapped_.InternalHigh = 0;

    switch (HeaderType(headerType_)) {
    case HeaderType::OFFSET_4:
        // use 4 byte offset, clear high word
        overlapped_.OffsetHigh = 0;
        break;
    case HeaderType::OFFSET_8:
        // use 8 byte offset
        break;
    default:
        // use current file offset
        *reinterpret_cast<uint64_t *>(&overlapped_.Offset) = device.offset_;
    }

    // get data and size to read/write
    auto data = data_;
    int size = size_;
    int result;
    if ((op_ & Op::WRITE) == 0) {
        // read
        result = ReadFile(device.file_, data, size, nullptr, &overlapped_);
    } else {
        // write
        result = WriteFile(device.file_, data, size, nullptr, &overlapped_);
    }

    if (result == 0) {
        int error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            // "real" error
            setReady(0);
            //return false;
        }
    }

    // increment file offset
    device.offset_ = *reinterpret_cast<uint64_t *>(&overlapped_.Offset) + size;
}

void File_Win32::Buffer::handle(OVERLAPPED *overlapped) {
    DWORD transferred;
    auto result = GetOverlappedResult(device_.file_, overlapped, &transferred, false);
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
