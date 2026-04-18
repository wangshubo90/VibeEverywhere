#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

#include <sys/stat.h>

#include "../../src/session/env_resolver.h"
#include "vibe/store/host_config_store.h"

namespace vibe::session {
namespace {

class EnvResolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = std::filesystem::temp_directory_path() /
                ("vibe-env-resolver-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(test_dir_);
  }

  [[nodiscard]] auto test_dir() const -> const std::filesystem::path& { return test_dir_; }

  void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output << content;
  }

  [[nodiscard]] auto EntriesToMap(const EffectiveEnvironment& env)
      -> std::unordered_map<std::string, EnvEntry> {
    std::unordered_map<std::string, EnvEntry> entries;
    for (const auto& entry : env.entries) {
      entries[entry.key] = entry;
    }
    return entries;
  }

 private:
  std::filesystem::path test_dir_;
};

TEST_F(EnvResolverTest, CleanModeAppliesEnvFilesProviderAndSessionOverridesInOrder) {
  WriteFile(test_dir() / ".env.in",
            "BASE=from_in\n"
            "SHARED=from_in\n"
            "EXPANDED=$HOME-sfx\n");
  WriteFile(test_dir() / ".env",
            "LOCAL=from_env\n"
            "SHARED=from_env\n");

  BootstrappedEnvCache cache;
  const vibe::store::HostIdentity host_config = vibe::store::MakeDefaultHostIdentity();
  const EnvConfig config{
      .mode = EnvMode::Clean,
      .overrides = {
          {"SHARED", "from_session"},
          {"SESSION_ONLY", "session_value"},
      },
      .env_file_path = std::nullopt,
  };

  const auto result = ResolveEnvironment(
      config, test_dir().string(), host_config, cache,
      {
          {"LOCAL", "from_provider"},
          {"PROVIDER_ONLY", "provider_value"},
          {"SHARED", "from_provider"},
      });

  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result.value().mode, EnvMode::Clean);
  ASSERT_TRUE(result.value().env_file_path.has_value());
  EXPECT_EQ(*result.value().env_file_path, (test_dir() / ".env").string());

  const auto entries = EntriesToMap(result.value());
  ASSERT_TRUE(entries.contains("PATH"));
  EXPECT_EQ(entries.at("PATH").value, "/usr/bin:/bin:/usr/sbin:/sbin");
  EXPECT_EQ(entries.at("PATH").source, EnvSource::DaemonInherited);

  const char* home = std::getenv("HOME");
  ASSERT_NE(home, nullptr);
  ASSERT_TRUE(entries.contains("EXPANDED"));
  EXPECT_EQ(entries.at("EXPANDED").value, std::string(home) + "-sfx");
  EXPECT_EQ(entries.at("EXPANDED").source, EnvSource::EnvFile);

  ASSERT_TRUE(entries.contains("LOCAL"));
  EXPECT_EQ(entries.at("LOCAL").value, "from_provider");
  EXPECT_EQ(entries.at("LOCAL").source, EnvSource::ProviderConfig);

  ASSERT_TRUE(entries.contains("PROVIDER_ONLY"));
  EXPECT_EQ(entries.at("PROVIDER_ONLY").value, "provider_value");
  EXPECT_EQ(entries.at("PROVIDER_ONLY").source, EnvSource::ProviderConfig);

  ASSERT_TRUE(entries.contains("SHARED"));
  EXPECT_EQ(entries.at("SHARED").value, "from_session");
  EXPECT_EQ(entries.at("SHARED").source, EnvSource::SessionOverride);

  ASSERT_TRUE(entries.contains("SESSION_ONLY"));
  EXPECT_EQ(entries.at("SESSION_ONLY").value, "session_value");
  EXPECT_EQ(entries.at("SESSION_ONLY").source, EnvSource::SessionOverride);
}

TEST_F(EnvResolverTest, BootstrapModeReturnsWarningWhenBootstrapShellWritesStderr) {
  const auto fake_shell = test_dir() / "fake-bootstrap-shell.sh";
  WriteFile(fake_shell,
            "#!/bin/sh\n"
            "printf 'bootstrap warning\\n' >&2\n"
            "printf 'PATH=/bootstrap/bin\\000HOME=/bootstrap/home\\000FROM_BOOTSTRAP=1\\000'\n");
  ASSERT_EQ(chmod(fake_shell.c_str(), 0755), 0);

  BootstrappedEnvCache cache;
  vibe::store::HostIdentity host_config = vibe::store::MakeDefaultHostIdentity();
  host_config.bootstrap_shell_path = fake_shell.string();

  const EnvConfig config{
      .mode = EnvMode::BootstrapFromShell,
      .overrides = {{"FROM_SESSION", "1"}},
      .env_file_path = std::nullopt,
  };

  const auto result = ResolveEnvironment(
      config, test_dir().string(), host_config, cache, {{"FROM_PROVIDER", "1"}});

  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result.value().mode, EnvMode::BootstrapFromShell);
  ASSERT_TRUE(result.value().bootstrap_shell_path.has_value());
  EXPECT_EQ(*result.value().bootstrap_shell_path, fake_shell.string());
  ASSERT_TRUE(result.value().bootstrap_warning.has_value());
  EXPECT_NE(result.value().bootstrap_warning->find("bootstrap warning"), std::string::npos);

  const auto entries = EntriesToMap(result.value());
  ASSERT_TRUE(entries.contains("FROM_BOOTSTRAP"));
  EXPECT_EQ(entries.at("FROM_BOOTSTRAP").value, "1");
  EXPECT_EQ(entries.at("FROM_BOOTSTRAP").source, EnvSource::BootstrapShell);
  ASSERT_TRUE(entries.contains("FROM_PROVIDER"));
  EXPECT_EQ(entries.at("FROM_PROVIDER").source, EnvSource::ProviderConfig);
  ASSERT_TRUE(entries.contains("FROM_SESSION"));
  EXPECT_EQ(entries.at("FROM_SESSION").source, EnvSource::SessionOverride);
  ASSERT_TRUE(entries.contains("PATH"));
  EXPECT_EQ(entries.at("PATH").value, "/bootstrap/bin");
}

}  // namespace
}  // namespace vibe::session
