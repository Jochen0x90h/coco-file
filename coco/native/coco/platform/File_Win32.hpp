#include <coco/File.hpp>
#include <coco/platform/Loop_native.hpp> // includes Windows.h


namespace coco {

/**
 * File implementation using Win32 and IO completion ports
 */
class File_Win32 : public File, public Loop_Win32::CompletionHandler {
public:
	/**
	 * Constructor
	 * @param loop event loop
	 */
	File_Win32(Loop_Win32 &loop)
		: File(State::DISABLED), loop(loop) {}

	/**
	 * Constructor that opens the file
	 * @param loop event loop
	 * @param name file name
	 * @param mode open mode
	 */
	File_Win32(Loop_Win32 &loop, String name, Mode mode)
		: File(State::DISABLED), loop(loop) {open(name, mode);}

	~File_Win32() override;

	class Buffer;

	// Device methods
	//StateTasks<const State, Events> &getStateTasks() override;
	void close() override;

	// BufferDevice methods
	int getBufferCount() override;
	Buffer &getBuffer(int index) override;

	// File methods
	bool open(String name, Mode mode) override;
	using File::open;
	uint64_t size() override;
	void resize(uint64_t size) override;
	void seek(uint64_t offset) override;


	/**
		Buffer for transferring data to/from a file
	*/
	class Buffer : public coco::Buffer, public IntrusiveListNode, public IntrusiveListNode2 {
		friend class File_Win32;
	public:
		Buffer(File_Win32 &device, int size);
		~Buffer() override;

		bool start(Op op) override;
		bool cancel() override;

	protected:
		void start();
		void handle(OVERLAPPED *overlapped);

		File_Win32 &device;
		OVERLAPPED overlapped;
		Op op;
	};

protected:
	void handle(OVERLAPPED *overlapped) override;

	Loop_Win32 &loop;

	// file handle
	HANDLE file = INVALID_HANDLE_VALUE;

	// auto incrementing file offset
	uint64_t offset = 0;

	// device state
	//StateTasks<State, Events> st = State::DISABLED;

	// list of buffers
	IntrusiveList<Buffer> buffers;

	// pending transfers
	IntrusiveList2<Buffer> transfers;
};

} // namespace coco
