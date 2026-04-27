#ifndef VIBE_NET_HUB_CONTROL_CHANNEL_H
#define VIBE_NET_HUB_CONTROL_CHANNEL_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>

namespace vibe::net {

// RelayTokenStore issues single-use, short-lived tokens that authorize the
// relay bridge to connect to a specific local session WebSocket path.
//
// Tokens are process-local and never leave the host. They are accepted only
// on the observer socket path (WebSocketSession), not on controller sockets.
class RelayTokenStore {
 public:
  static constexpr std::chrono::seconds kTokenTTL{30};

  // Issues a new token bound to session_id. The caller (relay bridge) must
  // present this token as ?access_token= on the matching /ws/sessions/{id}
  // path within kTokenTTL seconds.
  [[nodiscard]] auto Issue(const std::string& session_id) -> std::string;

  // Validates and consumes a token. Returns true only if the token exists,
  // has not expired, and the stored session_id matches the requested one.
  // On success the token is erased (single-use).
  [[nodiscard]] auto ConsumeIfValid(const std::string& token,
                                    const std::string& requested_session_id) -> bool;

  // Removes expired tokens. Called opportunistically; not required for
  // correctness since ConsumeIfValid checks expiry.
  void PruneExpired();

 private:
  struct Entry {
    std::string session_id;
    std::chrono::steady_clock::time_point expires_at;
  };

  std::mutex mutex_;
  std::unordered_map<std::string, Entry> tokens_;
};

// HubControlChannelOptions mirrors the TLS/timeout options from HubClient.
struct HubControlChannelOptions {
  std::chrono::seconds reconnect_delay{5};
  std::chrono::seconds connect_timeout{10};
  bool use_default_verify_paths{true};
  std::optional<std::string> ca_certificate_file;
};

// HubControlChannel maintains a persistent WSS connection to Hub's
// /api/v1/hosts/stream control channel. On relay.requested events it spawns
// a bridge thread that connects the Hub relay WS to the local session WS.
//
// Lifecycle: Start() / Stop(). Start() must be called after the local
// HttpServer is already listening. Stop() joins all bridge threads.
class HubControlChannel {
 public:
  // IssueRelayTokenFn is called by the control channel to get a relay token
  // for a given session_id. Provided by HttpServer.
  using IssueRelayTokenFn = std::function<std::string(const std::string& session_id)>;

  HubControlChannel(std::string hub_url, std::string hub_token,
                    std::uint16_t local_port, bool local_tls,
                    IssueRelayTokenFn issue_relay_token,
                    HubControlChannelOptions options = {});
  ~HubControlChannel();

  HubControlChannel(const HubControlChannel&) = delete;
  auto operator=(const HubControlChannel&) = delete;

  void Start();
  void Stop();

 private:
  void RunControlLoop();
  void HandleRelayRequested(const std::string& channel_id,
                            const std::string& session_id);
  void RunRelayBridge(std::string channel_id, std::string session_id,
                      std::string relay_token);
  void ReapFinishedBridges();

  std::string hub_url_;
  std::string hub_token_;
  std::uint16_t local_port_;
  bool local_tls_;
  IssueRelayTokenFn issue_relay_token_;
  HubControlChannelOptions options_;

  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_{false};
  boost::asio::io_context* current_ioc_{nullptr};  // guarded by mutex_; set during connect/read
  std::thread control_thread_;

  // Bridge threads. Joined on Stop(); finished threads are reaped periodically.
  std::mutex bridges_mutex_;
  std::vector<std::thread> bridge_threads_;
};

}  // namespace vibe::net

#endif
