# Sentrits Architecture Details

Generated from codebase analysis (April 2026, v0.2.2).

---

## 1. Top-Level Process & Communication Model

```
+-----------------------------------------------------------------------------+
|                         sentrits (daemon process)                           |
|                                                                             |
|  +--------------+   +-------------------+   +--------------------------+    |
|  | Signal Thread |   |  ASIO io_context  |   |  Session Pump (timer)    |   |
|  |  (SIGHUP/     |   |  (single-threaded)|   |  polls PTYs -> pushes    |   |
|  |  SIGINT/TERM) |   |  all async I/O    |   |  output to WebSockets    |   |
|  +--------------+   +--------+----------+   +--------------------------+    |
|                               |                                             |
|          +--------------------+--------------------+                        |
|          v                    v                     v                       |
|  +-----------------+ +------------------+ +----------------------------+    |
|  |  AdminListener  | |  RemoteListener  | |  LocalControllerListener   |    |
|  |  127.0.0.1:18085| |  0.0.0.0:18080   | |  ~/.sentrits/controller.   |    |
|  |  (TCP, plain)   | |  (TCP, opt. TLS) | |  sock  (Unix domain socket)|    |
|  +--------+--------+ +--------+---------+ +-------------+--------------+    |
|           |                   |                          |                  |
|    REST+WebSocket       REST+WebSocket          Binary framed protocol      |
+-----------|-------------------|--------------------------|------------------|
            |                   |                          |
            v                   v                          v
     +-------------+    +--------------+         +---------------------+
     | sentrits CLI|    | Web Frontend |         |  sentrits attach    |
     | (admin cmds)|    | (browser)    |         |  (raw terminal CLI) |
     +-------------+    +--------------+         +---------------------+
```

---

## 2. Runtime Layers (inside the daemon)

```
+--------------------------------------------------------------------------+
|  NETWORK LAYER  (src/net/http_server.cpp)                                |
|                                                                          |
|  HttpListener<tcp::socket>          HttpsListener<ssl::stream>           |
|  +-- HttpSession (per conn)         +-- HttpSession (per conn)           |
|  |   +-- WebSocketSession           |   +-- OverviewWebSocketSession     |
|  |   +-- OverviewWebSocketSession   |                                    |
|  +-- LocalControllerSession (UDS)                                        |
|                                                                          |
|  WebSocket Registries:                                                   |
|    session_ws_registry[session_id] -> [weak_ptr<WebSocketSession>, ...]  |
|    overview_ws_registry            -> [weak_ptr<OverviewWsSession>, ...] |
+--------------------------------------------------------------------------+
|  SERVICE LAYER  (src/service/session_manager.cpp)                        |
|                                                                          |
|  SessionManager                                                          |
|  +-- sessions_: vector<SessionEntry>                                     |
|       +-- id: SessionId                                                  |
|       +-- process: unique_ptr<IPtyProcess>     <- owns PTY               |
|       +-- runtime: unique_ptr<SessionRuntime>  <- polls + buffers        |
|       +-- git_inspector: unique_ptr<GitInspector>                        |
|       +-- file_watcher: unique_ptr<WorkspaceFileWatcher>                 |
|       +-- controller_client_id / controller_kind (Host|Remote|None)      |
+--------------------------------------------------------------------------+
|  SESSION LAYER  (src/session/session_runtime.cpp)                        |
|                                                                          |
|  SessionRuntime                                                          |
|  +-- Owns: SessionOutputBuffer (circular, 64KB-8MB, seq-numbered)        |
|  +-- Owns: TerminalMultiplexer -> libvterm (screen state, ANSI rendering)|
|  +-- PollOutput() -> reads from IPtyProcess -> feeds buffer + multiplexer|
+--------------------------------------------------------------------------+
|  PTY LAYER  (src/session/posix_pty_process.cpp)                          |
|                                                                          |
|  PosixPtyProcess (implements IPtyProcess)                                |
|  +-- master_fd_: int  (PTY master, R/W for daemon)                       |
|  +-- pid_: int        (child process PID)                                |
|  +-- Read(timeout_ms) -> select() + read(master_fd_)                     |
|  +-- Write(data)      -> write(master_fd_, ...)                          |
|  +-- Resize(cols,rows)-> ioctl(master_fd_, TIOCSWINSZ)                   |
|  +-- PollExit()       -> waitpid(pid_, WNOHANG)                          |
+--------------------------------------------------------------------------+
```

---

## 3. PTY Session Spawning (forkpty path)

```
SessionManager::CreateSession(req)
        |
        v
PosixPtyProcess::Start(launch_spec)
        |
        +--- forkpty(&master_fd_, nullptr, nullptr, &winsize)
        |         |
        |    +----+--------------------------------------+
        |    | PARENT (daemon)                           |
        |    |  master_fd_ = PTY master fd (R/W)         |
        |    |  pid_ = child PID                         |
        |    |  Sets FD_CLOEXEC on error pipe            |
        |    |  Returns to SessionRuntime polling        |
        |    +-------------------------------------------+
        |         |
        |    +----+--------------------------------------+
        |    | CHILD process                             |
        |    |  stdin/stdout/stderr -> PTY slave fd      |
        |    |  chdir(working_directory)                 |
        |    |  setenv() each env override               |
        |    |  execvpe(executable, argv, envp)          |
        |    +-------------------------------------------+
        |
        +--- Parent stores master_fd_, pid_ in PosixPtyProcess

Session output flow:
  child writes to stdout
    -> PTY slave (kernel line discipline)
    -> PTY master (daemon reads via master_fd_)
    -> SessionRuntime::PollOutput()
    -> SessionOutputBuffer (circular, seq-numbered)
    -> TerminalMultiplexer (libvterm screen update)
    -> WebSocket broadcast to all attached clients

Session input flow:
  WebSocket client sends input bytes
    -> SessionManager::SendInput(id, data)
    -> PosixPtyProcess::Write(data) -> write(master_fd_, ...)
    -> PTY slave -> child's stdin
```

---

## 4. Session Launch Modes

```
+----------------------------------------------------------------------+
|                     LaunchSpec variants                              |
|                                                                      |
|  Mode A: Interactive Shell                                           |
|  +----------------------------------------------------------------+  |
|  |  executable = "/bin/bash" (or $SHELL)                          |  |
|  |  arguments  = []    (no -c flag)                               |  |
|  |  -- forkpty -> bash running with full PTY/job control          |  |
|  |  -- user can run commands interactively                        |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  Mode B: Shell One-shot (command string via shell)                   |
|  +----------------------------------------------------------------+  |
|  |  executable = "/bin/bash"                                      |  |
|  |  arguments  = ["-c", "user_command_string"]                    |  |
|  |  -- forkpty -> bash -c "..." -> exits when done                |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  Mode C: Direct Argv (no shell wrapping)                             |
|  +----------------------------------------------------------------+  |
|  |  executable = "/usr/bin/python3"  (or any binary)              |  |
|  |  arguments  = ["script.py", "--arg1"]                          |  |
|  |  -- forkpty -> execvpe() directly, no shell                    |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  Mode D: Provider Mode (AI agent)                                    |
|  +----------------------------------------------------------------+  |
|  |  provider   = ProviderType::Claude | ProviderType::Codex       |  |
|  |  executable = from host_config (provider command path)         |  |
|  |  -- forkpty -> provider CLI/script running in PTY              |  |
|  |  -- SessionManager tracks provider-specific metadata           |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  Special: local-pty (no daemon)                                      |
|  +----------------------------------------------------------------+  |
|  |  sentrits local-pty [cmd]                                      |  |
|  |  -- forkpty directly in CLI process                            |  |
|  |  -- select() loop: user stdin <-> PTY master                   |  |
|  |  -- No network, no daemon, no persistence                      |  |
|  +----------------------------------------------------------------+  |
+----------------------------------------------------------------------+
```

---

## 5. File Descriptor Map (daemon process)

```
+----------------------------------------------------------------------------+
|  sentrits daemon -- open file descriptors                                  |
|                                                                            |
|  Inherited/Standard:                                                       |
|    fd 0  stdin   (may be /dev/null in daemon mode)                         |
|    fd 1  stdout  -> RotatingFileStreamBuf (log file)                       |
|    fd 2  stderr  -> same log file                                          | 
|                                                                            |
|  Listening Sockets:                                                        |
|    fd N  TCP socket  127.0.0.1:18085  (admin HTTP listener)                |
|    fd M  TCP socket  0.0.0.0:18080    (remote HTTP listener)               |
|    fd K  Unix socket ~/.sentrits/controller.sock (LocalControllerListener) |
|                                                                            |
|  Per admin TCP connection (HttpSession):                                   |
|    fd X  TCP connected socket  (client <-> admin)                          |
|          -- upgraded to WebSocket in-place (same fd)                       |
|                                                                            |
|  Per remote TCP connection:                                                |
|    fd X  TCP socket  (plain or SSL-wrapped)                                |
|                                                                            |
|  Per Unix domain socket connection (LocalControllerSession):               |
|    fd X  unix stream socket  (CLI attach)                                  |
|                                                                            |
|  Per spawned session (PosixPtyProcess):                                    |
|    fd P  PTY master  (from forkpty -- R/W for daemon)                      |
|    -- child holds PTY slave as fd 0/1/2 (in child process space)           |
|                                                                            |
|  Per session extras:                                                       |
|    fd Q  pipe (error pipe, FD_CLOEXEC, used for exec-failure detection)    |
|    fd R  inotify/kqueue fd  (WorkspaceFileWatcher, per session)            |
|    fd S  (GitInspector subprocess pipe, per session, transient)            |
|                                                                            |
|  TLS (if configured):                                                      |
|    SSL context over the remote TCP socket fd (same fd, layered)            |
|                                                                            |
|  Summary per-session count: 1 PTY master + 1 error pipe + 1 watcher fd     |
+----------------------------------------------------------------------------+
```

---

## 6. Data Flow: CLI Attach (raw terminal)

```
  User terminal                 sentrits CLI              sentrits daemon
  -------------                 ------------              ---------------
  keystrokes
     |
     v
  raw mode stdin
     |
     +---> HTTP POST /sessions  ------------------>  SessionManager::CreateSession()
     |                                                  +-- forkpty() -> child
     |
     +---> WebSocket connect
           ws://127.0.0.1:18085/ws/sessions/{id}
               |                                         |
               |  <--- ANSI output frames -------------- SessionPump
               |        (from SessionOutputBuffer)            |
               |                                         PosixPtyProcess::Read()
               |                                              |
               |                                         PTY master fd
               |                                              |  (kernel)
               |                                         PTY slave (child's stdin/stdout)
               |                                              |
               |                                         bash / provider process
               |
               +---> send input bytes -----------------> SessionManager::SendInput()
                                                           +-- write(master_fd_, ...)
```

---

## 7. Key Design Properties

| Property | Value |
|---|---|
| Event model | Single-threaded Boost.ASIO io_context |
| Session I/O | Polled by timer (~10ms), not epoll on PTY master fds |
| Output storage | Circular buffer, seq-numbered, replay-capable |
| Terminal state | libvterm tracks screen for snapshot/reconnect |
| Admin auth | Local TCP -- no token (localhost trust) |
| Remote auth | Bearer JWT or pairing token |
| Session persistence | FileSessionStore -> survives daemon restart |
| PTY child isolation | FD_CLOEXEC on all non-essential parent fds |
| Watcher ownership | Per-session (GitInspector + WorkspaceFileWatcher) |
| Controller exclusivity | One controller per session (Host, Remote, or None) |
