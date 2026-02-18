#pragma once

#include <coco/BufferDevice.hpp>
#include <coco/enum.hpp>
#include <coco/String.hpp>
#ifdef NATIVE
#include <filesystem>
#endif

namespace coco {

/// @brief Asynchronous file abstraction. Implementations provide buffer classes derived from HeaderBuffer for the actual
/// data transfer.
class File : public BufferDevice {
public:
    /// @brief File open mode.
    ///
    enum class Mode {
        READ = 1,
        WRITE = 2,
        READ_WRITE = READ | WRITE,

        // truncate the file when opening
        TRUNCATE = 4,

        // create a new file and open in write mode
        CREATE = TRUNCATE | WRITE,
    };

    /// @brief Buffer header type.
    ///
    enum class HeaderType : uint8_t {
        // header is not used, each buffer gets written at the current file position which is incremented
        NONE = 0,

        // header contains a file offset of 4 bytes size
        OFFSET_4 = 4,

        // header contains a file offset of 8 bytes size
        OFFSET_8 = 8
    };

    File(State state) : BufferDevice(state) {}
    virtual ~File();

    /// @brief Open the file. If operation completes immediately the state is READY or DISABLED depending on the result.
    /// If the operation takes some time the state is BUSY and then goes to READY or DISABLED depending on the result.
    /// @param name file name
    /// @param mode open mode
    /// @return True on success
    virtual bool open(String name, Mode mode) = 0;

#ifdef NATIVE
    template <typename T> requires (CStringConcept<T>)
    bool open(const T &name, Mode mode) {
        return open(String(name), mode);
    }

    /// @brief Open the file using std::filesystem::path.
    /// @param name file name
    /// @param mode open mode
    /// @return True on success
    bool open(const std::filesystem::path &name, Mode mode) {
        auto str = name.u8string();
        return open(String(str.data(), str.size()), mode);
    }
#endif

    /// @brief Get size of file.
    /// @return file size
    /// @return File size on success, -1 on error and error() contains the error code
    virtual uint64_t size() = 0;

    /// @brief Set size of file.
    /// @param size new file size
    /// @return true on success, false on error and error() contains the error code
    virtual bool resize(uint64_t size) = 0;

    /// @brief Seek.
    /// @param offset file offset to seek to
    /// @return true on success, false on error and error() contains the error code
    virtual bool seek(uint64_t offset) = 0;
};

COCO_ENUM(File::Mode)

} // namespace coco
