#ifndef VIBE_SESSION_ENV_RESOLVER_H
#define VIBE_SESSION_ENV_RESOLVER_H

#include <string>
#include <string_view>
#include <unordered_map>

#include "vibe/session/bootstrapped_env_cache.h"
#include "vibe/session/env_config.h"
#include "vibe/store/host_config_store.h"

namespace vibe::session {

using EffectiveEnvResult = EnvResult<EffectiveEnvironment>;

// Resolve a complete EffectiveEnvironment from EnvConfig + context.
// Signature mandated by the design document.
[[nodiscard]] auto ResolveEnvironment(const EnvConfig& config,
                                      std::string_view workspace_root,
                                      const vibe::store::HostIdentity& host_config,
                                      BootstrappedEnvCache& cache,
                                      const std::unordered_map<std::string, std::string>& provider_overrides)
    -> EffectiveEnvResult;

// Resolve the shell to use for bootstrap (respects HostIdentity::bootstrap_shell_path,
// then $SHELL, then platform default).
[[nodiscard]] auto ResolveBootstrapShell(const vibe::store::HostIdentity& host_config) -> std::string;

}  // namespace vibe::session

#endif
