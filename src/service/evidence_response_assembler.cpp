#include "vibe/service/evidence_response_assembler.h"

#include <sstream>
#include <string_view>
#include <utility>

namespace vibe::service {
namespace {

constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

auto JsonEscape(const std::string_view input) -> std::string {
  std::string escaped;
  escaped.reserve(input.size());
  for (const char ch : input) {
    switch (ch) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20U) {
          escaped += "\\u00";
          constexpr char kHex[] = "0123456789abcdef";
          escaped.push_back(kHex[(static_cast<unsigned char>(ch) >> 4U) & 0x0FU]);
          escaped.push_back(kHex[static_cast<unsigned char>(ch) & 0x0FU]);
        } else {
          escaped.push_back(ch);
        }
        break;
    }
  }
  return escaped;
}

auto Base64UrlEncode(const std::string_view input) -> std::string {
  std::string encoded;
  encoded.reserve(((input.size() + 2U) / 3U) * 4U);
  for (std::size_t index = 0; index < input.size(); index += 3U) {
    const auto a = static_cast<unsigned char>(input[index]);
    const auto b = index + 1U < input.size() ? static_cast<unsigned char>(input[index + 1U]) : 0U;
    const auto c = index + 2U < input.size() ? static_cast<unsigned char>(input[index + 2U]) : 0U;
    encoded.push_back(kBase64UrlAlphabet[(a >> 2U) & 0x3FU]);
    encoded.push_back(kBase64UrlAlphabet[((a & 0x03U) << 4U) | ((b >> 4U) & 0x0FU)]);
    if (index + 1U < input.size()) {
      encoded.push_back(kBase64UrlAlphabet[((b & 0x0FU) << 2U) | ((c >> 6U) & 0x03U)]);
    }
    if (index + 2U < input.size()) {
      encoded.push_back(kBase64UrlAlphabet[c & 0x3FU]);
    }
  }
  return encoded;
}

auto MakeReplayToken(const EvidenceResult& result) -> std::string {
  std::ostringstream json;
  json << "{\"source_session_id\":\"" << JsonEscape(result.source.session_id.value()) << "\","
       << "\"operation\":\"" << ToString(result.operation) << "\","
       << "\"revision_start\":" << result.revision_start << ','
       << "\"revision_end\":" << result.revision_end << ','
       << "\"limit\":" << result.entries.size() << ','
       << "\"query\":\"" << JsonEscape(result.query) << "\"}";
  return Base64UrlEncode(json.str());
}

auto MakeSummary(const EvidenceResult& result, const std::string& source_title) -> std::string {
  std::ostringstream summary;
  summary << "read " << result.entries.size() << " " << ToString(result.operation)
          << " result";
  if (result.entries.size() != 1U) {
    summary << 's';
  }
  if (!source_title.empty()) {
    summary << " from " << source_title;
  }
  return summary.str();
}

}  // namespace

auto EvidenceResponseAssembler::Assemble(EvidenceAssemblyRequest request) const
    -> EvidenceAssembly {
  request.result.replay_token = MakeReplayToken(request.result);
  std::optional<ObservationEvent> observation;
  if (request.actor.has_value() && !request.actor->actor_session_id.empty()) {
    observation = ObservationEvent{
        .timestamp_unix_ms = request.timestamp_unix_ms,
        .actor_session_id = request.actor->actor_session_id,
        .actor_title = request.actor->actor_title,
        .actor_id = request.actor->actor_session_id,
        .pid = request.actor->pid,
        .uid = request.actor->uid,
        .gid = request.actor->gid,
        .operation = request.result.operation,
        .source = request.result.source,
        .source_title = request.source_title,
        .query = request.result.query,
        .revision_start = request.result.revision_start,
        .revision_end = request.result.revision_end,
        .result_count = request.result.entries.size(),
        .truncated = request.result.truncated,
        .summary = MakeSummary(request.result, request.source_title),
        .replay_token = request.result.replay_token,
    };
  }
  return EvidenceAssembly{
      .result = std::move(request.result),
      .observation = std::move(observation),
  };
}

}  // namespace vibe::service
