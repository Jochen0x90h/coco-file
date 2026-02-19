#include "File_io_uring.hpp"
#include <filesystem>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


namespace coco {

File_io_uring::~File_io_uring() {
    ::close(file_);
}

bool File_io_uring::open(String name, Mode mode) {
    if (file_ != INVALID_HANDLE_VALUE)
        return false;

    // open file
    std::string n(name);
    int flags = int(mode) | O_NONBLOCK | O_CLOEXEC;
    int perm = 0666;
    int file = ::open(n.c_str(), flags, perm);
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

uint64_t File_io_uring::size() {
    struct stat st;
    if (fstat(file_, &st) < 0) {
        int error = errno;
        setSystemError(error);
        return -1;
    }
    return st.st_size;
}

bool File_io_uring::resize(uint64_t size) {
    if (ftruncate(file_, size) < 0) {
        int error = errno;
        setSystemError(error);
        return false;
    }
    return true;
}

bool File_io_uring::seek(uint64_t offset) {
    offset_ = offset;
    return true;
}

int File_io_uring::getBufferCount() {
    return buffers_.count();
}

File_io_uring::Buffer &File_io_uring::getBuffer(int index) {
    return buffers_.get(index);
}

// todo: test what happens when buffers are busy when we call close()
void File_io_uring::close() {
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


// File_io_uring::Buffer

File_io_uring::Buffer::Buffer(File_io_uring &device, int capacity, HeaderType headerType)
    : coco::Buffer(&offset_, int(headerType), new uint8_t[capacity], capacity, device.state_)
    , device_(device)
{
    device.buffers_.add(*this);
}

File_io_uring::Buffer::~Buffer() {
    delete [] data_;
}

bool File_io_uring::Buffer::start() {
    if (state_ != State::READY || (op_ & Op::READ_WRITE) == 0 || size_ == 0) {
        assert(state_ != State::BUSY);
        setSuccess(0);
        return false;
    }

    flags_ = 1;

    // submit operation
    if (!transfer())
        return false;

    // set state
    setBusy();

    return true;
}

bool File_io_uring::Buffer::cancel() {
    if (state_ != State::BUSY)
        return false;

    if (flags_ != 0) {
        if (!device_.loop_.cancel(this)) {
            // error: submit buffer full
            setError(std::errc::resource_unavailable_try_again);
            return false;
        }
        flags_ = 0;
    }
    return true;
}

bool File_io_uring::Buffer::transfer() {
    auto &device = device_;

    // get offset
    uint64_t &offset = HeaderType(headerCapacity_) == HeaderType::NONE ? device.offset_ : offset_;

    // get data and size to read/write
    if (!device_.loop_.transfer((op_ & Op::WRITE) == 0 ? IORING_OP_READ : IORING_OP_WRITE,
        device.file_, offset, data_, size_, this))
    {
        // error: submit buffer full
        setError(std::errc::resource_unavailable_try_again);
        return false;
    }

    // increment file offset
    if (HeaderType(headerCapacity_) == HeaderType::NONE)
        device.offset_ += size_;
    return true;
}

void File_io_uring::Buffer::handle(io_uring_cqe &cqe) {
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

    // transfer finished
    setReady();
}

} // namespace coco
