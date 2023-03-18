#pragma once

#include <coco/Device.hpp>
#include <coco/enum.hpp>
#include <coco/String.hpp>


namespace coco {

/**
	Asynchronous file abstraction. Implementations provide buffer classes derived from HeaderBuffer for the actual
	data transfer.
*/
class File : public Device {
public:
	enum class Mode {
		READ = 1,
		WRITE = 2,
		READ_WRITE = READ | WRITE,

		TRUNCATE = 4
	};

	virtual ~File();

	/**
		Open the file
		@param name file name
		@param mode open mode
	*/
	virtual bool open(String name, Mode mode) = 0;

	/**
		Close the file
	*/
	virtual void close() = 0;

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
