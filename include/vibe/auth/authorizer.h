#ifndef VIBE_AUTH_AUTHORIZER_H
#define VIBE_AUTH_AUTHORIZER_H

#include <optional>
#include <string>

#include "vibe/auth/pairing.h"

namespace vibe::auth {

enum class AuthorizationAction {
  ObserveSessions,
  ControlSession,
  ApprovePairing,
  ConfigureHost,
};

struct RequestContext {
  std::string bearer_token;
  std::string client_address;
  std::string target;
  bool is_websocket{false};
  bool is_local_request{false};

  [[nodiscard]] auto operator==(const RequestContext& other) const -> bool = default;
};

struct AuthResult {
  bool authenticated{false};
  bool authorized{false};
  std::optional<DeviceId> device_id;
  std::string reason;

  [[nodiscard]] auto operator==(const AuthResult& other) const -> bool = default;
};

class Authorizer {
 public:
  virtual ~Authorizer() = default;

  [[nodiscard]] virtual auto AuthenticateBearerToken(const std::string& bearer_token) const
      -> AuthResult = 0;
  [[nodiscard]] virtual auto Authorize(const RequestContext& request_context,
                                       AuthorizationAction action) const -> AuthResult = 0;
};

}  // namespace vibe::auth

#endif
