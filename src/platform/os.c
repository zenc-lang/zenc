
#include "os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if ZC_OS_WINDOWS
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#if ZC_OS_MACOS
#include <mach-o/dyld.h>
#endif
#endif

void z_setup_terminal(void)
{
#if ZC_OS_WINDOWS
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode))
    {
        return;
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr == INVALID_HANDLE_VALUE)
    {
        return;
    }
    if (!GetConsoleMode(hErr, &dwMode))
    {
        return;
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hErr, dwMode);
#endif
}

double z_get_monotonic_time(void)
{
#if ZC_OS_WINDOWS
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init)
    {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

double z_get_time(void)
{
#if ZC_OS_WINDOWS
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const unsigned __int64 EPOCH_DIFF = 116444736000000000ULL;
    return (double)(uli.QuadPart - EPOCH_DIFF) / 10000000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

#define MAX_PATH_SIZE 1024

const char *z_get_temp_dir(void)
{
#if ZC_OS_WINDOWS
    static char tmp[MAX_PATH_SIZE] = {0};
    if (tmp[0])
    {
        return tmp;
    }

    DWORD len = GetTempPathA(sizeof(tmp), tmp);
    if (len > 0 && len < sizeof(tmp))
    {
        for (DWORD i = 0; i < len; i++)
        {
            if (tmp[i] == '\\')
            {
                tmp[i] = '/';
            }
        }

        if (len > 0 && tmp[len - 1] == '/')
        {
            tmp[len - 1] = 0;
        }
        return tmp;
    }
    return "C:/Windows/Temp";
#else
    return "/tmp";
#endif
}

int z_get_pid(void)
{
#if ZC_OS_WINDOWS
    return _getpid();
#else
    return getpid();
#endif
}

void z_get_executable_path(char *buffer, size_t size)
{
    memset(buffer, 0, size);
#if ZC_OS_WINDOWS
    GetModuleFileNameA(NULL, buffer, (DWORD)size);
#elif ZC_OS_LINUX
    ssize_t len = readlink("/proc/self/exe", buffer, size - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
    }
#elif ZC_OS_MACOS
    uint32_t buf_size = (uint32_t)size;
    if (_NSGetExecutablePath(buffer, &buf_size) != 0)
    {
        // buffer was too small? or other error
        memset(buffer, 0, size);
    }
#else
    // Fallback
#endif

    // Strip the executable filename to get the directory
    char *last_slash = strrchr(buffer, '/');
    char *last_bslash = strrchr(buffer, '\\');
    char *last_sep = last_slash > last_bslash ? last_slash : last_bslash;
    if (last_sep)
    {
        *last_sep = '\0';
    }
}

int z_isatty(int fd)
{
#if ZC_OS_WINDOWS
    return _isatty(fd);
#else
    return isatty(fd);
#endif
}

int z_match_os(const char *os_name)
{
    if (!os_name)
    {
        return 0;
    }

    if (0 == strcmp(os_name, "linux"))
    {
#if ZC_OS_LINUX
        return 1;
#else
        return 0;
#endif
    }
    else if (0 == strcmp(os_name, "windows"))
    {
#if ZC_OS_WINDOWS
        return 1;
#else
        return 0;
#endif
    }
    else if (0 == strcmp(os_name, "macos") || 0 == strcmp(os_name, "darwin"))
    {
#if ZC_OS_MACOS
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

const char *z_get_system_name(void)
{
#if ZC_OS_WINDOWS
    return "windows";
#elif ZC_OS_MACOS
    return "macos";
#else
    return "linux";
#endif
}

FILE *z_tmpfile(void)
{
#if ZC_OS_WINDOWS
    char temp_path[MAX_PATH_SIZE];
    char temp_file[MAX_PATH_SIZE];

    if (!GetTempPathA(MAX_PATH_SIZE, temp_path))
    {
        return NULL;
    }

    if (!GetTempFileNameA(temp_path, "zc", 0, temp_file))
    {
        return NULL;
    }

    HANDLE h = CreateFileA(temp_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);

    if (h == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    int fd = _open_osfhandle((intptr_t)h, 0);
    if (fd == -1)
    {
        CloseHandle(h);
        return NULL;
    }

    FILE *f = _fdopen(fd, "w+b");
    if (!f)
    {
        _close(fd);
        return NULL;
    }
    return f;
#else
    return tmpfile();
#endif
}

#if ZC_OS_WINDOWS
static char *quote_arg(const char *arg)
{
    if (!strpbrk(arg, " \t\n\v\""))
    {
        return strdup(arg); // use strdup since we free it later directly, or xstrdup
    }

    size_t len = strlen(arg);
    char *result = malloc(len * 2 + 3);
    char *p = result;
    *p++ = '\"';

    for (size_t i = 0; i < len;)
    {
        int num_backslashes = 0;
        while (i < len && arg[i] == '\\')
        {
            num_backslashes++;
            i++;
        }

        if (i == len)
        {
            for (int k = 0; k < num_backslashes * 2; k++)
            {
                *p++ = '\\';
            }
            break;
        }
        else if (arg[i] == '\"')
        {
            for (int k = 0; k < num_backslashes * 2 + 1; k++)
            {
                *p++ = '\\';
            }
            *p++ = '\"';
            i++;
        }
        else
        {
            for (int k = 0; k < num_backslashes; k++)
            {
                *p++ = '\\';
            }
            *p++ = arg[i];
            i++;
        }
    }

    *p++ = '\"';
    *p = '\0';
    return result;
}
#endif

int z_run_command(char *const argv[])
{
#if ZC_OS_WINDOWS
    size_t cmd_len = 0;
    for (int i = 0; argv[i]; i++)
    {
        char *q = quote_arg(argv[i]);
        cmd_len += strlen(q) + 1;
        free(q);
    }

    char *cmd_line = malloc(cmd_len + 1);
    cmd_line[0] = '\0';
    for (int i = 0; argv[i]; i++)
    {
        char *q = quote_arg(argv[i]);
        strcat(cmd_line, q);
        if (argv[i + 1])
        {
            strcat(cmd_line, " ");
        }
        free(q);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        free(cmd_line);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(cmd_line);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid == 0)
    {
        execvp(argv[0], argv);
        exit(127);
    }
    else if (pid < 0)
    {
        return -1;
    }
    else
    {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        return -1;
    }
#endif
}

#if !ZC_OS_WINDOWS
#include <sys/wait.h>
#endif

int z_run_command_capture(char *const argv[], char *buffer, size_t size)
{
#if ZC_OS_WINDOWS
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        return -1;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    size_t cmd_len = 0;
    for (int i = 0; argv[i]; i++)
    {
        char *q = quote_arg(argv[i]);
        cmd_len += strlen(q) + 1;
        free(q);
    }

    char *cmd_line = malloc(cmd_len + 1);
    cmd_line[0] = '\0';
    for (int i = 0; argv[i]; i++)
    {
        char *q = quote_arg(argv[i]);
        strcat(cmd_line, q);
        if (argv[i + 1])
        {
            strcat(cmd_line, " ");
        }
        free(q);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        free(cmd_line);
        return -1;
    }

    CloseHandle(hWritePipe);

    DWORD bytesRead;
    if (ReadFile(hReadPipe, buffer, (DWORD)size - 1, &bytesRead, NULL))
    {
        buffer[bytesRead] = '\0';
    }
    else
    {
        buffer[0] = '\0';
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(cmd_line);
    return (int)exit_code;
#else
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        exit(127);
    }
    else if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    else
    {
        close(pipefd[1]);
        ssize_t n = read(pipefd[0], buffer, size - 1);
        if (n >= 0)
        {
            buffer[n] = '\0';
        }
        else
        {
            buffer[0] = '\0';
        }
        close(pipefd[0]);

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        return -1;
    }
#endif
}
