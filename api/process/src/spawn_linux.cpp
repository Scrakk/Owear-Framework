// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/spawn_linux.cpp — fork/exec con pipes (Linux/Mac base).
//
#include "registry.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

namespace proc {

namespace {

void PipeReader(int id, int fd, uint32_t win, const char* which,
                const std::atomic<bool>& stop) {
    char buf[65 * 1024];
    while (!stop.load()) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            EmitData(win, id, which, buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || errno != EINTR) break;
    }
    ::close(fd);
}

} // namespace

long SpawnPipes(int* outId, const std::string& cmd, std::vector<std::string> args,
                const std::string& cwd, std::map<std::string, std::string> env,
                bool useShell, uint32_t windowId, std::string& err) {
    int inPipe[2], outPipe[2], errPipe[2];
    if (pipe(inPipe) || pipe(outPipe) || pipe(errPipe)) {
        err = "pipe() falló";
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) { err = "fork() falló"; return -1; }

    if (pid == 0) {
        dup2(inPipe[0], 0);  close(inPipe[0]); close(inPipe[1]);
        dup2(outPipe[1], 1); close(outPipe[0]); close(outPipe[1]);
        dup2(errPipe[1], 2); close(errPipe[0]); close(errPipe[1]);
        if (!cwd.empty()) chdir(cwd.c_str());
        for (auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
        if (!useShell) {
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(cmd.c_str()));
            for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            execvp(cmd.c_str(), argv.data());
        } else {
            std::string all = cmd;
            for (auto& a : args) all += " " + a;
            execl("/bin/sh", "sh", "-c", all.c_str(), static_cast<char*>(nullptr));
        }
        _exit(127);
    }

    close(inPipe[0]); close(outPipe[1]); close(errPipe[1]);

    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    auto* p = new Proc();
    p->id = id;
    p->pid = pid;
    p->stdinFd = inPipe[1];
    p->window_id = windowId;
    *outId = id;
    g_procs[id] = p;

    auto* stopFlag = &p->stop;
    p->reader = std::thread([id, fdO = outPipe[0], fdE = errPipe[0],
                             win = p->window_id, pid, stopFlag] {
        PipeReader(id, fdO, win, "stdout", *stopFlag);
        PipeReader(id, fdE, win, "stderr", *stopFlag);

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

bool WriteStdin(int id, const char* data, size_t len, std::string& err) {
    auto* p = Get(id);
    if (!p) { err = "proc inexistente"; return false; }
    if (p->kind == Kind::Pty) {
        if (p->masterFd < 0) { err = "pty cerrado"; return false; }
        size_t off = 0;
        while (off < len) {
            ssize_t n = ::write(p->masterFd, data + off, len - off);
            if (n <= 0) { err = "write pty falló"; return false; }
            off += static_cast<size_t>(n);
        }
        return true;
    }
    if (p->stdinFd < 0) { err = "proc sin stdin"; return false; }
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(p->stdinFd, data + off, len - off);
        if (n <= 0) { err = "write stdin falló"; return false; }
        off += static_cast<size_t>(n);
    }
    return true;
}

void CloseStdin(int id) {
    auto* p = Get(id);
    if (p && p->stdinFd >= 0) ::close(p->stdinFd), p->stdinFd = -1;
}

bool Kill(int id, int sig, std::string& err) {
    auto* p = Get(id);
    if (!p) { err = "proc inexistente"; return false; }
    if (::kill(p->pid, sig) != 0) { err = "kill falló"; return false; }
    return true;
}

std::vector<int> List() {
    std::lock_guard lock(g_mu);
    std::vector<int> ids;
    for (auto& [id, _] : g_procs) ids.push_back(id);
    return ids;
}

} // namespace proc
