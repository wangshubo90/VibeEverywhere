# 25 PTY Output Streaming, Buffering, and Backpressure Design

This section defines how vibe-hostd should handle terminal output from coding CLI sessions.

The main goal is:

never block PTY reading because of slow clients.

A coding CLI may sometimes produce very large outputs:

- build logs
- test logs
- package install logs
- git diff output
- stack traces

If output is pushed directly to WebSocket clients without buffering and rate control, the system may suffer from:

- memory growth
- stalled event loop
- dropped client connection
- frozen browser UI

Therefore vibe-hostd should treat terminal output as a high-throughput stream with explicit buffering and backpressure control.

---

## 25.1 Design Principle

PTY reading must always have higher priority than client delivery.

The system must protect:

- the host daemon
- the session runtime
- the browser client

from burst output.

---

## 25.2 Output Pipeline

Recommended pipeline

PTY -> PTYReader -> SessionOutputBuffer -> EventBus -> ClientDispatcher -> WebSocket

Responsibilities

PTYReader
- continuously reads PTY bytes
- never waits for WebSocket send completion

SessionOutputBuffer
- stores recent output in a bounded ring buffer
- aggregates chunks for downstream delivery

EventBus
- notifies that new output is available

ClientDispatcher
- sends batched updates to subscribed clients
- applies throttling and drop policy if needed

WebSocket
- receives already batched output frames

---

## 25.3 Session Output Buffer

Each session should maintain a bounded in-memory output buffer.

Recommended structure

- raw byte ring buffer for recent terminal stream
- optional line index for tail retrieval
- monotonic sequence number for each appended chunk

Example fields

sessionId  
bufferCapacityBytes  
writeSeq  
chunks[]  

Properties

- bounded memory
- old data evicted automatically
- new clients can request recent tail
- slow clients do not block active sessions

Recommended initial capacity

4 MB to 16 MB per session

This should be configurable.

---

## 25.4 Chunking Strategy

Do not emit one WebSocket message per PTY read.

Instead PTY output should be appended into a session-local aggregation buffer and flushed periodically.

Recommended flush triggers

- output size exceeds threshold
- flush timer fires
- explicit newline-heavy idle point
- session exits

Suggested initial parameters

maxChunkBytes: 8 KB to 32 KB  
flushIntervalMs: 30 to 100 ms  

This gives low perceived latency while avoiding tiny-frame overhead.

---

## 25.5 Sequence Numbers

Each output chunk should carry a monotonically increasing sequence number.

Example

seq: 1042

This allows clients to

- detect missed chunks
- request replay from snapshot or tail
- support reconnect logic

Example event

{
  "type": "terminal.output",
  "sessionId": "s1",
  "seqStart": 1040,
  "seqEnd": 1042,
  "data": "...\n"
}

---

## 25.6 Client Subscription Model

A client does not own the output stream.

A client subscribes to a session stream.

Each subscribed client should maintain its own lightweight delivery state

- lastAckedSeq
- pendingBytes
- deliveryMode

Possible deliveryMode

live
catchup
degraded

Definitions

live
- receiving normal streamed output

catchup
- newly attached client receiving recent tail first

degraded
- client is too slow, so streaming is throttled or summarized

---

## 25.7 Slow Client Policy

Slow clients must not affect PTY reading or other clients.

Recommended policy

1 if client pending WebSocket bytes exceed threshold, mark client slow  
2 reduce push frequency  
3 if still growing, stop live streaming for that client  
4 client remains attached but enters degraded mode  
5 degraded mode client may manually request latest tail

Suggested thresholds

perClientPendingBytesSoftLimit: 256 KB to 1 MB  
perClientPendingBytesHardLimit: 2 MB to 4 MB  

In degraded mode, the client may receive only:

- periodic session status
- changed files
- git updates
- optional truncated terminal summary

instead of full live output.

---

## 25.8 Browser Protection

The browser terminal should not render unlimited output.

The server should support a recent-tail model, and the client should render only a bounded viewport.

Recommended client behavior

- keep only recent rendered lines
- virtualize terminal view
- avoid full DOM append per line
- prefer terminal canvas renderer or incremental renderer

Server should expose

GET /sessions/{sessionId}/tail?bytes=65536

for reconnect and degraded recovery.

---

## 25.9 Initial Attach Behavior

When a client attaches to a running session, it should not require replay of the entire session history.

Recommended attach behavior

1 client requests session snapshot  
2 server returns metadata + recent tail  
3 client subscribes to live stream from current seq  

This avoids huge initial sync costs.

---

## 25.10 Output Drop Policy

Do not drop PTY output before it enters the session ring buffer unless the host is in extreme failure mode.

Preferred drop order

1 throttle client delivery  
2 degrade slow client  
3 evict oldest session history from ring buffer  
4 only in extreme conditions, truncate stream with explicit truncation marker

If truncation occurs, emit explicit event

{
  "type": "terminal.truncated",
  "sessionId": "s1",
  "droppedBytes": 32768
}

This is better than silent data loss.

---

## 25.11 Line vs Byte Semantics

PTY output should be stored as bytes, not only as lines.

Reason

- terminal output includes ANSI escape sequences
- many tools output partial lines
- interactive terminal redraws are not line based

However optional helper indexes may track newline offsets for tail display.

---

## 25.12 ANSI Handling

Server should preserve raw terminal stream.

Do not fully reinterpret terminal semantics on the server in initial versions.

Recommended approach

- server stores raw bytes
- client terminal widget handles ANSI rendering

Server may later add optional parsing for higher-level summaries, but raw stream remains source of truth.

---

## 25.13 EventBus Interaction

EventBus should not carry large output payload copies unnecessarily.

Recommended approach

- SessionOutputBuffer owns output storage
- EventBus emits lightweight notification such as:
  new output available for session s1 seq 1042
- ClientDispatcher pulls aggregated chunk from SessionOutputBuffer

This avoids repeated copying of large terminal payloads through the whole system.

---

## 25.14 Threading Model Suggestion

Recommended model

PTYReader thread or async task per session
- reads PTY
- appends to SessionOutputBuffer

Dispatcher thread or shared async loop
- batches output delivery
- sends WebSocket messages

FileWatcher and GitInspector
- independent producers into EventBus

This keeps PTY ingestion isolated from network slowness.

---

## 25.15 Lightweight Recovery and Tail

Because the system uses lightweight recovery, only limited recent terminal output should be persisted.

Persistable items

- session metadata
- last known status
- workspace path
- provider type
- recent terminal tail
- recent file changes
- git summary

Do not attempt full terminal replay across restart.

---

## 25.16 Recommended Initial Defaults

These values are starting points and should be configurable

sessionRingBufferBytes = 8 MB  
terminalFlushIntervalMs = 50  
terminalFlushMaxBytes = 16 KB  
clientSoftPendingLimit = 512 KB  
clientHardPendingLimit = 2 MB  
snapshotTailBytes = 64 KB  
maxRenderedClientLines = bounded on client side  

---

## 25.17 Next Refinement Task for AI Agent

The AI agent should refine

1 SessionOutputBuffer data structure  
2 per-client delivery state machine  
3 reconnect and tail replay protocol  
4 degraded mode behavior  
5 API schema for tail and snapshot retrieval  
6 WebSocket batching strategy  
7 memory budget estimation for multi-session burst output