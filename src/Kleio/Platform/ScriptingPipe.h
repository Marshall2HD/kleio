#pragma once

#include "Core/Scripting.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <thread>

namespace Kleio::ScriptingPipe
{
    // Named pipe that carries the scripting verbs (kleio --get/--select/--delete/--clear) from the
    // short-lived process started by the `kleio.exe` execution alias to the running instance and
    // returns the result, so the caller gets output and an exit code. AppInstance activation
    // redirection (used for --pause etc.) has no reply channel, hence the pipe.
    //
    // Name: \\.\pipe\Kleio.Scripting.<user SID>.<session id>; the default pipe DACL only lets the
    // same user (plus SYSTEM/administrators) write to it, and remote clients are rejected.
    // Protocol: frames of [uint32 little-endian length][bytes]. Request: one UTF-8 JSON frame
    // {"verb","number","format","yes"}. Response: a UTF-8 JSON frame {"exitCode","error","format"}
    // followed by a frame with the payload bytes.
    std::wstring Name();

    // Runs in the main instance. The handler runs on the thread that created the server (the UI
    // thread); the pipe I/O runs on a worker thread.
    class Server
    {
    public:
        using Handler = std::function<Scripting::Result(Scripting::Command const&)>;

        explicit Server(Handler handler);
        ~Server();
        Server(Server const&) = delete;
        Server& operator=(Server const&) = delete;

    private:
        void Run();
        void Serve(HANDLE pipe);
        Scripting::Result Dispatch(Scripting::Command const& command);

        Handler m_handler;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        wil::unique_event m_stop;
        std::atomic<bool> m_stopping{ false };
        std::thread m_thread;
    };

    // Runs in the alias process: sends `command` to the running instance (starting Kleio in the
    // background first if needed), prints the result and returns the process exit code.
    int RunClient(Scripting::Command const& command);

    // Prints `kleio --help` / an argument error (exit code UsageError) to the caller's console.
    int PrintUsage();
    int PrintUsageError(std::wstring const& error);
}
