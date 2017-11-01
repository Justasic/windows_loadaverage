// loadaverage.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"

// Various variables
HINSTANCE hInst;
HANDLE LoadThread;
DWORD ThreadID;
std::atomic<bool> quitting;
CRITICAL_SECTION   LoadLock;
CONDITION_VARIABLE cvLoad;
const char szClassName[] = "Load Average";
const char szWindowTitle[] = "Load Avg.";

template<typename T>
class LoadAverage
{
	// Our current load average right now.
	T loadavg;
	// The "magic" number for calculating the load against.
	T magic;
public:
	// Make sure we know what we are.
	typedef T datatype;

	// Calculate a magic number so you can do adjustments I guess.
	static inline T CalculateMagic(T SampleRate, T TimeFrame)
	{
		return std::exp(-SampleRate / TimeFrame);
	}

	LoadAverage(T magic) : loadavg(0.0), magic(magic) {}
	LoadAverage() : loadavg(0.0), magic(0.0) {}

	// Get the load average.
	T operator()() const { return this->loadavg; }

	// add to the load average.
	LoadAverage &operator +=(size_t n) { this->Add(n); return *this; }

	void Add(size_t n)
	{
		this->loadavg *= this->magic;
		this->loadavg += n * (1 - this->magic);
	}
	T Get() const { return this->loadavg; }

	// Implement the cast operator so C++ can implicit cast this class.
	operator T() const { return this->loadavg; }
};

#ifdef _DEBUG
void XTrace(LPCTSTR lpszFormat, ...)
{
	va_list list;
	va_start(list, lpszFormat);
	int nBuf;
	TCHAR szBuffer[512];
	nBuf = _vsnprintf_s(szBuffer, 511, lpszFormat, list);
	OutputDebugString(szBuffer);
	va_end(list);
}
#else
# define XTrace(...)
#endif

inline COLORREF CreateHexColor(int hex)
{
	int red = ((hex >> 16) & 0xFF) / 1.0;
	int green = ((hex >> 8) & 0xFF) / 1.0;
	int blue = ((hex)& 0xFF) / 1.0;

	if (red < 256 && green < 256 && blue < 256)
		return RGB(red, green, blue);
	return NULL;
}

inline HBRUSH CreateHexBrush(int hex)
{
	int red = ((hex >> 16) & 0xFF) / 1.0;
	int green = ((hex >> 8) & 0xFF) / 1.0;
	int blue = ((hex)& 0xFF) / 1.0;

	if (red < 256 && green < 256 && blue < 256)
		return CreateSolidBrush(RGB(red, green, blue));

	return NULL;
}

DWORD WINAPI LoadAverageCalculationThread(LPVOID lpParam)
{
	HWND hwnd = *reinterpret_cast<HWND*>(lpParam);
	// Calculate the magic values for the load times
	static const double EXP_1 = LoadAverage<double>::CalculateMagic(2.0f, 60.0f);
	static const double EXP_5 = LoadAverage<double>::CalculateMagic(2.0f, 300.0f);
	static const double EXP_15 = LoadAverage<double>::CalculateMagic(2.0f, 900.0f);

	// Our load averages
	LoadAverage<double> Load_1(EXP_1);
	LoadAverage<double> Load_5(EXP_5);
	LoadAverage<double> Load_15(EXP_15);

	HRESULT hres;

	// Step 1: --------------------------------------------------
	// Initialize COM. ------------------------------------------

	hres = CoInitializeEx(0, COINIT_MULTITHREADED);
	if (FAILED(hres))
	{
		// std::cout << "Failed to initialize COM library. Error code = 0x" << std::hex << hres << std::endl;
		return 0;                  // Program has failed.
	}

	// Step 2: --------------------------------------------------
	// Set general COM security levels --------------------------

	hres = CoInitializeSecurity(
		NULL,
		-1,                          // COM authentication
		NULL,                        // Authentication services
		NULL,                        // Reserved
		RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication 
		RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
		NULL,                        // Authentication info
		EOAC_NONE,                   // Additional capabilities 
		NULL                         // Reserved
		);


	if (FAILED(hres))
	{
		// std::cout << "Failed to initialize security. Error code = 0x" << std::hex << hres << std::endl;
		CoUninitialize();
		return 0;                    // Program has failed.
	}

	// Step 3: ---------------------------------------------------
	// Obtain the initial locator to WMI -------------------------

	IWbemLocator *pLoc = NULL;

	hres = CoCreateInstance(
		CLSID_WbemLocator,
		0,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator, (LPVOID *)&pLoc);

	if (FAILED(hres))
	{
		// std::cout << "Failed to create IWbemLocator object." << " Err code = 0x" << std::hex << hres << std::endl;
		CoUninitialize();
		return 0;                 // Program has failed.
	}

	// Step 4: -----------------------------------------------------
	// Connect to WMI through the IWbemLocator::ConnectServer method

	IWbemServices *pSvc = NULL;

	// Connect to the root\cimv2 namespace with
	// the current user and obtain pointer pSvc
	// to make IWbemServices calls.
	hres = pLoc->ConnectServer(
		_bstr_t(L"ROOT\\CIMV2"), // Object path of WMI namespace
		NULL,                    // User name. NULL = current user
		NULL,                    // User password. NULL = current
		0,                       // Locale. NULL indicates current
		NULL,                    // Security flags.
		0,                       // Authority (for example, Kerberos)
		0,                       // Context object 
		&pSvc                    // pointer to IWbemServices proxy
		);

	if (FAILED(hres))
	{
		// std::cout << "Could not connect. Error code = 0x" << std::hex << hres << std::endl;
		pLoc->Release();
		CoUninitialize();
		return 0;                // Program has failed.
	}

	// std::cout << "Connected to ROOT\\CIMV2 WMI namespace" << std::endl;


	// Step 5: --------------------------------------------------
	// Set security levels on the proxy -------------------------

	hres = CoSetProxyBlanket(
		pSvc,                        // Indicates the proxy to set
		RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
		RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
		NULL,                        // Server principal name 
		RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
		RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
		NULL,                        // client identity
		EOAC_NONE                    // proxy capabilities 
		);

	if (FAILED(hres))
	{
		// std::cout << "Could not set proxy blanket. Error code = 0x" << std::hex << hres << std::endl;
		pSvc->Release();
		pLoc->Release();
		CoUninitialize();
		return 0;               // Program has failed.
	}

	// Step 6: --------------------------------------------------
	// Use the IWbemServices pointer to make requests of WMI ----

	// For example, get the name of the operating system
	IEnumWbemClassObject* pEnumerator = NULL, *pEnum2 = NULL;
	std::stringstream ss;

	while (!quitting.load())
	{
		// Query for Processor Queue Length
		hres = pSvc->ExecQuery(
			bstr_t("WQL"),
			bstr_t("SELECT ProcessorQueueLength FROM Win32_PerfFormattedData_PerfOS_System"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
			NULL,
			&pEnumerator);

		if (FAILED(hres))
		{
			// std::cout << "Query for operating system name failed. Error code = 0x" << std::hex << hres << std::endl;
			pSvc->Release();
			pLoc->Release();
			CoUninitialize();
			return 0;               // Program has failed.
		}

		hres = pSvc->ExecQuery(
			bstr_t("WQL"),
			bstr_t("SELECT CurrentDiskQueueLength FROM Win32_PerfFormattedData_PerfDisk_PhysicalDisk"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
			NULL,
			&pEnum2);

		if (FAILED(hres))
		{
			// std::cout << "Query for operating system name failed. Error code = 0x" << std::hex << hres << std::endl;
			pSvc->Release();
			pLoc->Release();
			CoUninitialize();
			return 0;               // Program has failed.
		}

		// Step 7: -------------------------------------------------
		// Get the data from the query in step 6 -------------------

		IWbemClassObject *pclsObj = NULL, *pclsObj2 = NULL;
		ULONG uReturn = 0, uReturn2 = 0;

		while (pEnumerator && pEnum2 && !quitting.load())
		{
			HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
			HRESULT hr2 = pEnum2->Next(WBEM_INFINITE, 1, &pclsObj2, &uReturn2);

			if (0 == uReturn || uReturn2 == 0)
				break;

			VARIANT vtProp, vtProp2;

			// Get the value of the Name property
			hr = pclsObj->Get(L"ProcessorQueueLength", 0, &vtProp, 0, 0);
			hr2 = pclsObj2->Get(L"CurrentDiskQueueLength", 0, &vtProp2, 0, 0);

			XTrace("Processor queue %d, Disk Queue %d\nhr = %d, hr2 = %d\n", vtProp.iVal, vtProp2.iVal, hr, hr2);

			Load_1 += vtProp.iVal + vtProp2.iVal;
			Load_5 += vtProp.iVal + vtProp2.iVal;
			Load_15 += vtProp.iVal + vtProp2.iVal;

			ss.str(""); ss.clear();

			ss << std::fixed << std::setprecision(2) << Load_1() << " " << Load_5() << " " << Load_15();

			// We set the GUI to this and redraw.
			SetDlgItemText(hwnd, 2, ss.str().c_str());
			InvalidateRect(hwnd, NULL, FALSE);
			UpdateWindow(hwnd);

			//printf("Loads: %.2f (1 min), %.2f (5 min), %.2f (15 min)\r", Load_1(), Load_5(), Load_15());

			VariantClear(&vtProp);
			VariantClear(&vtProp2);

			pclsObj->Release();
			pclsObj2->Release();
		}

		// Sleep for 5 seconds.
		SleepConditionVariableCS(&cvLoad, &LoadLock, 2 * 1000);
	}

	// Cleanup
	// ========

	pSvc->Release();
	pLoc->Release();
	pEnumerator->Release();
	CoUninitialize();
	return 0;
}

// WIndow procedure for handling window drawing and events
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lparam)
{
	static wchar_t *strbuf = (wchar_t*)malloc((1 << 16)), *sprintfbuf = (wchar_t*)malloc((1 << 16));
	switch (message)
	{
		case WM_CREATE:
			LOGFONT lf;
			HFONT hMyFont;

			memset(&lf, 0, sizeof(LOGFONT));
			lf.lfWeight = FW_NORMAL;
			lf.lfHeight = 15;
			strcpy_s(lf.lfFaceName, "Arial");
			hMyFont = CreateFontIndirect(&lf);

			// Draw the controls
			CreateWindow("STATIC", "Loads:", WS_VISIBLE | WS_CHILD | SS_CENTER, 5, 4, 50, 15, hwnd, (HMENU)1, NULL, NULL);
			CreateWindow("STATIC", "0.00 0.00 0.00", WS_VISIBLE | WS_CHILD | SS_CENTER, 48, 4, 100, 15, hwnd, (HMENU)2, NULL, NULL);


			// Create the control fonts since the windows normal ones suck

			for (int i = 1; i < 2; ++i)
				SendMessage(GetDlgItem(hwnd, i), WM_SETFONT, (WPARAM)hMyFont, 0);

			break;
		case WM_CTLCOLORSTATIC:
			// Set the colour of the text for our URL
			if ((HWND)lparam == GetDlgItem(hwnd, 5))
			{
				// we're about to draw the static
				// set the text colour in (HDC)lParam
				SetBkMode((HDC)wParam, TRANSPARENT);
				SetTextColor((HDC)wParam, CreateHexColor(0x939393));
				return (BOOL)CreateSolidBrush(GetSysColor(COLOR_MENU));
			}
			break;
		case VK_ESCAPE:
		case WM_DESTROY:
		case WM_CLOSE:
			free(sprintfbuf);
			free(strbuf);
			// Instruct the thread that we're quitting
			quitting = true;
			WakeConditionVariable(&cvLoad);
			// Wait for the thread to exit.
			WaitForSingleObject(LoadThread, INFINITE);
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hwnd, message, wParam, lparam);
	}
	return 0;
}


// Basically we need this query:
// wmic path Win32_PerfFormattedData_PerfOS_System get ProcessorQueueLength
// This gets the processor queue length.

int WINAPI WinMain(HINSTANCE hThisInstance, HINSTANCE hPrevInstance, LPSTR lpSzArgument, int nFunsterStil)
{
	HWND hwnd;
	MSG messages;
	WNDCLASSEX wincl;

	quitting = false;

	wincl.hInstance = hInst = hThisInstance;
	wincl.lpszClassName = szClassName;
	wincl.lpfnWndProc = WindowProcedure;
	wincl.style = CS_DBLCLKS | CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
	wincl.cbSize = sizeof(WNDCLASSEX);

	wincl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wincl.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wincl.hCursor = LoadCursor(NULL, IDC_ARROW);
	wincl.lpszMenuName = NULL;
	wincl.cbClsExtra = 0;
	wincl.cbWndExtra = 0;

	wincl.hbrBackground = CreateHexBrush(0xF0F0F0);

	if (!RegisterClassEx(&wincl))
		return 0;

	hwnd = CreateWindowEx(
		0, // Extended possibilities for variation
		szClassName, // Class name
		szWindowTitle,    // Window title
		(WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_EX_STATICEDGE | WS_EX_TOPMOST | WS_EX_WINDOWEDGE),
		CW_USEDEFAULT, // Window decides position
		CW_USEDEFAULT,  // where the window ends up on the screen
		200,  // Width
		60,  // Height
		HWND_DESKTOP, // The window is a child-window of desktop
		NULL, // No Menu
		hThisInstance,
		NULL // No Creation data
		);

	ShowWindow(hwnd, nFunsterStil);

	// Make the window always on top (because apparently WS_EX_TOPMOST doesn't work?
	SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);

	InitializeConditionVariable(&cvLoad);
	InitializeCriticalSection(&LoadLock);

	// Launch the Music thread
	LoadThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		LoadAverageCalculationThread,          // thread function name
		&hwnd,                   // argument to thread function 
		0,                      // use default creation flags 
		&ThreadID);             // returns the thread identifier 

	while (GetMessage(&messages, NULL, 0, 0))
	{
		TranslateMessage(&messages);
		DispatchMessage(&messages);
	}

	return messages.wParam;
}