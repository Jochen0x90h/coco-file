#include "File_io_uring.hpp"
//#include <iostream>
#include <filesystem>


namespace coco {

namespace {
int getFlags(Mode mode) {
    int flags = O_NONBLOCK;

    switch (mode & Mode::READ_WRITE) {
        case Mode::READ:
            flags |= O_RDONLY;
            break;
        case Mode::WRITE:
            flags |= O_WRONLY;
            break;
        case Mode::READ_WRITE:
            flags |= O_RDWR;
            break;
    }

    if ((mode & Mode::TRUNCATE) != 0) {
        flags |= O_TRUNC;
    }

    // close on exec for safety
    flags |= O_CLOEXEC;

    return flags;
}
}

File_io_uring::~File_io_uring() {
    ::close(file_);
}

bool File_io_uring::open(String name, Mode mode) {
    if (file_ != INVALID_HANDLE_VALUE)
        return false;

    // open file
    std::string n = name;
    int flags = getFlags(mode);
    int perm = 0666;
    int file = open(n.c_str(), flags, perm);
    if (file == INVALID_HANDLE_VALUE) {
        int error = errno;
        setSystemError(error);
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
    struct stat st;
    if (fstat(file_, &st) < 0) {
        int error = errno;
        setSystemError(error);
        return -1;
    }
    return st.st_size;
}

bool File_Win32::resize(uint64_t size) {
    if (ftruncate(file_, size) < 0) {
        int error = errno;
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
    ::close(file_);

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


// File_Win32::Buffer

File_Win32::Buffer::Buffer(File_Win32 &device, int capacity, HeaderType headerType)
    : coco::Buffer(&offset_, int(headerType), new uint8_t[capacity], capacity, device.state_)
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
        if (!device_.loop_.cancel(this)) {
            // error: submit buffer full
            setError(std::errc::resource_unavailable_try_again);
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

    // get offset
    uint64_t &offset = HeaderType(headerCapacity_) == HeaderType::NONE ? device.offset_ : offset_;

    // get data and size to read/write
    if (!device_.loop_.submit((op_ & Op::WRITE) == 0 ? IORING_OP_READ : IORING_OP_WRITE,
        device.file_, offset, data_, size_, this))
    {
        // error: submit buffer full
        setError(std::errc::resource_unavailable_try_again);
        return false;
    }

    // increment file offset
    device.offset_ = *reinterpret_cast<uint64_t *>(&overlapped_.Offset) + size;
    return true;
}

void File_Win32::Buffer::handle(io_uring_cqe &cqe) {
    int result = cqe.res;
    if (result >= 0) {
        // success
        setSuccess(result);
    } else {
        // error
        // EMSGSIZE: datagram was larger than buffer (for UDP)
        // ECANCELED: cancelled
        int error = -result;
        setSystemError(error);
    }

    // remove from list of active transfers
    remove2();

    // transfer finished
    setReady();
}

} // namespace coco
