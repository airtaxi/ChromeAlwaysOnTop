// Keeps every visible chrome.exe window topmost.
// Usage: ChromeAlwaysOnTop.exe [interval in milliseconds]
// Each start registers the program, with its arguments, to run at sign-in.
// Running it again while an instance is active stops that instance and keeps the registration.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define DEFAULT_INTERVAL_MS 1000
#define MINIMUM_INTERVAL_MS 100
#define MAXIMUM_INTERVAL_MS 3600000
#define IMAGE_PATH_CAPACITY 1024
#define COMMAND_CAPACITY 1536

static const WCHAR ApplicationName[] = L"ChromeAlwaysOnTop";
static const WCHAR WindowClassName[] = L"ChromeAlwaysOnTopHiddenWindow";

typedef struct ScanState
{
    DWORD lastProcessId;
    BOOL lastProcessIsChrome;
} ScanState;

static BOOL IsChromeProcess(DWORD processId)
{
    WCHAR imagePath[IMAGE_PATH_CAPACITY];
    DWORD length = IMAGE_PATH_CAPACITY;
    BOOL isChrome = FALSE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

    if (process == NULL) return FALSE;

    if (QueryFullProcessImageNameW(process, 0, imagePath, &length))
    {
        const WCHAR* fileName = imagePath + length;
        while (fileName > imagePath && fileName[-1] != L'\\') fileName--;
        isChrome = lstrcmpiW(fileName, L"chrome.exe") == 0;
    }

    CloseHandle(process);
    return isChrome;
}

static BOOL CALLBACK PinChromeWindow(HWND window, LPARAM parameter)
{
    ScanState* state = (ScanState*)parameter;
    DWORD processId = 0;

    if (!IsWindowVisible(window) || (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;

    // Windows of the same process are usually enumerated back to back, so reuse the last lookup.
    GetWindowThreadProcessId(window, &processId);
    if (processId != state->lastProcessId)
    {
        state->lastProcessId = processId;
        state->lastProcessIsChrome = IsChromeProcess(processId);
    }

    // SWP_ASYNCWINDOWPOS keeps this loop from blocking when a Chrome window is not responding.
    if (state->lastProcessIsChrome) SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);

    return TRUE;
}

static void PinChromeWindows(void)
{
    ScanState state = { 0, FALSE };
    EnumWindows(PinChromeWindow, (LPARAM)&state);
}

static const WCHAR* GetArguments(void)
{
    const WCHAR* cursor = GetCommandLineW();

    // Skip the program path, which may be quoted.
    if (*cursor == L'"')
    {
        cursor++;
        while (*cursor && *cursor != L'"') cursor++;
        if (*cursor) cursor++;
    }
    else
    {
        while (*cursor > L' ') cursor++;
    }
    while (*cursor == L' ' || *cursor == L'\t') cursor++;

    return cursor;
}

static void RegisterStartup(const WCHAR* arguments)
{
    WCHAR command[COMMAND_CAPACITY];
    DWORD length = GetModuleFileNameW(NULL, command + 1, IMAGE_PATH_CAPACITY);

    if (length == 0 || length >= IMAGE_PATH_CAPACITY) return;

    command[0] = L'"';
    length++;
    command[length++] = L'"';
    if (*arguments)
    {
        command[length++] = L' ';
        while (*arguments && length < COMMAND_CAPACITY - 1) command[length++] = *arguments++;
    }
    command[length++] = L'\0';

    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", ApplicationName, REG_SZ, command, length * sizeof(WCHAR));
}

static DWORD ReadInterval(const WCHAR* cursor)
{
    DWORD interval = 0;

    while (*cursor >= L'0' && *cursor <= L'9' && interval <= MAXIMUM_INTERVAL_MS) interval = interval * 10 + (DWORD)(*cursor++ - L'0');

    if (interval == 0) return DEFAULT_INTERVAL_MS;
    if (interval < MINIMUM_INTERVAL_MS) return MINIMUM_INTERVAL_MS;
    if (interval > MAXIMUM_INTERVAL_MS) return MAXIMUM_INTERVAL_MS;
    return interval;
}

static LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_TIMER)
    {
        PinChromeWindows();
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
{
    // Unprefixed names live in the session namespace, the same as a "Local\" prefix.
    HANDLE exitEvent = CreateEventW(NULL, FALSE, FALSE, ApplicationName);
    WNDCLASSW windowClass = { 0 };
    const WCHAR* arguments;
    DWORD interval;
    HWND window;
    MSG message;

    if (exitEvent == NULL) return 1;

    // The event already exists when another instance is running: signal it to stop and leave.
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        SetEvent(exitEvent);
        CloseHandle(exitEvent);
        return 0;
    }

    arguments = GetArguments();
    RegisterStartup(arguments);
    interval = ReadInterval(arguments);

    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassW(&windowClass)) return 1;

    // The window is never shown; it only receives timer ticks and the close request.
    window = CreateWindowExW(0, WindowClassName, ApplicationName, 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (window == NULL) return 1;

    PinChromeWindows();
    if (SetTimer(window, 1, interval, NULL) == 0) return 1;

    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        DispatchMessageW(&message);
    }

    CloseHandle(exitEvent);
    return 0;
}
