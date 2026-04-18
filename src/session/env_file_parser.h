#ifndef VIBE_SESSION_ENV_FILE_PARSER_H
#define VIBE_SESSION_ENV_FILE_PARSER_H

#include <string>
#include <unordered_map>
#include <vector>

namespace vibe::session {

// Parse a .env file (KEY=VALUE lines, comments, quotes) into key-value pairs.
// `current_env` is used for ${VAR} expansion within values.
// Lines starting with '#' are comments; empty lines are ignored.
// Values may be single- or double-quoted (quotes are stripped).
// ${VAR} and $VAR are expanded from current_env at parse time.
[[nodiscard]] auto ParseEnvFile(const std::string& content,
                                const std::unordered_map<std::string, std::string>& current_env)
    -> std::vector<std::pair<std::string, std::string>>;

}  // namespace vibe::session

#endif
