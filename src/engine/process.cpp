#include "engine/process.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace chess {

struct Process::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> lines;
    bool eof = false;
    std::thread reader;

#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;

    void readerLoop() {
        std::string buf;
        char chunk[4096];
        DWORD n = 0;
        while (ReadFile(stdoutRead, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
            buf.append(chunk, n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                {
                    std::lock_guard<std::mutex> lk(mutex);
                    lines.push_back(std::move(line));
                }
                cv.notify_one();
            }
        }
        {
            std::lock_guard<std::mutex> lk(mutex);
            if (!buf.empty()) lines.push_back(buf);
            eof = true;
        }
        cv.notify_all();
    }
#endif
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

Process::~Process() { stop(std::chrono::milliseconds(500)); }

#ifdef _WIN32

bool Process::start(const std::filesystem::path& executable, std::string& error) {
    if (impl_->process) {
        error = "Prozess läuft bereits";
        return false;
    }
    if (!std::filesystem::exists(executable)) {
        error = "Datei nicht gefunden: " + executable.string();
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdinRead = nullptr, childStdoutWrite = nullptr;
    if (!CreatePipe(&childStdinRead, &impl_->stdinWrite, &sa, 0) ||
        !CreatePipe(&impl_->stdoutRead, &childStdoutWrite, &sa, 0)) {
        error = "Pipes konnten nicht erstellt werden";
        return false;
    }
    // Unsere Enden duerfen nicht ans Kind vererbt werden
    SetHandleInformation(impl_->stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->stdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError = childStdoutWrite;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + executable.wstring() + L"\"";
    const std::wstring workdir = executable.parent_path().wstring();
    const BOOL ok = CreateProcessW(executable.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, workdir.empty() ? nullptr : workdir.c_str(), &si, &pi);
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);
    if (!ok) {
        error = "Programm konnte nicht gestartet werden (Fehler " + std::to_string(GetLastError()) + ")";
        CloseHandle(impl_->stdinWrite);
        CloseHandle(impl_->stdoutRead);
        impl_->stdinWrite = impl_->stdoutRead = nullptr;
        return false;
    }
    CloseHandle(pi.hThread);
    impl_->process = pi.hProcess;
    impl_->eof = false;
    impl_->reader = std::thread([this] { impl_->readerLoop(); });
    return true;
}

bool Process::writeLine(const std::string& line) {
    if (!impl_->stdinWrite) return false;
    const std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(impl_->stdinWrite, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
           written == data.size();
}

void Process::stop(std::chrono::milliseconds grace) {
    if (!impl_->process) return;
    if (impl_->stdinWrite) {
        CloseHandle(impl_->stdinWrite);
        impl_->stdinWrite = nullptr;
    }
    if (WaitForSingleObject(impl_->process, static_cast<DWORD>(grace.count())) == WAIT_TIMEOUT)
        TerminateProcess(impl_->process, 1);
    WaitForSingleObject(impl_->process, 2000);
    if (impl_->reader.joinable()) impl_->reader.join();
    CloseHandle(impl_->stdoutRead);
    CloseHandle(impl_->process);
    impl_->stdoutRead = nullptr;
    impl_->process = nullptr;
}

#else

bool Process::start(const std::filesystem::path&, std::string& error) {
    error = "Externe Engines werden bisher nur unter Windows unterstützt";
    return false;
}
bool Process::writeLine(const std::string&) { return false; }
void Process::stop(std::chrono::milliseconds) {}

#endif

std::optional<std::string> Process::readLine(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(impl_->mutex);
    impl_->cv.wait_for(lk, timeout, [&] { return !impl_->lines.empty() || impl_->eof; });
    if (impl_->lines.empty()) return std::nullopt;
    std::string line = std::move(impl_->lines.front());
    impl_->lines.pop_front();
    return line;
}

bool Process::finished() const {
    std::lock_guard<std::mutex> lk(impl_->mutex);
    return impl_->eof && impl_->lines.empty();
}

}  // namespace chess
