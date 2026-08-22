// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/pty/pty_linux.cpp — PTY real con forkpty(3).
// Terminal integrada del IDE: sesión interactiva completa con ioctl resize.
//
#include "../registry.hpp"

#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace proc {

long PtyOpen(int* outId, const std::string& cmd, std::vector<std::string> args,
             const std::string& cwd, std::map<std::string, std::string> env,
             int cols, int rows, uint32_t windowId, std::string& err) {
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 80);
    ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 24);

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) { err = "forkpty falló"; return -1; }

    if (pid == 0) {
        if (!cwd.empty()) chdir(cwd.c_str());
        for (auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
        setenv("TERM", "xterm-256color", 1);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cmd.c_str()));
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(cmd.c_str(), argv.data());
        _exit(127);
    }

    fcntl(master, F_SETFD, FD_CLOEXEC);

    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    auto* p = new Proc();
    p->id = id;
    p->kind = Kind::Pty;
    p->pid = pid;
    p->masterFd = master;
    p->window_id = windowId;
    *outId = id;
    g_procs[id] = p;

    p->reader = std::thread([id, master, win = p->window_id, pid, stop = &p->stop] {
        char buf[65 * 1024];
        while (!stop->load()) {
            ssize_t n = ::read(master, buf, sizeof(buf));
            if (n > 0) {
                EmitData(win, id, "stdout", buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0 || errno != EINTR) break;
        }
        ::close(master);

        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
        Emit(win, "process.exit",
             "{\"procId\":" + std::to_string(id) + ",\"code\":" +
                 std::to_string(code) + ",\"signal\":" + std::to_string(sig) + "}");
        Remove(id);
    });

    return pid;
}

bool PtyResize(int id, int cols, int rows, std::string& err) {
    auto* p = Get(id);
    if (!p || p->masterFd < 0) { err = "pty inválido"; return false; }
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    if (ioctl(p->masterFd, TIOCSWINSZ, &ws) != 0) {
        err = "ioctl TIOCSWINSZ falló";
        return false;
    }
    // SIGHUP no; la mayoría de shells escuchan SIGWINCH vía el propio ioctl
    kill(p->pid, SIGWINCH);
    return true;
}

} // namespace proc
