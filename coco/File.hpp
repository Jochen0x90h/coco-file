#pragma once

#include <coco/BufferDevice.hpp>
#include <coco/enum.hpp>
#include <coco/String.hpp>
#ifdef NATIVE
#include <filesystem>
#endif
#if defined(_WIN32)
#include <coco/platform/WindowsDef.hpp>
#include <windows.h>
#include <coco/platform/WindowsUndef.hpp>
#elif defined(__linux__)
#include <fcntl.h>
#endif


namespace coco {

/// @brief Asynchronous file abstraction. Implementations provide buffer classes derived from HeaderBuffer for the actual
/// data transfer.
class File : public BufferDevice {
public:
    /// @brief File open mode.
    ///
    enum class Mode : uint32_t {
#if defined(_WIN32)
        // open existing file in read-only mode
        OPEN_READ = OPEN_EXISTING | GENERIC_READ,

        // open existing file in read/write mode
        OPEN = OPEN_EXISTING | GENERIC_READ | GENERIC_WRITE,

        // truncate existing file and open in read/write mode
        TRUNCATE = TRUNCATE_EXISTING | GENERIC_READ | GENERIC_WRITE,

        // create file if it does not exist or open in read/write mode
        CREATE_OR_OPEN = OPEN_ALWAYS | GENERIC_READ | GENERIC_WRITE,

        // create file if it does not exist or truncate existing file and open in read/write mode
        CREATE_OR_TRUNCATE = CREATE_ALWAYS | GENERIC_READ | GENERIC_WRITE,

        // create file if it does not exist of fail if it exists and open in read/write mode
        CREATE_OR_FAIL = CREATE_NEW | GENERIC_READ | GENERIC_WRITE,
#elif defined(__linux__)
        // open existing file in read-only mode
        OPEN_READ = O_RDONLY,

        // open existing file in read/write mode
        OPEN = O_RDWR,

        // truncate existing file and open in read/write mode
        TRUNCATE = O_TRUNC | O_RDWR,

        // create file if it does not exist or open in read/write mode
        CREATE_OR_OPEN = O_CREAT | O_RDWR,

        // create file if it does not exist or truncate existing file and open in read/write mode
        CREATE_OR_TRUNCATE = O_CREAT | O_TRUNC | O_RDWR,

        // create file if it does not exist of fail if it exists and open in read/write mode
        CREATE_OR_FAIL = O_CREAT | O_EXCL | O_RDWR,
#else
        // open existing file in read-only mode
        OPEN_READ = 1,

        // open existing file in read/write mode
        OPEN = 3,

        // truncate existing file and open in read/write mode
        TRUNCATE = (1 << 2) | 3,

        // create file if it does not exist or open in read/write mode
        CREATE_OR_OPEN = (2 << 2) | 3,

        // create file if it does not exist or truncate existing file and open in read/write mode
        CREATE_OR_TRUNCATE = (3 << 2) | 3,

        // create file if it does not exist of fail if it exists and open in read/write mode
        CREATE_OR_FAIL = (4 << 2) | 3,
#endif
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

#ifdef NATIVE
    /// @brief Open the file. If operation completes immediately the state is READY or DISABLED depending on the result.
    /// If the operation takes some time the state is BUSY and then goes to READY or DISABLED depending on the result.
    /// @param name file path and name
    /// @param mode open mode
    /// @return True on success
    virtual bool open(const std::filesystem::path &path, Mode mode) = 0;

    bool open(String path, Mode mode) {
        std::filesystem::path p(std::u8string_view(reinterpret_cast<const char8_t *>(path.data()), path.size()));
        return open(p, mode);
    }

    template <typename T> requires (CStringConcept<T>)
    bool open(const T &path, Mode mode) {
        return open(String(path), mode);
    }

#else
    /// @brief Open the file. If operation completes immediately the state is READY or DISABLED depending on the result.
    /// If the operation takes some time the state is BUSY and then goes to READY or DISABLED depending on the result.
    /// @param path file path and name
    /// @param mode open mode
    /// @return True on success
    virtual bool open(String path, Mode mode) = 0;
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

} // namespace coco
