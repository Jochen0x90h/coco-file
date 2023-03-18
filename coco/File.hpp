#pragma once

#include <coco/BufferDevice.hpp>
#include <coco/enum.hpp>
#include <coco/String.hpp>
#ifdef NATIVE
#include <filesystem>
#endif

namespace coco {

/**
	Asynchronous file abstraction. Implementations provide buffer classes derived from HeaderBuffer for the actual
	data transfer.
*/
class File : public BufferDevice {
public:
	enum class Mode {
		READ = 1,
		WRITE = 2,
		READ_WRITE = READ | WRITE,

		TRUNCATE = 4
	};

	virtual ~File();

	/**
		Open the file. If operation completes immediately the state is READY or DISABLED depending on the result.
		If the operation takes some time the state is BUSY and then goes to READY or DISABLED depending on the result.
		@param name file name
		@param mode open mode
	*/
	virtual bool open(String name, Mode mode) = 0;

#ifdef NATIVE
	template <typename T> requires (CStringConcept<T>)
	bool open(const T &name, Mode mode) {
		return open(String(name), mode);
	}

	bool open(const std::filesystem::path &name, Mode mode) {
		auto str = name.u8string();
		return open(String(str.data(), str.size()), mode);
	}
#endif

	/**
		Get size of file
		@return file size
	*/
	virtual uint64_t size() = 0;

	/**
		Set size of file
		@param size new file size
	*/
	virtual void resize(uint64_t size) = 0;

	/**
		Seek
		@param offset file offset to seek to
	*/
	virtual void seek(uint64_t offset) = 0;
};

COCO_ENUM(File::Mode)

} // namespace coco
