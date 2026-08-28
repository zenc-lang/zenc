// SPDX-License-Identifier: MIT

#include "os.h"
#include "../zprep.h"
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
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

#define MAX_PATH_SIZE 1024

#if !ZC_OS_WINDOWS
ZEN_CONST
#endif
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
#ifdef __COSMOPOLITAN__
    const char *exe = GetProgramExecutableName();
    if (exe)
    {
        strncpy(buffer, exe, size - 1);
    }
#elif ZC_OS_WINDOWS
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
    char *last_slash = (char *)strrchr(buffer, '/');
    char *last_bslash = (char *)strrchr(buffer, '\\');
    char *last_sep = last_slash > last_bslash ? last_slash : last_bslash;
    if (last_sep)
    {
        *last_sep = '\0';
    }
}

void z_get_absolute_path(const char *path, char *buffer, size_t size)
{
    if (!path || !path[0])
    {
        memset(buffer, 0, size);
        return;
    }

#if ZC_OS_WINDOWS
    _fullpath(buffer, path, (int)size);
    // Convert backslashes to forward slashes for consistency
    for (char *p = buffer; *p; p++)
    {
        if (*p == '\\')
        {
            *p = '/';
        }
    }
#else
    char *real = realpath(path, NULL);
    if (real)
    {
#ifdef __COSMOPOLITAN__
        // If we are in APE and got a /zip path, but we need to pass this to a sub-process (like
        // cc), we MUST use a physical path because sub-processes can't see into our zip.
        if (strncmp(real, "/zip", 4) == 0)
        {
            // Try to see if the path exists on the physical disk relative to the executable.
            const char *exe_path = GetProgramExecutableName();
            if (exe_path)
            {
                char exe_dir[MAX_PATH_SIZE];
                strncpy(exe_dir, exe_path, sizeof(exe_dir) - 1);
                char *last_slash = (char *)strrchr(exe_dir, '/');
                if (last_slash)
                {
                    *last_slash = '\0';
                    // We heuristicly assume that if we are in APE and got a /zip path,
                    // we want the equivalent path in the directory where the APE resides.
                    // This is true for the Zen C repository structure.
                    char physical[2 * MAX_PATH_SIZE];
                    if (strlen(real) >= 5)
                    {
                        snprintf(physical, sizeof(physical), "%s/%s", exe_dir,
                                 real + 5); // real+5 skips "/zip/"
                        if (access(physical, F_OK) == 0)
                        {
                            strncpy(buffer, physical, size - 1);
                            buffer[size - 1] = '\0';
                            libc_free(real);
                            return;
                        }
                    }

                    // Fallback: if it was just "/zip", then it's exe_dir.
                    if (strcmp(real, "/zip") == 0)
                    {
                        strncpy(buffer, exe_dir, size - 1);
                        buffer[size - 1] = '\0';
                        libc_free(real);
                        return;
                    }
                }
            }
        }
#endif
        strncpy(buffer, real, size - 1);
        buffer[size - 1] = '\0';
        libc_free(real);
    }
    else
    {
        // realpath failed (e.g. path doesn't exist yet or /zip path on host)
        // Just copy it as-is
        strncpy(buffer, path, size - 1);
        buffer[size - 1] = '\0';
    }
#endif
}

int z_isatty(int fd)
{
#if ZC_OS_WINDOWS
    return _isatty(fd);
#else
    return isatty(fd);
#endif
}

ZEN_CONST const char *z_get_system_name(void)
{
#if ZC_OS_WINDOWS
    return "windows";
#elif ZC_OS_MACOS
    return "macos";
#else
    return "linux";
#endif
}

int z_path_match_compiler(const char *path, const char *compiler_name)
{
    if (!path || !compiler_name)
    {
        return 0;
    }

    // Handle "zig cc" and other space-separated command strings
    // We check if the compiler name exists as a distinct word in the path/command.
    const char *p = path;
    size_t name_len = strlen(compiler_name);

    while ((p = strstr(p, compiler_name)) != NULL)
    {
        // Verify it's a "whole word" match or at least at a boundary
        // Start boundary: beginning of string, or space, or slash
        int start_ok =
            (p == path || isspace((unsigned char)p[-1]) || p[-1] == '/' || p[-1] == '\\');

        // End boundary: end of string, or space, or '.' (for extensions like .exe)
        int end_ok = (p[name_len] == '\0' || isspace((unsigned char)p[name_len]) ||
                      p[name_len] == '.' || p[name_len] == '-' || p[name_len] == '_');

        if (start_ok && end_ok)
        {
            return 1;
        }
        p += name_len;
    }

    return 0;
}

int z_path_has_extension(const char *path, const char *ext)
{
    if (!path || !ext)
    {
        return 0;
    }

    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);

    if (path_len < ext_len)
    {
        return 0;
    }

    return strcmp(path + path_len - ext_len, ext) == 0;
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
        return xstrdup(arg); // use xstrdup since we free it later directly, or xxstrdup
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
        zfree(q);
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
        zfree(q);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        DWORD err = GetLastError();
        LPSTR msg_buf = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg_buf, 0,
                       NULL);
        fprintf(stderr, "error: CreateProcess failed (code %lu): %s\n", (unsigned long)err,
                msg_buf ? msg_buf : "unknown error");
        fprintf(stderr, "  command: %s\n", cmd_line);
        if (msg_buf)
        {
            LocalFree(msg_buf);
        }
        zfree(cmd_line);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    zfree(cmd_line);
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
    return -1;
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
        zfree(q);
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
        zfree(q);
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
        zfree(cmd_line);
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
    zfree(cmd_line);
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
    return -1;
}
