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
        int error = GetLastError();
        setSystemError(error);
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
        int error = GetLastError();
        setSystemError(error);
        CloseHandle(file);
        return false;
    }
    file_ = file;
    setSuccess();

    // set state
    state_ = State::READY;

    // enable buffers
    for (auto &buffer : buffers_) {
        buffer.setSuccess(0);
        buffer.setReady();
    }

    // resume all coroutines waiting for state change
    notify(Events::ENTER_OPENING | Events::ENTER_READY);

    return true;
}

uint64_t File_Win32::size() {
    // https://learn.microsoft.com/de-de/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex
    FILE_STANDARD_INFO fileInfo;
    if (!GetFileInformationByHandleEx(
            file_,
            FileStandardInfo,
            &fileInfo,
            sizeof(fileInfo)))
    {
        int error = GetLastError();
        setSystemError(error);
        return -1;
    }
    return fileInfo.EndOfFile.QuadPart;
}

bool File_Win32::resize(uint64_t size) {
    // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle
    FILE_END_OF_FILE_INFO eofInfo;
    eofInfo.EndOfFile.QuadPart = size;
    if (!SetFileInformationByHandle(
        file_,
        FileEndOfFileInfo,
        &eofInfo,
        sizeof(eofInfo)))
    {
        int error = GetLastError();
        setSystemError(error);
        return false;

    }
    return true;
}

bool File_Win32::seek(uint64_t offset) {
    offset_ = offset;
    return true;
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
    setSuccess();

    // set state
    state_ = State::DISABLED;

    // disable buffers
    for (auto &buffer : buffers_) {
        buffer.setDisabled();
    }

    // resume all coroutines waiting for state change
    notify(Events::ENTER_CLOSING | Events::ENTER_DISABLED);
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
    : coco::Buffer(&overlapped_.Offset, int(headerType), new uint8_t[capacity], capacity, device.state_)
    , device_(device)
{
    device.buffers_.add(*this);

    // we don't use the event object
    overlapped_.hEvent = nullptr;
}

File_Win32::Buffer::~Buffer() {
    delete [] data_;
}

bool File_Win32::Buffer::start() {
    if (state_ != State::READY || (op_ & Op::READ_WRITE) == 0 || size_ == 0) {
        assert(state_ != State::BUSY);
        setSuccess(0);
        return false;
    }

    op2_ = op_;

    // submit operation
    if (!submit())
        return false;

    // add to list of pending transfers
    device_.transfers_.add(*this);

    // set state
    setBusy();

    return true;
}

bool File_Win32::Buffer::cancel() {
    if (state_ != State::BUSY)
        return false;

    if ((op2_ & Op::CANCEL) == 0) {
        auto result = CancelIoEx(device_.file_, &overlapped_);
        if (!result) {
            int error = GetLastError();
            setSystemError(error);
            //std::cerr << "cancel error " << e << std::endl;
            return false;
        }
        op2_ |= Op::CANCEL;
    }
    return true;
}

bool File_Win32::Buffer::submit() {
    auto &device = device_;

    // initialize overlapped
    overlapped_.Internal = 0;
    overlapped_.InternalHigh = 0;

    switch (HeaderType(headerCapacity_)) {
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
            setSystemError(error);
            return false;
        }
    }

    // increment file offset
    device.offset_ = *reinterpret_cast<uint64_t *>(&overlapped_.Offset) + size;
    return true;
}

void File_Win32::Buffer::handle(OVERLAPPED *overlapped) {
    DWORD transferred;
    auto result = GetOverlappedResult(device_.file_, overlapped, &transferred, false);
    if (result) {
        // success
        setSuccess(transferred);
    } else {
        // error
        // ERROR_OPERATION_ABORTED: cancelled
        auto error = GetLastError();
        setSystemError(error);
    }

    // remove from list of active transfers
    remove2();

    // transfer finished
    setReady();
}

} // namespace coco
