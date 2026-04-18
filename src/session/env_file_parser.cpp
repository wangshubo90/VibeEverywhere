#include "env_file_parser.h"

#include <algorithm>
#include <string_view>

namespace vibe::session {

namespace {

auto TrimLeft(const std::string_view s) -> std::string_view {
  std::size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
    start += 1;
  }
  return s.substr(start);
}

auto TrimRight(const std::string_view s) -> std::string_view {
  std::size_t end = s.size();
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
    end -= 1;
  }
  return s.substr(0, end);
}

auto Trim(const std::string_view s) -> std::string_view {
  return TrimRight(TrimLeft(s));
}

// Strip inline comment: unquoted '#' preceded by whitespace.
auto StripInlineComment(const std::string_view s) -> std::string_view {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char ch = s[i];
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
    } else if (ch == '"' && !in_single) {
      in_double = !in_double;
    } else if (ch == '#' && !in_single && !in_double) {
      if (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t')) {
        return TrimRight(s.substr(0, i));
      }
    }
  }
  return s;
}

// Expand ${VAR} and $VAR references from current_env.
auto ExpandVars(const std::string_view s,
                const std::unordered_map<std::string, std::string>& env) -> std::string {
  std::string result;
  result.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '$') {
      result.push_back(s[i]);
      i += 1;
      continue;
    }
    // '$' found
    i += 1;  // skip '$'
    if (i >= s.size()) {
      result.push_back('$');
      break;
    }
    if (s[i] == '{') {
      // ${VAR} form
      i += 1;  // skip '{'
      const std::size_t start = i;
      while (i < s.size() && s[i] != '}') {
        i += 1;
      }
      const std::string var_name(s.substr(start, i - start));
      if (i < s.size()) {
        i += 1;  // skip '}'
      }
      const auto it = env.find(var_name);
      if (it != env.end()) {
        result += it->second;
      }
    } else if ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '_') {
      // $VAR form
      const std::size_t start = i;
      while (i < s.size() &&
             ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) {
        i += 1;
      }
      const std::string var_name(s.substr(start, i - start));
      const auto it = env.find(var_name);
      if (it != env.end()) {
        result += it->second;
      }
    } else {
      result.push_back('$');
      // don't advance i; let the next iteration handle it
    }
  }
  return result;
}

auto UnquoteValue(const std::string_view raw,
                  const std::unordered_map<std::string, std::string>& env) -> std::string {
  if (raw.empty()) {
    return {};
  }

  // Single-quoted: literal (no expansion, no escapes)
  if (raw.front() == '\'' && raw.back() == '\'' && raw.size() >= 2) {
    return std::string(raw.substr(1, raw.size() - 2));
  }

  // Double-quoted: expansion but no shell escapes beyond \"
  if (raw.front() == '"' && raw.back() == '"' && raw.size() >= 2) {
    const std::string inner(raw.substr(1, raw.size() - 2));
    // Unescape \" inside double quotes
    std::string unescaped;
    unescaped.reserve(inner.size());
    for (std::size_t i = 0; i < inner.size(); ++i) {
      if (inner[i] == '\\' && i + 1 < inner.size() && inner[i + 1] == '"') {
        unescaped.push_back('"');
        i += 1;
      } else {
        unescaped.push_back(inner[i]);
      }
    }
    return ExpandVars(unescaped, env);
  }

  // Unquoted: expand, strip inline comment, trim
  const std::string_view stripped = StripInlineComment(raw);
  const std::string_view trimmed = TrimRight(stripped);
  return ExpandVars(trimmed, env);
}

}  // namespace

auto ParseEnvFile(const std::string& content,
                  const std::unordered_map<std::string, std::string>& current_env)
    -> std::vector<std::pair<std::string, std::string>> {
  std::vector<std::pair<std::string, std::string>> result;
  std::unordered_map<std::string, std::string> accumulated = current_env;

  std::size_t pos = 0;
  while (pos <= content.size()) {
    // Find end of line
    const std::size_t line_end = content.find('\n', pos);
    const std::size_t actual_end = (line_end == std::string::npos) ? content.size() : line_end;
    const std::string_view raw_line(content.data() + pos, actual_end - pos);
    pos = actual_end + 1;

    // Trim carriage return
    std::string_view line = raw_line;
    if (!line.empty() && line.back() == '\r') {
      line = line.substr(0, line.size() - 1);
    }
    line = Trim(line);

    // Skip blank lines and comments
    if (line.empty() || line.front() == '#') {
      continue;
    }

    // Optional "export " prefix
    if (line.substr(0, 7) == "export ") {
      line = TrimLeft(line.substr(7));
    }

    // Find '='
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      continue;  // Malformed line, skip
    }

    const std::string_view key_sv = TrimRight(line.substr(0, eq));
    const std::string_view raw_value = line.substr(eq + 1);

    if (key_sv.empty()) {
      continue;
    }

    // Validate key: alphanumeric + underscore
    bool valid_key = true;
    for (const char ch : key_sv) {
      if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_')) {
        valid_key = false;
        break;
      }
    }
    if (!valid_key) {
      continue;
    }

    const std::string key(key_sv);
    const std::string value = UnquoteValue(raw_value, accumulated);

    result.emplace_back(key, value);
    accumulated[key] = value;

    if (actual_end == content.size()) {
      break;
    }
  }

  return result;
}

}  // namespace vibe::session
