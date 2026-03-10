#include "vibe/auth/default_authorizer.h"

namespace vibe::auth {

DefaultAuthorizer::DefaultAuthorizer(const vibe::store::PairingStore& pairing_store)
    : pairing_store_(pairing_store) {}

auto DefaultAuthorizer::AuthenticateBearerToken(const std::string& bearer_token) const -> AuthResult {
  if (bearer_token.empty()) {
    return AuthResult{
        .authenticated = false,
        .authorized = false,
        .device_id = std::nullopt,
        .reason = "missing bearer token",
    };
  }

  for (const auto& record : pairing_store_.LoadApprovedPairings()) {
    if (record.bearer_token == bearer_token) {
      return AuthResult{
          .authenticated = true,
          .authorized = true,
          .device_id = record.device_id,
          .reason = "",
      };
    }
  }

  return AuthResult{
      .authenticated = false,
      .authorized = false,
      .device_id = std::nullopt,
      .reason = "invalid bearer token",
  };
}

auto DefaultAuthorizer::Authorize(const RequestContext& request_context,
                                  const AuthorizationAction action) const -> AuthResult {
  if (action == AuthorizationAction::ApprovePairing ||
      action == AuthorizationAction::ConfigureHost) {
    if (!request_context.is_local_request) {
      const auto auth_result = AuthenticateBearerToken(request_context.bearer_token);
      return AuthResult{
          .authenticated = auth_result.authenticated,
          .authorized = false,
          .device_id = auth_result.device_id,
          .reason = "local request required",
      };
    }

    return AuthResult{
        .authenticated = true,
        .authorized = true,
        .device_id = std::nullopt,
        .reason = "",
    };
  }

  return AuthenticateBearerToken(request_context.bearer_token);
}

}  // namespace vibe::auth
