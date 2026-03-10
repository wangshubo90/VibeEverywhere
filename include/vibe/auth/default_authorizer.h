#ifndef VIBE_AUTH_DEFAULT_AUTHORIZER_H
#define VIBE_AUTH_DEFAULT_AUTHORIZER_H

#include "vibe/auth/authorizer.h"
#include "vibe/store/pairing_store.h"

namespace vibe::auth {

class DefaultAuthorizer final : public Authorizer {
 public:
  explicit DefaultAuthorizer(const vibe::store::PairingStore& pairing_store);

  [[nodiscard]] auto AuthenticateBearerToken(const std::string& bearer_token) const
      -> AuthResult override;
  [[nodiscard]] auto Authorize(const RequestContext& request_context,
                               AuthorizationAction action) const -> AuthResult override;

 private:
  const vibe::store::PairingStore& pairing_store_;
};

}  // namespace vibe::auth

#endif
