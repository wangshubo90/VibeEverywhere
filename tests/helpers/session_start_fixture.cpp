#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

auto ParseInt(const char* value, const int fallback) -> int {
  if (value == nullptr) {
    return fallback;
  }

  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: session_start_fixture <mode> [args...]\n";
    return 64;
  }

  const std::string mode = argv[1];
  if (mode == "success") {
    std::cout << "fixture:success\n";
    return 0;
  }

  if (mode == "exit") {
    const int code = argc >= 3 ? ParseInt(argv[2], 1) : 1;
    const std::string message = argc >= 4 ? argv[3] : "fixture:exit";
    std::cerr << message << '\n';
    return code;
  }

  if (mode == "require-env") {
    if (argc < 4) {
      std::cerr << "fixture:require-env missing arguments\n";
      return 64;
    }

    const std::string key = argv[2];
    const std::string expected = argv[3];
    const char* actual = std::getenv(key.c_str());
    if (actual == nullptr) {
      std::cerr << "fixture:missing-env " << key << '\n';
      return 41;
    }
    if (expected != actual) {
      std::cerr << "fixture:wrong-env " << key << " expected=" << expected
                << " actual=" << actual << '\n';
      return 42;
    }

    std::cout << "fixture:env-ok " << key << "=" << actual << '\n';
    return 0;
  }

  if (mode == "sleep") {
    const int seconds = argc >= 3 ? ParseInt(argv[2], 5) : 5;
    std::cout << "fixture:sleep " << seconds << '\n';
    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    std::cout << "fixture:wake\n";
    return 0;
  }

  if (mode == "throw") {
    const std::string message = argc >= 3 ? argv[2] : "fixture:throw";
    throw std::runtime_error(message);
  }

  std::cerr << "fixture:unknown-mode " << mode << '\n';
  return 64;
}
