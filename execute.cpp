// execute.cpp  — multiWatch + exec helpers
// (Drop-in; assumes TabState is defined elsewhere and tabs is extern)

#include <iostream>
#include <iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <cctype>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <err.h>
#include <string>
#include <chrono>
#include <vector>
#include <bits/stdc++.h>
#include <unistd.h>
#include <cctype>
#include <regex>
#include <sys/stat.h>
#include <limits.h>

#include "drawscreen.cpp"
using namespace std;

// -----------------------------
// Forward / external Declarations
// -----------------------------
// Your project defines TabState somewhere (drawscreen.cpp). It must include at least:
//   vector<string> screenBuffer;
//   string cwd;
//   string input;
//   int currCursorPos;
// etc.

extern vector<TabState> tabs;

// -----------------------------
// Globals used by execute.cpp
// -----------------------------
static mutex current_pids_mutex;
static vector<pid_t> current_child_pids; // guarded by current_pids_mutex

// Flags
atomic<bool> mw_stop_requested(false); // set by UI to request multiWatch stop
atomic<bool> mw_finished(false);       // set by multiWatch when it completed & restored
atomic<bool> cmd_running(false);       // true while execCommand or multiWatch is running

// Async-safe small flag for signal handler/UI -> execute communication
static volatile sig_atomic_t sigint_request_flag = 0;

// -----------------------------
// Utility
// -----------------------------
static string getCurrentTime()
{
    time_t now = time(nullptr);
    struct tm tmbuf;
    localtime_r(&now, &tmbuf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmbuf);
    return string(buf);
}

// -----------------------------
// notify_sigint_from_ui
// Called by UI (synchronously) when Ctrl+C is pressed.
// This function must be safe to call from signal handler context.
// -----------------------------
extern "C" void notify_sigint_from_ui()
{
    // Request stop of multiWatch and other loops
    mw_stop_requested.store(true);

    // Set the async flag to be handled by handle_pending_sigint()
    sigint_request_flag = 1;

    // Also proactively kill any recorded child pids (best-effort, non-blocking)
    {
        lock_guard<mutex> lk(current_pids_mutex);
        for (pid_t p : current_child_pids)
        {
            if (p > 0)
                kill(p, SIGINT);
        }
    }
}

// -----------------------------
// handle_pending_sigint
// Call periodically from loops that read pipes so we forward the request
// to the currently-recorded child processes.
// -----------------------------
static void handle_pending_sigint()
{
    if (sigint_request_flag == 0)
        return;
    // clear flag
    sigint_request_flag = 0;

    lock_guard<mutex> lk(current_pids_mutex);
    for (pid_t p : current_child_pids)
    {
        if (p > 0)
            kill(p, SIGINT);
    }
}
vector<string> execCommandInDir(const string &cmd, string &cwd_for_tab)
{
    if (cmd.empty())
        return {""};

    auto trim = [](string s)
    {
        s.erase(0, s.find_first_not_of(" \t"));
        if (!s.empty())
            s.erase(s.find_last_not_of(" \t") + 1);
        return s;
    };

    string trimmed = trim(cmd);

    // tab-local cd
    if (trimmed.rfind("cd ", 0) == 0)
    {
        string path = trim(trimmed.substr(3));
        string target;
        if (path.empty() || path == "~")
        {
            const char *home = getenv("HOME");
            target = home ? string(home) : string("/");
        }
        else if (path[0] == '/')
            target = path;
        else
            target = cwd_for_tab + "/" + path;

        char resolved[PATH_MAX];
        if (realpath(target.c_str(), resolved))
        {
            struct stat st{};
            if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode))
            {
                cwd_for_tab = resolved;
                return {""};
            }
        }
        return {string("ERROR: cd: no such file or directory: ") + path};
    }
    if (trimmed == "cd" || trimmed == "cd ~")
    {
        const char *home = getenv("HOME");
        cwd_for_tab = home ? string(home) : string("/");
        return {""};
    }

    // Pipeline split
    vector<string> parts;
    {
        stringstream ss(cmd);
        string p;
        while (getline(ss, p, '|'))
        {
            p.erase(0, p.find_first_not_of(" \t"));
            if (!p.empty())
                p.erase(p.find_last_not_of(" \t") + 1);
            if (!p.empty())
                parts.push_back(p);
        }
    }
    if (parts.empty())
        return {""};

    int n = (int)parts.size();
    int numPipes = max(0, n - 1);

    vector<int> chainFds(2 * numPipes, -1);
    for (int i = 0; i < numPipes; ++i)
    {
        if (pipe(chainFds.data() + i * 2) < 0)
        {
            for (int j = 0; j < i; ++j)
            {
                close(chainFds[j * 2]);
                close(chainFds[j * 2 + 1]);
            }
            return {"ERROR: pipe creation failed"};
        }
    }

    int capture_out[2] = {-1, -1}, capture_err[2] = {-1, -1};
    if (pipe(capture_out) < 0)
    {
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        return {"ERROR: capture_out pipe failed"};
    }
    if (pipe(capture_err) < 0)
    {
        close(capture_out[0]);
        close(capture_out[1]);
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        return {"ERROR: capture_err pipe failed"};
    }

    vector<pid_t> pids;
    bool forkError = false;

    cmd_running.store(true);
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
    }

    for (int i = 0; i < n; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            forkError = true;
            break;
        }
        if (pid == 0)
        {
            // Child: change cwd then exec
            if (!cwd_for_tab.empty())
                chdir(cwd_for_tab.c_str());

            if (i > 0)
                dup2(chainFds[(i - 1) * 2], STDIN_FILENO);
            if (i < numPipes)
                dup2(chainFds[i * 2 + 1], STDOUT_FILENO);
            else
                dup2(capture_out[1], STDOUT_FILENO);
            dup2(capture_err[1], STDERR_FILENO);

            for (int fd : chainFds)
                if (fd >= 0)
                    close(fd);
            close(capture_out[0]);
            close(capture_out[1]);
            close(capture_err[0]);
            close(capture_err[1]);

            execlp("bash", "bash", "-c", parts[i].c_str(), (char *)NULL);
            _exit(127);
        }
        else
            pids.push_back(pid);
    }

    if (forkError)
    {
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        close(capture_out[0]);
        close(capture_out[1]);
        close(capture_err[0]);
        close(capture_err[1]);
        for (pid_t p : pids)
            if (p > 0)
                waitpid(p, nullptr, 0);
        cmd_running.store(false);
        return {"ERROR: fork failed"};
    }

    for (int fd : chainFds)
        if (fd >= 0)
            close(fd);
    close(capture_out[1]);
    close(capture_err[1]);

    // record pids
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids = pids;
    }

    // read with poll (same as execCommand)
    string outBuf, errBuf;
    const int BUF_SZ = 4096;
    char buffer[BUF_SZ];
    struct pollfd pfds[2];
    pfds[0].fd = capture_out[0];
    pfds[0].events = POLLIN | POLLHUP | POLLERR;
    pfds[1].fd = capture_err[0];
    pfds[1].events = POLLIN | POLLHUP | POLLERR;

    int active = 2;
    while (active > 0)
    {
        handle_pending_sigint();

        int r = poll(pfds, 2, -1);
        if (r < 0)
        {
            if (errno == EINTR)
            {
                handle_pending_sigint();
                continue;
            }
            break;
        }

        for (int i = 0; i < 2; ++i)
        {
            if (pfds[i].fd < 0)
                continue;
            if (pfds[i].revents & POLLIN)
            {
                ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                if (n > 0)
                {
                    if (i == 0)
                        outBuf.append(buffer, n);
                    else
                        errBuf.append(buffer, n);
                }
                else
                {
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                }
            }
            else if (pfds[i].revents & (POLLHUP | POLLERR))
            {
                while (true)
                {
                    ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                    if (n > 0)
                    {
                        if (i == 0)
                            outBuf.append(buffer, n);
                        else
                            errBuf.append(buffer, n);
                    }
                    else
                        break;
                }
                if (pfds[i].fd >= 0)
                {
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                }
            }
        }
    }

    if (pfds[0].fd >= 0)
    {
        close(pfds[0].fd);
        pfds[0].fd = -1;
    }
    if (pfds[1].fd >= 0)
    {
        close(pfds[1].fd);
        pfds[1].fd = -1;
    }

    // wait children
    bool hadError = false;
    int lastStatus = 0;
    for (pid_t p : pids)
    {
        int status = 0;
        if (waitpid(p, &status, 0) < 0)
            hadError = true;
        else
        {
            lastStatus = status;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                hadError = true;
        }
    }

    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
    }
    cmd_running.store(false);

    if (!errBuf.empty())
        hadError = true;

    auto splitLines = [](const string &s) -> vector<string>
    {
        vector<string> out;
        size_t pos = 0;
        while (pos < s.size())
        {
            size_t nl = s.find('\n', pos);
            if (nl == string::npos)
            {
                out.push_back(s.substr(pos));
                break;
            }
            out.push_back(s.substr(pos, nl - pos));
            pos = nl + 1;
        }
        return out;
    };

    vector<string> outLines = splitLines(outBuf);
    vector<string> errLines = splitLines(errBuf);

    vector<string> result;
    if (hadError)
    {
        if (!errLines.empty())
            for (auto &l : errLines)
                result.push_back(string("ERROR: ") + l);
        else if (!outLines.empty())
            for (auto &l : outLines)
                result.push_back(string("ERROR: ") + l);
        else
        {
            int exitCode = (WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : -1);
            result.push_back(string("ERROR: (process exited with code ") + to_string(exitCode) + ")");
        }
    }
    else
    {
        result = std::move(outLines);
        if (result.empty())
            result.push_back("");
    }

    return result;
}
void multiWatchThreaded_using_pipes(const vector<string> &cmds, int tab_index, const vector<string> &oldBuffer)

{
    if (cmds.empty())
        return;
    if (tab_index < 0 || tab_index >= (int)tabs.size())
        return;

    TabState &T = tabs[tab_index];

    // Save old screen buffer for restore

    T.screenBuffer.clear();
    T.screenBuffer.push_back("multiWatch — starting...");
    mw_stop_requested.store(false);
    mw_finished.store(false);
    cmd_running.store(true);

    while (!mw_stop_requested.load())
    {
        vector<thread> workers;
        vector<pair<string, string>> results;
        mutex results_mtx;

        for (const auto &cmd : cmds)
        {
            workers.emplace_back([&, cmd]()
                                 {
                int pipefd[2];
                if (pipe(pipefd) < 0) return;

                pid_t pid = fork();
                if (pid == 0)
                {
                    // Child: redirect stdout/stderr to pipe
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    dup2(pipefd[1], STDERR_FILENO);
                    execlp("bash", "bash", "-c", cmd.c_str(), (char*)NULL);
                    _exit(127);
                }
                else if (pid > 0)
                {
                    // Parent
                    close(pipefd[1]);
                    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

                    // Register PID
                    {
                        lock_guard<mutex> lk(current_pids_mutex);
                        current_child_pids.push_back(pid);
                    }

                    string outBuf;
                    char buf[4096];
                    struct pollfd pfd{pipefd[0], POLLIN | POLLHUP | POLLERR, 0};
                    bool done = false;

                    while (!done && !mw_stop_requested.load())
                    {
                        handle_pending_sigint();

                        int r = poll(&pfd, 1, 200);
                        if (r > 0)
                        {
                            if (pfd.revents & POLLIN)
                            {
                                ssize_t n = read(pipefd[0], buf, sizeof(buf));
                                if (n > 0)
                                    outBuf.append(buf, n);
                                else if (n == 0)
                                    done = true;
                            }
                            else if (pfd.revents & (POLLHUP | POLLERR))
                            {
                                done = true;
                            }
                        }
                    }

                    // Stop or wait
                    int status = 0;
                    if (mw_stop_requested.load())
                    {
                        kill(pid, SIGINT);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        kill(pid, SIGKILL);
                    }
                    waitpid(pid, &status, 0);
                    close(pipefd[0]);

                    // Remove PID record
                    {
                        lock_guard<mutex> lk(current_pids_mutex);
                        auto it = std::find(current_child_pids.begin(), current_child_pids.end(), pid);
                        if (it != current_child_pids.end())
                            current_child_pids.erase(it);
                    }

                    // Save result
                    lock_guard<mutex> lk(results_mtx);
                    results.emplace_back(cmd, outBuf.empty() ? "(no output)\n" : outBuf);
                } });
        }

        for (auto &t : workers)
            if (t.joinable())
                t.join();

        // Update output like "watch"
        {
            T.screenBuffer.clear();
            T.screenBuffer.push_back("multiWatch — " + getCurrentTime() + " (Ctrl+C to stop)");
            T.screenBuffer.push_back("====================================================");

            for (auto &[cmd, out] : results)
            {
                T.screenBuffer.push_back("\"" + cmd + "\" output:");
                T.screenBuffer.push_back("----------------------------------------------------");
                std::stringstream ss(out);
                std::string line;
                while (std::getline(ss, line))
                    T.screenBuffer.push_back(line);
                T.screenBuffer.push_back("----------------------------------------------------");
            }
        }

        // Refresh every 2s (with frequent stop checks)
        for (int i = 0; i < 20 && !mw_stop_requested.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup any leftover child processes
    {
        lock_guard<mutex> lk(current_pids_mutex);
        for (pid_t p : current_child_pids)
            if (p > 0)
                kill(p, SIGINT);
        current_child_pids.clear();
    }

    // === Restore previous screen ===
    T.screenBuffer = oldBuffer;
    // T.screenBuffer.push_back("^C");

    std::string sdisp = formatPWD(T.cwd);
    // std::string prompt = (sdisp == "/")
    //                          ? ("swagnik@myterm:" + sdisp + "$ ")
    //                          : ("swagnik@myterm:~" + sdisp + "$ ");
    // T.screenBuffer.push_back(prompt);

    T.input.clear();
    T.currCursorPos = 0;

    // Reset state flags so the next command works
    cmd_running.store(false);
    mw_finished.store(true);
    mw_stop_requested.store(false);
    sigint_request_flag = 0; //
}

// -----------------------------
// execCommand
// Runs the given command line (supports simple '|' pipelines).
// Returns vector<string> of output lines (or error lines prefixed with "ERROR:").
// This is blocking and will set cmd_running while active.
// -----------------------------
vector<string> execCommand(const string &cmd)
{
    if (cmd.empty())
        return {""};

    // Trim helper
    auto trim_inplace = [](string &s)
    {
        s.erase(0, s.find_first_not_of(" \t"));
        if (!s.empty())
            s.erase(s.find_last_not_of(" \t") + 1);
    };

    string cmdcopy = cmd;
    trim_inplace(cmdcopy);
    if (cmdcopy.empty())
        return {""};

    // built-in cd handling
    if (cmdcopy.rfind("cd ", 0) == 0)
    {
        string path = cmdcopy.substr(3);
        trim_inplace(path);
        if (path.empty() || path == "~")
        {
            const char *home = getenv("HOME");
            if (home)
                chdir(home);
            return {""};
        }
        if (chdir(path.c_str()) != 0)
            return {string("ERROR: cd: no such file or directory: ") + path};
        return {""};
    }
    if (cmdcopy == "cd" || cmdcopy == "cd ~")
    {
        const char *home = getenv("HOME");
        if (home)
            chdir(home);
        return {""};
    }

    // Split pipeline by '|'
    vector<string> parts;
    {
        stringstream ss(cmd);
        string p;
        while (getline(ss, p, '|'))
        {
            trim_inplace(p);
            if (!p.empty())
                parts.push_back(p);
        }
    }
    if (parts.empty())
        return {""};

    int n = (int)parts.size();
    int numPipes = max(0, n - 1);

    // allocate pipes for chaining
    vector<int> chainFds(2 * numPipes, -1);
    for (int i = 0; i < numPipes; ++i)
    {
        if (pipe(chainFds.data() + i * 2) < 0)
        {
            // cleanup
            for (int j = 0; j < i; ++j)
            {
                close(chainFds[j * 2]);
                close(chainFds[j * 2 + 1]);
            }
            return {"ERROR: pipe creation failed"};
        }
    }

    int capture_out[2] = {-1, -1}, capture_err[2] = {-1, -1};
    if (pipe(capture_out) < 0)
    {
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        return {"ERROR: capture_out pipe failed"};
    }
    if (pipe(capture_err) < 0)
    {
        close(capture_out[0]);
        close(capture_out[1]);
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        return {"ERROR: capture_err pipe failed"};
    }

    vector<pid_t> pids;
    bool forkError = false;

    cmd_running.store(true);
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
    }

    for (int i = 0; i < n; ++i)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            forkError = true;
            break;
        }
        if (pid == 0)
        {
            // Child
            if (i > 0)
                dup2(chainFds[(i - 1) * 2], STDIN_FILENO);
            if (i < numPipes)
                dup2(chainFds[i * 2 + 1], STDOUT_FILENO);
            else
                dup2(capture_out[1], STDOUT_FILENO);

            // send stderr to capture_err
            dup2(capture_err[1], STDERR_FILENO);

            // close all fds inherited
            for (int fd : chainFds)
                if (fd >= 0)
                    close(fd);
            close(capture_out[0]);
            close(capture_out[1]);
            close(capture_err[0]);
            close(capture_err[1]);

            execlp("bash", "bash", "-c", parts[i].c_str(), (char *)NULL);
            _exit(127);
        }
        else
        {
            pids.push_back(pid);
        }
    }

    if (forkError)
    {
        for (int fd : chainFds)
            if (fd >= 0)
                close(fd);
        close(capture_out[0]);
        close(capture_out[1]);
        close(capture_err[0]);
        close(capture_err[1]);
        for (pid_t p : pids)
            if (p > 0)
                waitpid(p, nullptr, 0);
        cmd_running.store(false);
        return {"ERROR: fork failed"};
    }

    // parent closes all chain write-ends and read-ends that it doesn't need
    for (int fd : chainFds)
        if (fd >= 0)
            close(fd);
    close(capture_out[1]);
    close(capture_err[1]);

    // record pids for UI interrupts
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids = pids;
    }

    // read both pipes
    string outBuf, errBuf;
    const int BUF_SZ = 4096;
    char buffer[BUF_SZ];
    struct pollfd pfds[2];
    pfds[0].fd = capture_out[0];
    pfds[0].events = POLLIN | POLLHUP | POLLERR;
    pfds[1].fd = capture_err[0];
    pfds[1].events = POLLIN | POLLHUP | POLLERR;

    int active = 2;
    while (active > 0)
    {
        handle_pending_sigint();

        int r = poll(pfds, 2, -1);
        if (r < 0)
        {
            if (errno == EINTR)
            {
                handle_pending_sigint();
                continue;
            }
            break;
        }
        for (int i = 0; i < 2; ++i)
        {
            if (pfds[i].fd < 0)
                continue;
            if (pfds[i].revents & POLLIN)
            {
                ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                if (n > 0)
                {
                    if (i == 0)
                        outBuf.append(buffer, n);
                    else
                        errBuf.append(buffer, n);
                }
                else
                {
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                }
            }
            else if (pfds[i].revents & (POLLHUP | POLLERR))
            {
                // drain
                while (true)
                {
                    ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                    if (n > 0)
                    {
                        if (i == 0)
                            outBuf.append(buffer, n);
                        else
                            errBuf.append(buffer, n);
                    }
                    else
                        break;
                }
                if (pfds[i].fd >= 0)
                {
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                }
            }
        }
    }

    if (pfds[0].fd >= 0)
    {
        close(pfds[0].fd);
        pfds[0].fd = -1;
    }
    if (pfds[1].fd >= 0)
    {
        close(pfds[1].fd);
        pfds[1].fd = -1;
    }

    // wait children
    bool hadError = false;
    int lastStatus = 0;
    for (pid_t p : pids)
    {
        int status = 0;
        if (waitpid(p, &status, 0) < 0)
        {
            hadError = true;
        }
        else
        {
            lastStatus = status;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                hadError = true;
        }
    }

    // clear recorded pids
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
    }
    cmd_running.store(false);

    // treat stderr content as error
    if (!errBuf.empty())
        hadError = true;

    // split lines helper
    auto splitLines = [](const string &s) -> vector<string>
    {
        vector<string> out;
        size_t pos = 0;
        while (pos < s.size())
        {
            size_t nl = s.find('\n', pos);
            if (nl == string::npos)
            {
                out.push_back(s.substr(pos));
                break;
            }
            out.push_back(s.substr(pos, nl - pos));
            pos = nl + 1;
        }
        return out;
    };

    vector<string> outLines = splitLines(outBuf);
    vector<string> errLines = splitLines(errBuf);

    vector<string> result;
    if (hadError)
    {
        if (!errLines.empty())
        {
            for (auto &l : errLines)
                result.push_back(string("ERROR: ") + l);
        }
        else if (!outLines.empty())
        {
            for (auto &l : outLines)
                result.push_back(string("ERROR: ") + l);
        }
        else
        {
            int exitCode = (WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : -1);
            result.push_back(string("ERROR: (process exited with code ") + to_string(exitCode) + ")");
        }
    }
    else
    {
        result = std::move(outLines);
        if (result.empty())
            result.push_back("");
    }

    return result;
}

// -----------------------------
// execCommandInDir — like execCommand but runs in provided cwd (per-tab)
// -----------------------------

// -----------------------------
// multiWatchThreaded_using_pipes
// - Does NOT draw. It updates tabs[tab_index].screenBuffer and restores it on exit.
// - Stops quickly when mw_stop_requested is set or notify_sigint_from_ui() is called.
// - Sets mw_finished = true when done.
// -----------------------------
