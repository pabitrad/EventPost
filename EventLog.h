#pragma once

#pragma warning(push)
#pragma warning(disable : 6001 26451 26461 26477 26485 26486 26493 26486 26489 26494)
#pragma warning(pop)

constexpr long long _block = 2048000LL;

template <typename T>
class Buffer
{
	T* mPtr;
	size_t mSize;
	size_t mOffset;
	
public:
	Buffer() : mPtr(nullptr), mSize {}, mOffset {}	{ resize(_block); }
	~Buffer()										{ if (mPtr) std::free(mPtr); }

	Buffer(Buffer&&) = delete;
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	Buffer& operator=(Buffer&&) = delete;
	
	bool acquire(size_t required, T* &begin, T* &end)
	{
		begin = end = nullptr;
		if (mOffset + required < mSize || resize(mOffset + required)) {
			begin = mPtr + mOffset;
			end = begin + required;
			assert(mOffset + required <= mSize);
			mOffset += required;
			return true;
		}
		return false;
	}

	void unacquire()				{ mOffset = 0; }
	size_t size() const				{ return mSize; }
	size_t used() const				{ return mOffset; }

	T* operator[](size_t n)			{ return n < mOffset ? &mPtr[n] : nullptr; }
	T* operator[](size_t n) const	{ return n < mOffset ? &mPtr[n] : nullptr; }

private:
	bool resize(std::size_t newSize)
	{
		void* mem = {};
		auto roundedUpSize = newSize == 0 ? 0 : ( newSize + _block - 1 ) & -_block;

		if (newSize == 0 || !( mem = std::realloc(mPtr, roundedUpSize * sizeof(T)) )) {
			if (newSize == 0)	// zero-size realloc is deprecated in C
				std::free(mPtr);
			mPtr = nullptr;
			mSize = mOffset = 0;
			return false;
		}
		mPtr = static_cast<T*>( mem );
		mSize = roundedUpSize;
		return true;
	}
};

struct ProviderString
{
	std::wstring publisher;
	EVT_FORMAT_MESSAGE_FLAGS field; // EvtFormatMessageLevel or EvtFormatMessageTask
	uint64_t index;					// zero-based index in provider's Msg Table
	std::wstring string;

	bool operator<(const ProviderString& other) const
	{
		const int comp = publisher.compare(other.publisher);
		return comp < 0 || ( comp == 0 && ( field < other.field || ( field == other.field && index < other.index ) ) );
	}
};

enum class LogType {
	Application, Security, System
};

struct Log
{
	LogType mType;
	const std::wstring mName;
	size_t mEvents;
	Buffer<wchar_t> mBookmark;
	Buffer<char> mOutput {};
	Buffer<byte> mValues {};

	void readEvent(const EVT_HANDLE& hEvent, std::map<std::wstring, EVT_HANDLE>& providerHandles, std::set<ProviderString>& providerStrings);
	std::wstring readProperty(const EVT_VARIANT& value);
};

std::wstring ErrorMessage(DWORD dwMessageId = GetLastError());
bool ToUtf8(const std::wstring& src, Buffer<char>& buffer, char*& begin, char*& end);
bool ToUtf8(const wchar_t* pBegin, const wchar_t* pEnd, Buffer<char>& buffer, char*& begin, char*& end);

std::string Boundary();
std::string Prefile(const std::string& boundary, const std::string& fileName);
std::string Postfile(const std::string& boundary, const bool last);
std::wstring ContentTypeHeader(const std::wstring& boundary);
std::string ContentType(const std::string& boundary);
std::wstring ContentLength(const size_t len);

std::wstring ToUnicode(const std::string& src);
std::string ToUtf8(const std::wstring& src);
std::wstring& ToCsv(std::wstring& message);

void ReadLogs();
void UploadLogs();

