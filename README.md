# VibeEverywhere

VibeEverywhere is a remote session system for AI coding CLIs. The project is being implemented in C++20 using CMake, Ninja, and Clang/LLVM, with test-driven development as the default engineering approach.

Current runtime target:

- macOS and Linux only
- PTY-backed session execution currently uses a shared POSIX `forkpty` backend behind an `IPtyProcess` factory seam
- file watching and process-tree/resource inspection are still planned seams, not completed Linux-ready subsystems

Start here:

- [VIBING.md](/Users/shubow/dev/VibeEverywhere/VIBING.md)
- [development _memo/implementation_plan.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/implementation_plan.md)
- [development _memo/build_and_test.md](/Users/shubow/dev/VibeEverywhere/development%20_memo/build_and_test.md)
