#include "vibe/net/http_shared.h"

#include <boost/json.hpp>

#include <optional>
#include <string>

#include "vibe/net/json.h"
#include "vibe/net/request_parsing.h"

namespace vibe::net {

namespace {

namespace json = boost::json;

constexpr const char* kLocalUiHtml = R"html(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>VibeEverywhere Host Approval</title>
  <style>
    :root { color-scheme: light; font-family: ui-monospace, Menlo, monospace; }
    body { margin: 0; background: #f4efe6; color: #1f2a2f; }
    main { max-width: 860px; margin: 0 auto; padding: 24px; }
    h1 { margin-bottom: 8px; }
    section { background: rgba(255,255,255,0.8); border: 1px solid #d8cdbc; border-radius: 12px; padding: 16px; margin-bottom: 16px; }
    form { display: grid; gap: 8px; }
    input, button { font: inherit; padding: 10px 12px; }
    button { border: 0; background: #1f6f5f; color: white; border-radius: 8px; cursor: pointer; }
    code, pre { white-space: pre-wrap; word-break: break-word; }
  </style>
</head>
<body>
  <main>
    <h1>Host Approval</h1>
    <p>Approve pending pairings and update the local host display name.</p>
    <section>
      <h2>Host</h2>
      <pre id="host-info">loading...</pre>
      <form id="config-form">
        <input id="display-name" name="displayName" placeholder="Display name" />
        <button type="submit">Save Host Config</button>
      </form>
    </section>
    <section>
      <h2>Pending Pairings</h2>
      <pre id="pending-list">loading...</pre>
      <form id="approve-form">
        <input id="pairing-id" name="pairingId" placeholder="Pairing ID" />
        <input id="pairing-code" name="code" placeholder="Code" />
        <button type="submit">Approve Pairing</button>
      </form>
      <pre id="approve-result"></pre>
    </section>
  </main>
  <script src="/ui/app.js"></script>
</body>
</html>)html";

constexpr const char* kLocalUiScript = R"js(async () => {
  const hostInfo = document.getElementById("host-info");
  const pendingList = document.getElementById("pending-list");
  const approveResult = document.getElementById("approve-result");
  const displayName = document.getElementById("display-name");

  async function refresh() {
    const [hostResponse, pendingResponse] = await Promise.all([
      fetch("/host/info"),
      fetch("/pairing/pending"),
    ]);
    const host = await hostResponse.json();
    const pending = await pendingResponse.json();
    hostInfo.textContent = JSON.stringify(host, null, 2);
    pendingList.textContent = JSON.stringify(pending, null, 2);
    displayName.value = host.displayName || "";
  }

  document.getElementById("config-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const response = await fetch("/host/config", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ displayName: displayName.value }),
    });
    hostInfo.textContent = JSON.stringify(await response.json(), null, 2);
  });

  document.getElementById("approve-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const response = await fetch("/pairing/approve", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        pairingId: document.getElementById("pairing-id").value,
        code: document.getElementById("pairing-code").value,
      }),
    });
    approveResult.textContent = JSON.stringify(await response.json(), null, 2);
    await refresh();
  });

  await refresh();
})();)js";

void ApplyCorsHeaders(HttpResponse& response) {
  response.set(http::field::access_control_allow_origin, "*");
  response.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
  response.set(http::field::access_control_allow_headers, "content-type, authorization");
}

auto ExtractBearerToken(const HttpRequest& request) -> std::string {
  const auto it = request.base().find(http::field::authorization);
  if (it == request.base().end()) {
    return "";
  }

  const std::string header = std::string(it->value());
  constexpr std::string_view prefix = "Bearer ";
  if (!header.starts_with(prefix)) {
    return "";
  }

  return header.substr(prefix.size());
}

auto MakeJsonResponse(const HttpRequest& request, const http::status status, std::string body) -> HttpResponse {
  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);
  response.result(status);
  response.set(http::field::content_type, "application/json; charset=utf-8");
  ApplyCorsHeaders(response);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}

auto MakeTextResponse(const HttpRequest& request, const http::status status, const std::string_view content_type,
                      std::string body) -> HttpResponse {
  HttpResponse response;
  response.version(request.version());
  response.keep_alive(false);
  response.result(status);
  response.set(http::field::content_type, content_type);
  ApplyCorsHeaders(response);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}

auto BuildRequestContext(const HttpRequest& request, const HttpRouteContext& context)
    -> vibe::auth::RequestContext {
  return vibe::auth::RequestContext{
      .bearer_token = ExtractBearerToken(request),
      .client_address = context.client_address,
      .target = std::string(request.target()),
      .is_websocket = false,
      .is_local_request = context.is_local_request,
  };
}

auto RequireAuthorization(const HttpRequest& request, const HttpRouteContext& context,
                          const vibe::auth::AuthorizationAction action) -> std::optional<HttpResponse> {
  if (context.authorizer == nullptr) {
    return std::nullopt;
  }

  const auto auth_result = context.authorizer->Authorize(BuildRequestContext(request, context), action);
  if (auth_result.authorized) {
    return std::nullopt;
  }

  const auto status = auth_result.authenticated ? http::status::forbidden : http::status::unauthorized;
  return MakeJsonResponse(
      request, status,
      "{\"error\":\"" + JsonEscape(auth_result.reason.empty() ? "request rejected" : auth_result.reason) + "\"}");
}

auto RequireLocalAuthorization(const HttpRequest& request, const HttpRouteContext& context,
                               const vibe::auth::AuthorizationAction action) -> std::optional<HttpResponse> {
  if (!context.is_local_request) {
    return MakeJsonResponse(request, http::status::forbidden, "{\"error\":\"local access required\"}");
  }

  return RequireAuthorization(request, context, action);
}

auto HandlePairingRequest(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing request\"}");
  }

  const auto pairing_request =
      context.pairing_service->StartPairing(parsed_request->device_name, parsed_request->device_type);
  if (!pairing_request.has_value()) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to start pairing\"}");
  }

  return MakeJsonResponse(request, http::status::created, ToJson(*pairing_request));
}

auto HandlePairingApprove(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response =
          RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ApprovePairing);
      auth_response.has_value()) {
    return *auth_response;
  }

  if (context.pairing_service == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"pairing service unavailable\"}");
  }

  const auto parsed_request = ParsePairingApprovalRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid pairing approval request\"}");
  }

  const auto pairing_record =
      context.pairing_service->ApprovePairing(parsed_request->pairing_id, parsed_request->code);
  if (!pairing_record.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"pairing approval rejected\"}");
  }

  return MakeJsonResponse(request, http::status::ok, ToJson(*pairing_record));
}

auto HandleHostConfig(const HttpRequest& request, const HttpRouteContext& context) -> HttpResponse {
  if (const auto auth_response =
          RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ConfigureHost);
      auth_response.has_value()) {
    return *auth_response;
  }

  if (context.host_config_store == nullptr) {
    return MakeJsonResponse(request, http::status::service_unavailable,
                            "{\"error\":\"host config unavailable\"}");
  }

  const auto parsed_request = ParseHostConfigRequest(request.body());
  if (!parsed_request.has_value()) {
    return MakeJsonResponse(request, http::status::bad_request,
                            "{\"error\":\"invalid host config request\"}");
  }

  const auto existing_identity = context.host_config_store->LoadHostIdentity().value_or(
      vibe::store::HostIdentity{
          .host_id = "local-dev-host",
          .display_name = "",
          .certificate_pem_path = "",
          .private_key_pem_path = "",
      });
  auto updated_identity = existing_identity;
  updated_identity.display_name = parsed_request->display_name;
  if (!context.host_config_store->SaveHostIdentity(updated_identity)) {
    return MakeJsonResponse(request, http::status::internal_server_error,
                            "{\"error\":\"failed to save host config\"}");
  }

  return MakeJsonResponse(request, http::status::ok,
                          ToJsonHostInfo(context.host_config_store->LoadHostIdentity(), false));
}

}  // namespace

auto HandleRequest(const HttpRequest& request, vibe::service::SessionManager& session_manager,
                   const HttpRouteContext& context) -> HttpResponse {
  if (request.method() == http::verb::options) {
    return MakeTextResponse(request, http::status::no_content, "application/json; charset=utf-8", "");
  }

  if (request.method() == http::verb::get && request.target() == "/health") {
    return MakeTextResponse(request, http::status::ok, "text/plain; charset=utf-8", "ok\n");
  }

  if (request.method() == http::verb::get && request.target() == "/ui") {
    if (const auto auth_response =
            RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ConfigureHost);
        auth_response.has_value()) {
      return *auth_response;
    }
    return MakeTextResponse(request, http::status::ok, "text/html; charset=utf-8", kLocalUiHtml);
  }

  if (request.method() == http::verb::get && request.target() == "/ui/app.js") {
    if (const auto auth_response =
            RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ConfigureHost);
        auth_response.has_value()) {
      return *auth_response;
    }
    return MakeTextResponse(request, http::status::ok, "application/javascript; charset=utf-8",
                            kLocalUiScript);
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/request") {
    return HandlePairingRequest(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/pairing/pending") {
    if (const auto auth_response =
            RequireLocalAuthorization(request, context, vibe::auth::AuthorizationAction::ApprovePairing);
        auth_response.has_value()) {
      return *auth_response;
    }

    if (context.pairing_service == nullptr) {
      return MakeJsonResponse(request, http::status::service_unavailable,
                              "{\"error\":\"pairing service unavailable\"}");
    }

    return MakeJsonResponse(request, http::status::ok,
                            ToJson(context.pairing_service->ListPendingPairings()));
  }

  if (request.method() == http::verb::post && request.target() == "/pairing/approve") {
    return HandlePairingApprove(request, context);
  }

  if (request.method() == http::verb::post && request.target() == "/host/config") {
    return HandleHostConfig(request, context);
  }

  if (request.method() == http::verb::get && request.target() == "/host/info") {
    const auto host_identity =
        context.host_config_store != nullptr ? context.host_config_store->LoadHostIdentity() : std::nullopt;
    return MakeJsonResponse(request, http::status::ok, ToJsonHostInfo(host_identity, false));
  }

  if (request.method() == http::verb::get && request.target() == "/sessions") {
    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
        auth_response.has_value()) {
      return *auth_response;
    }

    return MakeJsonResponse(request, http::status::ok, ToJson(session_manager.ListSessions()));
  }

  if (request.method() == http::verb::post && request.target() == "/sessions") {
    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
        auth_response.has_value()) {
      return *auth_response;
    }

    const auto parsed_request = ParseCreateSessionRequest(request.body());
    if (!parsed_request.has_value()) {
      return MakeJsonResponse(request, http::status::bad_request,
                              "{\"error\":\"invalid create session request\"}");
    }

    const auto created = session_manager.CreateSession(*parsed_request);
    if (!created.has_value()) {
      return MakeJsonResponse(request, http::status::internal_server_error,
                              "{\"error\":\"failed to create session\"}");
    }

    return MakeJsonResponse(request, http::status::created, ToJson(*created));
  }

  const std::string target(request.target());
  const std::string sessions_prefix = "/sessions/";
  if (target.rfind(sessions_prefix, 0) == 0) {
    const std::string remainder = target.substr(sessions_prefix.size());
    const auto snapshot_suffix = std::string("/snapshot");
    const auto input_suffix = std::string("/input");
    const auto stop_suffix = std::string("/stop");
    const auto tail_marker = std::string("/tail");

    if (remainder.size() > snapshot_suffix.size() && remainder.ends_with(snapshot_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - snapshot_suffix.size());
      const auto snapshot = session_manager.GetSnapshot(session_id);
      if (!snapshot.has_value()) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }

      return MakeJsonResponse(request, http::status::ok, ToJson(*snapshot));
    }

    const std::size_t tail_pos = remainder.find(tail_marker);
    if (tail_pos != std::string::npos) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, tail_pos);
      const auto tail = session_manager.GetTail(session_id, ParseTailBytes(target));
      if (!tail.has_value()) {
        return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
      }

      return MakeJsonResponse(request, http::status::ok, ToJson(*tail));
    }

    if (request.method() == http::verb::post && remainder.size() > input_suffix.size() &&
        remainder.ends_with(input_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - input_suffix.size());
      const auto input = ParseInputRequest(request.body());
      if (!input.has_value()) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"invalid input request\"}");
      }

      if (!session_manager.SendInput(session_id, *input)) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"unable to send input\"}");
      }

      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"ok\"}");
    }

    if (request.method() == http::verb::post && remainder.size() > stop_suffix.size() &&
        remainder.ends_with(stop_suffix)) {
      if (const auto auth_response =
              RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ControlSession);
          auth_response.has_value()) {
        return *auth_response;
      }

      const std::string session_id = remainder.substr(0, remainder.size() - stop_suffix.size());
      if (!session_manager.StopSession(session_id)) {
        return MakeJsonResponse(request, http::status::bad_request, "{\"error\":\"unable to stop session\"}");
      }

      return MakeJsonResponse(request, http::status::ok, "{\"status\":\"stopped\"}");
    }

    if (const auto auth_response =
            RequireAuthorization(request, context, vibe::auth::AuthorizationAction::ObserveSessions);
        auth_response.has_value()) {
      return *auth_response;
    }

    const auto summary = session_manager.GetSession(remainder);
    if (!summary.has_value()) {
      return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"session not found\"}");
    }

    return MakeJsonResponse(request, http::status::ok, ToJson(*summary));
  }

  return MakeJsonResponse(request, http::status::not_found, "{\"error\":\"not found\"}");
}

}  // namespace vibe::net
