#include <coco/File.hpp>
#include <coco/platform/Loop_native.hpp> // includes Windows.h


namespace coco {

/// @brief File implementation using IO completion ports on Windows.
///
class File_Win32 : public File, public Loop_Win32::CompletionHandler {
public:
    /// @brief Constructor
    /// @param loop event loop
    File_Win32(Loop_Win32 &loop)
        : File(State::DISABLED), loop_(loop) {}

    /// @brief Constructor that opens the file
    /// @param loop event loop
    /// @param name file name
    /// @param mode open mode
    File_Win32(Loop_Win32 &loop, String name, Mode mode)
        : File(State::DISABLED), loop_(loop) {open(name, mode);}

    ~File_Win32() override;

    class Buffer;

    // File methods
    bool open(String name, Mode mode) override;
    using File::open;
    uint64_t size() override;
    bool resize(uint64_t size) override;
    bool seek(uint64_t offset) override;

    // BufferDevice methods
    int getBufferCount() override;
    Buffer &getBuffer(int index) override;

    // Device methods
    void close() override;


    /// @brief Buffer for transferring data to/from a file
    ///
    class Buffer : public coco::Buffer, public IntrusiveListNode {
        friend class File_Win32;
    public:
        Buffer(File_Win32 &device, int capacity, HeaderType headerType = HeaderType::NONE);
        ~Buffer() override;

        bool start() override;
        bool cancel() override;

    protected:
        bool transfer();
        void handle(OVERLAPPED *overlapped);

        File_Win32 &device_;
        OVERLAPPED overlapped_;
    };

protected:
    void handle(OVERLAPPED *overlapped) override;

    Loop_Win32 &loop_;

    // file handle
    HANDLE file_ = INVALID_HANDLE_VALUE;

    // auto incrementing file offset
    uint64_t offset_ = 0;

    // list of buffers
    IntrusiveList<Buffer> buffers_;

    // pending transfers
    //IntrusiveList2<Buffer> transfers_;
};

} // namespace coco
