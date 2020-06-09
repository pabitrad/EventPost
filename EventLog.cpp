#include "stdafx.h"
#include "EventLog.h"

#pragma comment(lib, "wevtapi")
#pragma comment(lib, "winhttp")

using HInternetPtr = std::unique_ptr<std::remove_pointer<HINTERNET>::type, decltype( &WinHttpCloseHandle )>;

inline auto HInternet(HINTERNET h)
{
	return std::unique_ptr<std::remove_pointer<HINTERNET>::type, decltype( &WinHttpCloseHandle )>(h, WinHttpCloseHandle);
}

template<class T>
T& replace(T& subject, const T& search, const T& replace)
{
	if (search.length()) {
		size_t pos = 0;
		while (( pos = subject.find(search, pos) ) != T::npos) {
			subject.replace(pos, search.length(), replace);
			pos += replace.length();
		}
	}
	return subject;
}

using EvtHandlePtr = std::unique_ptr<std::remove_pointer<EVT_HANDLE>::type, decltype( &EvtClose )>;

inline auto EvtHandle(EVT_HANDLE h)
{
	return std::unique_ptr<std::remove_pointer<EVT_HANDLE>::type, decltype( &EvtClose )>(h, EvtClose);
}

typedef union {
	ULONGLONG ft_scalar;
	FILETIME ft_struct;
} UnionFt;

std::wstring Time(const ULONGLONG& ullTime)
{
	UnionFt uIn;
	uIn.ft_scalar = ullTime;
	SYSTEMTIME st {};
	if (FileTimeToSystemTime(&uIn.ft_struct, &st)) {
		static std::wstring ws(19, L'\0');		
		size_t offset {};
		ws[offset++] = st.wYear / 1000 % 10 + L'0';
		ws[offset++] = st.wYear / 100 % 10 + L'0';
		ws[offset++] = st.wYear / 10 % 10 + L'0';
		ws[offset++] = st.wYear % 10 + L'0';
		ws[offset++] = L'/';
		ws[offset++] = st.wMonth / 10 % 10 + L'0';
		ws[offset++] = st.wMonth % 10 + L'0';
		ws[offset++] = L'/';
		ws[offset++] = st.wDay / 10 % 10 + L'0';
		ws[offset++] = st.wDay % 10 + L'0';
		ws[offset++] = L' ';
		ws[offset++] = st.wHour / 10 % 10 + L'0';
		ws[offset++] = st.wHour % 10 + L'0';
		ws[offset++] = L':';
		ws[offset++] = st.wMinute / 10 % 10 + L'0';
		ws[offset++] = st.wMinute % 10 + L'0';
		ws[offset++] = L':';
		ws[offset++] = st.wSecond / 10 % 10 + L'0';
		ws[offset++] = st.wSecond % 10 + L'0';
		return ws;
	}
	return std::wstring {};
}

std::wstring Log::readProperty(const EVT_VARIANT& value)
{
	do {
		if (value.Type == EvtVarTypeString) {
			return std::wstring { value.StringVal, value.Count };
		}
		else if (value.Type == EvtVarTypeFileTime) {
			return Time(value.FileTimeVal);
		}
		else if (value.Type == EvtVarTypeByte ||
				 value.Type == EvtVarTypeUInt16 ||
				 value.Type == EvtVarTypeUInt32 ||
				 value.Type == EvtVarTypeUInt64 ||
				 value.Type == EvtVarTypeHexInt64)
		{
			return std::to_wstring(
				value.Type == EvtVarTypeByte ? value.ByteVal :
				value.Type == EvtVarTypeUInt16 ? value.UInt16Val :
				value.Type == EvtVarTypeUInt32 ? value.UInt32Val : value.UInt64Val);
		}
	} while(false);

	return {};
}

void Log::readEvent(const EVT_HANDLE &hEvent, std::map<std::wstring, EVT_HANDLE>& providerHandles, std::set<ProviderString>& providerStrings)
{
	const wchar_t* ppValues[] = { L"Event/System/Provider/@Name", L"Event/System/Level", L"Event/System/TimeCreated/@SystemTime", L"Event/System/EventID", L"Event/System/Task" };

	DWORD cProperties {};

	static EvtHandlePtr systemContext = EvtHandle(EvtCreateRenderContext(_countof(ppValues), ppValues, EvtRenderContextValues));
	EVT_HANDLE hPublisher {};
	char* begin {}, * end {};

	DWORD required {}, used {};
	if (!EvtRender(systemContext.get(), hEvent, EvtRenderEventValues, 0, nullptr, &required, &cProperties) && ERROR_INSUFFICIENT_BUFFER == GetLastError()) {
		mValues.unacquire();
		byte* bbegin {}, * bend {};
		if (mValues.acquire(required, bbegin, bend)) {
			if (EvtRender(systemContext.get(), hEvent, EvtRenderEventValues, required, bbegin, &used, &cProperties)) {
				if (PEVT_VARIANT values = reinterpret_cast<PEVT_VARIANT>( bbegin )) {
					std::wstring wsProvider;
					size_t index = 0;
					
					for (PEVT_VARIANT valuesEnd = values + cProperties; values != valuesEnd; ++values) {
						std::wstring wsValue = readProperty(*values);

						switch (values->Type)
						{
							case EvtVarTypeString:
								ToUtf8(wsValue + L',', mOutput, begin = {}, end = {});
								if (index == 0) { // Provider
									wsProvider = wsValue;
									auto match = providerHandles.find(wsProvider);
									if (match == providerHandles.end()) {
										hPublisher = EvtOpenPublisherMetadata(nullptr, wsProvider.c_str(), nullptr, 0 /*MAKELCID(SUBLANG_NEUTRAL, SUBLANG_DEFAULT)*/, 0);
										if (!hPublisher)
											hPublisher = EvtOpenPublisherMetadata(nullptr, mName.c_str(), nullptr, 0 /*MAKELCID(SUBLANG_NEUTRAL, SUBLANG_DEFAULT)*/, 0);
										providerHandles[wsProvider] = hPublisher; // Store null handles too, to avoid repeated EvtOpenPublisherMetadata() calls for a provider that is no longer available
									}
									else
										hPublisher = match->second;
								}
								break;
							case EvtVarTypeFileTime:
								ToUtf8(wsValue + L',', mOutput, begin = {}, end = {});
								break;
							case EvtVarTypeByte:
							case EvtVarTypeUInt16:
							case EvtVarTypeUInt32:
							case EvtVarTypeUInt64:
								if (index == 1 || index == 4) { // Level or Task
									const auto format { index == 1 ? EvtFormatMessageLevel : EvtFormatMessageTask };
									uint64_t level = std::wcstoull(wsValue.c_str(), nullptr, 10);
									ProviderString providerStr { wsProvider, format, level };

									auto match = providerStrings.find(providerStr);
									if (match != providerStrings.end())
										wsValue = match->string;
									else {
										required = used = 0;
										if (!EvtFormatMessage(hPublisher, hEvent, 0, 0, nullptr, format, 0, nullptr, &required) &&
											ERROR_INSUFFICIENT_BUFFER == GetLastError() && required > 0) {
											wsValue = std::wstring(required, {});
											auto wsz = const_cast<wchar_t*>( wsValue.data() );

											if (EvtFormatMessage(hPublisher, hEvent, 0, 0, nullptr, format, required, wsz, &used)) {
												while (!wsValue.empty() && *wsValue.rbegin() == L'\0')
													wsValue.erase(wsValue.begin() + wsValue.length() - 1);

												if (!wsValue.empty()) {
													providerStr.string = wsValue;
													providerStrings.insert(providerStr);
												}
											}
										}
									}
								}
								ToUtf8(wsValue + L',', mOutput, begin = {}, end = {});
								break;
							default:
								break;
						}

						++index;
					}
				}
			}
		}
	}

	required = used = 0;
	mValues.unacquire();
	byte* bbegin {}, * bend {};
	if (hPublisher &&
		mValues.acquire(_block, bbegin, bend) &&
		EvtFormatMessage(hPublisher, hEvent, 0, 0, nullptr, EvtFormatMessageEvent, _block / 2, (wchar_t*)bbegin, &used) &&
		used > 0)
	{
		wchar_t* pBegin = (wchar_t*)bbegin;
		wchar_t* pEnd = pBegin + used - 1;
		while (*pEnd == 0)
			--pEnd;

		for (auto pCur = pBegin; pCur != pEnd; ++pCur) {
			if (*pCur == L'\r' || *pCur == L'\n')
				*pCur = L'\u00b6';
			else if (*pCur == L',')
				*pCur = L';';
			else if (*pCur == L'\"')
				*pCur = L'\'';
		}
		ToUtf8( pBegin, pEnd , mOutput, begin = {}, end = {});
	}
	else { // Event provider is probably no longer available - display the user data		
		static EvtHandlePtr userDataContext = EvtHandle(EvtCreateRenderContext(0, nullptr, EvtRenderContextUser));
		required = 0;
		cProperties = 0;
		if (!EvtRender(userDataContext.get(), hEvent, EvtRenderEventValues, 0, nullptr, &required, &cProperties) &&
			ERROR_INSUFFICIENT_BUFFER == GetLastError())
		{
			byte* bbegin {}, * bend {};
			if (mValues.acquire(required, bbegin, bend)) {
				if (EvtRender(userDataContext.get(), hEvent, EvtRenderEventValues, required, bbegin, &used, &cProperties)) {
					if ( PEVT_VARIANT values = reinterpret_cast< PEVT_VARIANT>( bbegin )) {
						for (PEVT_VARIANT valuesEnd = values + cProperties; values != valuesEnd; ++values, --cProperties) {
							auto wsValue = readProperty(*values);
							if (cProperties > 1)
							wsValue +=  L";"s;
							char* begin, * end;
							ToUtf8(wsValue, mOutput, begin = {}, end = {});
						}
					}
				}
			}
		}
	}
	ToUtf8(L"\r\n", mOutput, begin = {}, end = {});

	++mEvents;
}

Log logs[]
{
	{ LogType::Application,	L"Application"s },
	{ LogType::Security,	L"Security"s  },
	{ LogType::System,		L"System"s  }
};

void ReadLogs()
{
	const auto start = std::chrono::high_resolution_clock::now();
	size_t totalEvents {}, totalBytes {};
	wprintf(L"Reading logs...\n");

	{
		std::map<std::wstring, EVT_HANDLE> providerHandles; // providerName => EVT_HANDLE
		std::set<ProviderString> providerStrings;			// providerName, field, index, string
		// 604800000:  86400000 (= milliseconds in a day) * 7
		const std::wstring query = L"<QueryList>  <Query Id=\"0\" Path=\"%LOGNAME%\">    <Select Path=\"%LOGNAME%\">*[System[TimeCreated[timediff(@SystemTime) &lt;= 86400000]]]</Select>  </Query></QueryList>";
		for (auto& log : logs) {
			log.mEvents = {};
			log.mOutput.unacquire();
			constexpr int maxEvents = 500;
			std::vector<EVT_HANDLE> vEvents(maxEvents);
			std::wstring wsQuery { query };
			replace(wsQuery, L"%LOGNAME%"s, std::wstring { log.mName });
			if (EvtHandlePtr hResults = EvtHandle(EvtQuery(nullptr, log.mName.c_str(), {} /*wsQuery.c_str()*/, EvtQueryChannelPath | EvtQueryTolerateQueryErrors))) {

				if (log.mBookmark.used()) {
					if (EvtHandlePtr hBookmark = EvtHandle(EvtCreateBookmark(log.mBookmark[0]))) {
						EvtSeek(hResults.get(), 1, hBookmark.get(), 0, EvtSeekRelativeToBookmark);
					}
					log.mBookmark.unacquire();
				}

				log.mEvents = {};

				DWORD returnedEvents{}, first = 1;
				while (EvtNext(hResults.get(), maxEvents, vEvents.data(), INFINITE, 0, &returnedEvents)) {
					if (first) {
						char* begin {}, * end {};
						ToUtf8(L"Source,Level,TimeCreated,EventID,Task,Message\r\n"s, log.mOutput, begin, end);
						first = 0;
					}
					for (auto& evt : std::vector<EVT_HANDLE>{ vEvents.begin(), vEvents.begin() + returnedEvents }) {
						log.readEvent(evt, providerHandles, providerStrings);
						EvtClose(evt);
					}
					totalEvents += returnedEvents;
				}

				if (EvtSeek(hResults.get(), 0, nullptr, 0, EvtSeekRelativeToLast)) {
					EVT_HANDLE hLast{};
					if (EvtNext(hResults.get(), 1, &hLast, INFINITE, 0, &returnedEvents)) {
						if (auto hLastEvent = EvtHandle(hLast)) {
							if (EvtHandlePtr hBookmark = EvtHandle(EvtCreateBookmark(nullptr))) {
								if (EvtUpdateBookmark(hBookmark.get(), hLastEvent.get())) {
									DWORD dwBufferSize{}, dwBufferUsed{}, dwPropertyCount{};
									if (!EvtRender(NULL, hBookmark.get(), EvtRenderBookmark, dwBufferSize, nullptr, &dwBufferUsed, &dwPropertyCount)) {
										if (ERROR_INSUFFICIENT_BUFFER == GetLastError()) {
											wchar_t* begin {}, * end {};
											if (log.mBookmark.acquire(dwBufferSize = dwBufferUsed, begin, end))
												EvtRender(NULL, hBookmark.get(), EvtRenderBookmark, dwBufferSize, begin, &dwBufferUsed, &dwPropertyCount);
										}
									}
								}
							}
						}
					}
				}
#ifdef _DEBUG
				wprintf_s(L"Log %s: %zu events\n", log.mName.c_str(), log.mEvents);
#endif
				totalBytes += log.mOutput.used();
			}
		}

		for (auto it : providerHandles) {
			if (it.second)
				EvtClose(it.second);
		}
	}

	const auto end = std::chrono::high_resolution_clock::now();
	const std::chrono::duration<double> diff = end - start;

#ifdef _DEBUG
	wchar_t out[255]{};
	swprintf_s(out, L"%zu events, %zu bytes, %lfs\n", totalEvents, totalBytes, diff.count());
	wprintf(out);
#endif
}

void UploadLogs()
{
	auto url = 
		L"http://app.pomdit.com:3010/uploadfile"s;

	DWORD required {};
	std::wstring logFilename = L"%MACHINE%.%LOGNAME%Log.%CUSTOMER_NUMBER%"s;	// MachineName.%LOGNAME%.CustomerNumber
	
	for (auto format : { ComputerNamePhysicalNetBIOS, ComputerNameDnsHostname, ComputerNameNetBIOS }) {
		if (!GetComputerNameExW(format, nullptr, &required) && // characters, includes terminating 0
			ERROR_MORE_DATA == GetLastError()) {
			if (auto machine = std::make_unique<wchar_t[]>(required)) {
				if (GetComputerNameExW(format, machine.get(), &required) && required > 0) {
					auto machineName = std::wstring(machine.get(), required - 1);
					replace(logFilename, L"%MACHINE%"s, machineName); // mymachinename.%LOGNAME%.%CUSTOMERNUMBER%
					break;
				}
			}

		}
	}

	if (ERROR_SUCCESS == RegGetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TC_Pomdit", L"Number", RRF_SUBKEY_WOW6432KEY | RRF_RT_REG_SZ, nullptr, nullptr, &required) &&
		required > 1) 	// bytes, includes terminating 0
	{
		if (auto number = std::make_unique<wchar_t[]>(required / sizeof(wchar_t))) {
			if (ERROR_SUCCESS == RegGetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TC_Pomdit", L"Number", RRF_SUBKEY_WOW6432KEY | RRF_RT_REG_SZ, nullptr, number.get(), &required)) {
				auto customerNumber = std::wstring(number.get(), --required / sizeof(wchar_t));
				replace(logFilename, L"%CUSTOMER_NUMBER%"s, customerNumber); // mymachinename.%LOGNAME%.666
			}
		}
	}

	replace(logFilename, L"%MACHINE%"s, L"UnknownMachine"s);
	replace(logFilename, L"%CUSTOMER_NUMBER%"s, L"UnknownCustomer"s);

	do {
		auto session_ = HInternet(WinHttpOpen(L"EvtPostService/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		if (!session_) {
			wprintf((L"WinHttpOpen(): " + ErrorMessage(GetLastError())).c_str());
			break;
		}

		URL_COMPONENTS components {};
		components.dwStructSize = sizeof(components);
		components.dwHostNameLength = (DWORD)-1;
		components.dwUrlPathLength = (DWORD)-1;
		if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>( url.length() ), 0, &components)) {
			wprintf(( L"WinHttpCrackUrl(): " + ErrorMessage(GetLastError()) ).c_str());
			break;
		}

		std::wstring hostName(components.lpszHostName ? std::wstring { components.lpszHostName, components.dwHostNameLength } : L"localhost"s );
		auto connection_ = HInternet(WinHttpConnect(session_.get(), hostName.c_str(), components.nPort, 0));
		if (!connection_) {
			wprintf(( L"WinHttpConnect(): " + ErrorMessage(GetLastError()) ).c_str());
			break;
		}

		DWORD flags = ( components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 );
		auto request = HInternet(WinHttpOpenRequest(connection_.get(), L"POST", components.lpszUrlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
		if (!request) {
			wprintf(( L"WinHttpOpenRequest(): " + ErrorMessage(GetLastError()) ).c_str());
			break;
		}

		std::string sBoundary = Boundary();
		std::wstring wsBoundary = ToUnicode(sBoundary);
		std::wstring wsContentType = ContentTypeHeader(wsBoundary);

		std::string sContentType = ContentType(sBoundary);
		std::string sPostFile = Postfile(sBoundary, false), sPostFileLast = Postfile(sBoundary, true);
		size_t totalLen = 0;//sContentType.length();
		int curLog = 0, lastLogWithData = -1;

		for (const auto& log : logs) {
			if (log.mEvents)
				lastLogWithData = curLog;
			++curLog;
		}

		if (lastLogWithData < 0) {
			wprintf(L"No new events, skipping log upload...\n");
		}
		else {
			wprintf(L"Uploading logs...\n");
			curLog = 0;
			std::vector<std::string> prefiles;
			for (const auto& log : logs) {
				if (log.mEvents) {
					auto name = logFilename;
					replace(name, L"%LOGNAME%"s, std::wstring(log.mName));
					
					prefiles.emplace_back(Prefile(sBoundary, ToUtf8(name)));
					const auto& preFile = *prefiles.rbegin();
					const auto& postFile = (curLog == lastLogWithData ? sPostFileLast : sPostFile);

					totalLen += preFile.length() + log.mOutput.used() + postFile.length();
				}
				++curLog;
			}

			if (!WinHttpAddRequestHeaders(request.get(), wsContentType.c_str(), static_cast<DWORD>(wsContentType.length()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
				wprintf(L"WinHttpAddRequestHeaders(): %s, %s\n", ErrorMessage(GetLastError()).c_str(), wsContentType.c_str());
				break;
			}
			/*if (!WinHttpAddRequestHeaders(request.get(), _T("Cache-Control: no-cache"), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
				wprintf(L"WinHttpAddRequestHeaders(cacheControl): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}
			if (!WinHttpAddRequestHeaders(request.get(), _T("Accept-Language: en-us,en"), static_cast<DWORD>( -1 ), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
				wprintf(L"WinHttpAddRequestHeaders(acceptLang): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}*/

			if (components.nScheme == INTERNET_SCHEME_HTTPS) {
				DWORD bufferLength = sizeof(flags);
				flags = {};
				if (WinHttpQueryOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &flags, &bufferLength)) {
					flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
					WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
				}
			}

			std::wstring contentLen = ContentLength(totalLen);
			if (!WinHttpSendRequest(request.get(), contentLen.c_str(), static_cast<DWORD>(contentLen.length()), WINHTTP_NO_REQUEST_DATA, 0, WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0)) {
				wprintf(L"WinHttpSendRequest: %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}

			DWORD written {};
			//if (!WinHttpWriteData(request.get(), sContentType.c_str(), static_cast<DWORD>( sContentType.length() ), &written)) {
			//	wprintf(L"WinHttpWriteData(contentType): %s\n", ErrorMessage(GetLastError()).c_str());
			//	break;
			//}

			curLog = 0;
			auto sPrefile = prefiles.begin();
			auto sent = true;

			for (auto& log : logs) {
				if (log.mEvents) {
					written = 0;
					if (!WinHttpWriteData(request.get(), sPrefile->c_str(), static_cast<DWORD>( sPrefile->length() ), &written)) {
						wprintf(L"WinHttpWriteData(prefile): %s\n", ErrorMessage(GetLastError()).c_str());
						sent = false;
						break;
					}
					
					//constexpr size_t chunk = 104857600; // 10240;
					const size_t len = log.mOutput.used();
					//const size_t chaff = len % chunk;
					//const auto chunks = len / chunk + ( chaff ? 1 : 0 );

					if (!WinHttpWriteData(request.get(), log.mOutput[0], static_cast<DWORD>(len), &written)) {
						wprintf(L"WinHttpWriteData(logOutput %s\n", log.mName.c_str(), ErrorMessage(GetLastError()).c_str());
						sent = false;
						break;
					}
					//for (size_t cur = 0; cur < chunks; ++cur) {
					//	char* begin = log.mOutput[0] + cur * chunk;
					//	char* end = begin + ( cur + 1 == chunks ? ( chaff ? chaff : chunk ) : chunk );
					//	if (end > begin) {
					//		if (!WinHttpWriteData(request.get(), begin, static_cast<DWORD>(end - begin), &written)) {
					//			wprintf(L"WinHttpWriteData(logOutput[%zd / %zd, %s]): %s\n", cur, chunks, log.mName.c_str(), ErrorMessage(GetLastError()).c_str());
					//			sent = false;
					//			break;
					//		}
					//	}
					//}

					//if (!sent)
						//break;

					const auto& postFile = (curLog == lastLogWithData ? sPostFileLast : sPostFile);
					if (!postFile.empty() && !WinHttpWriteData(request.get(), postFile.c_str(), static_cast<DWORD>(postFile.length()), &written)) {
						wprintf(L"WinHttpWriteData(postFile): %s\n", ErrorMessage(GetLastError()).c_str());
						sent = false;
						break;
					}
					++sPrefile;
				}
				++curLog;
			}

			if (!sent)
				break;

			if (!WinHttpReceiveResponse(request.get(), nullptr)) {
				wprintf(L"WinHttpReceiveResponse(): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}

			DWORD status {}, available {}, len = sizeof(status);
			if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
				wprintf(L"WinHttpQueryHeaders(): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}

			wprintf(L"Server returned %lu\n", status);
			break;
			/*
			enum {
				kBufferLength_ = 8 * 1024,
			};

			auto buffer = std::make_unique<char[]>(kBufferLength_ + 1);

			if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0) {
				if (available != 0)
					wprintf(L"WinHttpQueryDataAvailable(): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}

			DWORD read;
			if (!WinHttpReadData(request.get(), buffer.get(), kBufferLength_, &read)) {
				wprintf(L"WinHttpReadData(): %s\n", ErrorMessage(GetLastError()).c_str());
				break;
			}

			printf("WinHttpReadData(): [%.*s]\n", static_cast<int>(read), buffer.get());
			*/
		}
	}
	while (false);
}

/*
Content-Type: multipart/form-data; charset=utf-8; boundary=BOUNDARY

--BOUNDARY
Content-Disposition: form-data; name="file"; filename="foo.txt"

(content of the uploaded file foo.txt)
--BOUNDARY
Content-Disposition: form-data; name="file"; filename="bar.txt"

(content of the uploaded file bar.txt)
--BOUNDARY--

*/

std::string Boundary()
{
	constexpr char srcCharacters[] = "0123456789"
									 "abcdefghijklmnopqrstuvwxyz"
									 "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	constexpr int cSrc = _countof(srcCharacters);

	std::random_device random_device;
	std::mt19937 engine(random_device());
	std::uniform_int_distribution<> randomInt(0, cSrc - 2);

	constexpr size_t cBoundary = 22;
	std::string boundary(cBoundary, '_');

	for (auto it = boundary.begin(); it != boundary.end(); ++it)
		*it = srcCharacters[randomInt(engine)];

	assert(strlen(boundary.data()) == cBoundary);
	return boundary;
}

std::string Prefile(const std::string& boundary, const std::string& fileName)
{
	std::string str = "--%BOUNDARY%\r\n"
					  "Content-Disposition: form-data; name=\"%FILENAME%\"\r\n"
					  "\r\n";

	return replace(replace(str, "%BOUNDARY%"s, boundary), "%FILENAME%"s, fileName);
}

std::string Postfile(const std::string& boundary, const bool last)
{
	std::string strLast = "--%BOUNDARY%\r\n";

	return (last ? replace(strLast, "%BOUNDARY%"s, boundary) : std::string{"\r\n"});
}

std::string ContentType(const std::string& boundary)
{
	std::string str = "Content-Type: multipart/form-data; charset=utf-8; boundary=\"%BOUNDARY%\"";

	return replace(str, "%BOUNDARY%"s, boundary);
}

std::wstring ContentTypeHeader(const std::wstring& boundary)
{
	std::wstring str = L"Content-Type: multipart/form-data; boundary=\"%BOUNDARY%\"";

	return replace(str, L"%BOUNDARY%"s, boundary);
}

std::wstring ContentLength(const size_t len)
{
	std::wstring str = L"Content-Length: %LENGTH%\r\n";

	return replace(str, L"%LENGTH%"s, std::to_wstring(len));
}

std::wstring ToUnicode(const std::string& src)
{
	if (const auto srcLen = static_cast<int>( src.length() )) {
		if (int needed = MultiByteToWideChar(CP_ACP, 0, src.data(), srcLen, nullptr, 0)) {
			std::wstring dest(needed, L' ');
			if ((needed = MultiByteToWideChar(CP_ACP, 0, src.data(), srcLen, const_cast<wchar_t*>( dest.data() ), needed)))
				return dest;
		}
	}
	return std::wstring {};
}

bool ToUtf8(const std::wstring& src, Buffer<char> &buffer, char*& begin, char*& end)
{
	end = begin = {};
	if (const auto srcLen = static_cast<int>(src.length())) {
		const int needed = WideCharToMultiByte(CP_UTF8, 0, src.data(), srcLen, nullptr, 0, nullptr, nullptr);
		if (buffer.acquire(needed, begin, end) && begin && end  && WideCharToMultiByte(CP_UTF8, 0, &src[0], srcLen, begin, needed, nullptr, nullptr))
			return true;
	}
	return {};
}

bool ToUtf8(const wchar_t*pBegin, const wchar_t* pEnd, Buffer<char>& buffer, char*& begin, char*& end)
{
	end = begin = {};
	if (const auto srcLen = static_cast<int>( pEnd - pBegin )) {
		const int needed = WideCharToMultiByte(CP_UTF8, 0, pBegin, srcLen, nullptr, 0, nullptr, nullptr);
		if (buffer.acquire(needed, begin, end) && begin && end && WideCharToMultiByte(CP_UTF8, 0, pBegin, srcLen, begin, needed, nullptr, nullptr))
			return true;
	}
	return {};
}

std::string ToUtf8(const std::wstring& src)
{
	 if (const auto srcLen = static_cast<int>( src.length() )) {
	    const int needed = WideCharToMultiByte(CP_ACP, 0, src.data(), srcLen, nullptr, 0, nullptr, nullptr);
	    std::string result(needed, 0);
	    WideCharToMultiByte(CP_ACP, 0, &src[0], srcLen, &result[0], needed, nullptr, nullptr);
	    return result;
	}
	 return std::string {};
}

std::wstring& ToCsv(std::wstring& message)
{
	/*
		Original text			CSV format					Change
		some "quoted" text		"some ""quoted"" text"		escape each quotation mark, and wrap the entire value in quotation marks
		some, more				"some, more"				wrap the entire value in quotation marks
		line CRLF broken		"line CRLF broken"			wrap the entire value in quotation marks
		
		Save some cycles and bytes by 
		1. using the Pilcrow character (used by MS Word to display line breaks)
		2. replacing each embedded quotation mark with a single quote
		3. replacing each comma with a semicolon
	*/
										
	replace(message, L"\r\n"s, L"\u00b6"s);
	replace(message, L"\r"s, L"\u00b6"s);
	replace(message, L"\n"s, L"\u00b6"s);
	std::replace(message.begin(), message.end(), L',', L';');
	std::replace(message.begin(), message.end(), L'\"', L'\'');
	
	return message;
}

std::wstring ErrorMessage(DWORD dwMessageId /*= GetLastError()*/)
{
	HMODULE h = GetModuleHandle(L"Winhttp");
	constexpr DWORD dwSize = 512;
	HANDLE hHeap = GetProcessHeap();
	LPWSTR lpwszBuffer = static_cast<LPWSTR>( HeapAlloc(hHeap, 0, dwSize * sizeof(WCHAR)) );
	if (lpwszBuffer) {
		if (0 == FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, h, dwMessageId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), lpwszBuffer, dwSize, nullptr))
			_ltow_s(dwMessageId, lpwszBuffer, dwSize, 16);
		else {
			for (WCHAR* p; ( p = wcschr(lpwszBuffer, L'\r') ) != nullptr; *p = L' ') {}
			for (WCHAR* p; ( p = wcschr(lpwszBuffer, L'\n') ) != nullptr; *p = L' ') {}
		}

		std::wstring wsError = lpwszBuffer;
		HeapFree(hHeap, 0, lpwszBuffer);
		return wsError;
	}
	return std::wstring {};
}