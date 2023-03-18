#include <coco/File.hpp>
#include <coco/BufferImpl.hpp>
#include <coco/platform/Loop_Win32.hpp> // includes Windows.h


namespace coco {

/**
	File implementation using Win32 and IO completion ports
*/
class File_Win32 : public File, public Loop_Win32::CompletionHandler {
public:
	/**
		Constructor
		@param loop event loop
	*/
	File_Win32(Loop_Win32 &loop) : loop(loop) {}

	/**
		Constructor that opens the file
		@param loop event loop
		@param name file name
		@param mode open mode
	*/
	File_Win32(Loop_Win32 &loop, String name, Mode mode) : loop(loop) {open(name, mode);}

	~File_Win32() override;

	class Buffer;

	State state() override;
	[[nodiscard]] Awaitable<> stateChange(int waitFlags = -1) override;
	int getBufferCount() override;
	Buffer &getBuffer(int index) override;

	bool open(String name, Mode mode) override;
	using File::open;
	void close() override;
	uint64_t size() override;
	void resize(uint64_t size) override;
	void seek(uint64_t offset) override;


	/**
		Buffer for transferring data to/from a file
	*/
	class Buffer : public BufferImpl, public LinkedListNode {
		friend class File_Win32;
	public:
		Buffer(File_Win32 &file, int size);
		~Buffer() override;

		bool setHeader(const uint8_t *data, int size) override;
		using BufferImpl::setHeader;
		bool startInternal(int size, Op op) override;
		void cancel() override;

	protected:
		void handle(OVERLAPPED *overlapped);

		File_Win32 &file;
		OVERLAPPED overlapped;

		// use file.offset
		bool useOffset = true;
	};

protected:
	void handle(OVERLAPPED *overlapped) override;

	Loop_Win32 &loop;

	// file handle
	HANDLE file = INVALID_HANDLE_VALUE;

	// state
	State stat = State::DISABLED;

	// auto incrementing file offset
	uint64_t offset = 0;

	CoroutineTaskList<> stateTasks;

	LinkedList<Buffer> buffers;
};

} // namespace coco
