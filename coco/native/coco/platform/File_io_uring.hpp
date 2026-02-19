#include <coco/File.hpp>
#include <coco/platform/Loop_native.hpp>


namespace coco {

/// @brief File implementation using io_uring on Linux.
///
class File_io_uring : public File {
public:
    /// @brief Constructor
    /// @param loop event loop
    File_io_uring(Loop_io_uring &loop)
        : File(State::DISABLED), loop_(loop) {}

    /// @brief Constructor that opens the file
    /// @param loop event loop
    /// @param name file name
    /// @param mode open mode
    File_io_uring(Loop_io_uring &loop, String name, Mode mode)
        : File(State::DISABLED), loop_(loop) {open(name, mode);}

    ~File_Win32() override;

    class Buffer;

    // File methods
    bool open(String name, Mode mode) override;
    using File::open;
    uint64_t size() override;
    void resize(uint64_t size) override;
    void seek(uint64_t offset) override;

    // BufferDevice methods
    int getBufferCount() override;
    Buffer &getBuffer(int index) override;

    // Device methods
    void close() override;


    /// @brief Buffer for transferring data to/from a file
    ///
    class Buffer : public coco::Buffer, public IntrusiveListNode {
        friend class File_io_uring;
    public:
        Buffer(File_io_uring &device, int capacity, HeaderType headerType = HeaderType::NONE);
        ~Buffer() override;

        bool start() override;
        bool cancel() override;

    protected:
        bool transfer();
        void handle(io_uring_cqe &cqe);

        File_io_uring &device_;
        uint64_t offset_ = 0;
    };

protected:
    Loop_io_uring &loop_;

    // file handle
    static constexpr int INVALID_HANDLE_VALUE = -1;
    int file_ = INVALID_HANDLE_VALUE;

    // auto incrementing file offset
    uint64_t offset_ = 0;

    // list of buffers
    IntrusiveList<Buffer> buffers_;
};

} // namespace coco
