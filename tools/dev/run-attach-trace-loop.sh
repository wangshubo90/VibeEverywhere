#!/bin/zsh

set -euo pipefail

readonly runs="${1:-10}"
readonly filter="${2:-SessionAttachFixture.DISABLED_SessionAttachDoesNotBatchTwoQuickSingleByteEchoes}"

summarize_run() {
  local run_dir="$1"
  local attach_trace="${run_dir}/attach.log"
  local server_trace="${run_dir}/server.log"
  local pty_trace="${run_dir}/pty.log"
  local ws_trace="${run_dir}/ws.log"

  if [[ -f "${attach_trace}" ]]; then
    awk '
      /stdin.read 1$/ { stdin[++stdin_count] = $1 }
      /ws.write.input 1$/ { wsin[++wsin_count] = $1 }
      /ws.read.binary 1$/ { wsbin[++wsbin_count] = $1 }
      /stdout.write 1$/ { stdout[++stdout_count] = $1 }
      END {
        limit = stdin_count
        if (wsin_count < limit) limit = wsin_count
        if (wsbin_count < limit) limit = wsbin_count
        if (stdout_count < limit) limit = stdout_count
        max_total = -1
        max_idx = 0
        for (i = 1; i <= limit; ++i) {
          total = stdout[i] - stdin[i]
          if (total > max_total) {
            max_total = total
            max_idx = i
          }
        }
        if (limit > 0) {
          printf("client iterations=%d worst_us=%d worst_index=%d first_us=%d\n",
                 limit, max_total, max_idx, stdout[1] - stdin[1])
        }
      }
    ' "${attach_trace}"
  fi

  if [[ -f "${server_trace}" ]]; then
    awk '
      /send_input.begin 1$/ { begin[++begin_count] = $1 }
      /send_input.ok 1$/ { ok[++ok_count] = $1 }
      /poll.output 1$/ { out[++out_count] = $1 }
      END {
        limit = begin_count
        if (ok_count < limit) limit = ok_count
        if (out_count < limit) limit = out_count
        max_total = -1
        max_idx = 0
        for (i = 1; i <= limit; ++i) {
          total = out[i] - begin[i]
          if (total > max_total) {
            max_total = total
            max_idx = i
          }
        }
        if (limit > 0) {
          printf("server iterations=%d worst_us=%d worst_index=%d first_us=%d\n",
                 limit, max_total, max_idx, out[1] - begin[1])
        }
      }
    ' "${server_trace}"
  fi

  if [[ -f "${pty_trace}" ]]; then
    awk '
      /pty.write.begin 1$/ { begin[++begin_count] = $1 }
      /pty.write.ok 1$/ { ok[++ok_count] = $1 }
      /pty.read.data 1$/ { readt[++read_count] = $1 }
      END {
        limit = begin_count
        if (ok_count < limit) limit = ok_count
        if (read_count < limit) limit = read_count
        max_total = -1
        max_idx = 0
        for (i = 1; i <= limit; ++i) {
          total = readt[i] - begin[i]
          if (total > max_total) {
            max_total = total
            max_idx = i
          }
        }
        if (limit > 0) {
          printf("pty iterations=%d worst_us=%d worst_index=%d first_us=%d\n",
                 limit, max_total, max_idx, readt[1] - begin[1])
        }
      }
    ' "${pty_trace}"
  fi

  if [[ -f "${ws_trace}" ]]; then
    awk '
      $3 == "ws.write.text" { ++text_count }
      $3 == "ws.write.binary" && $4 == "1" { ++binary_count }
      END {
        printf("ws text_frames=%d binary_frames=%d\n", text_count, binary_count)
      }
    ' "${ws_trace}"
  fi

  echo "trace_dir=${run_dir}"
}

for i in $(seq 1 "${runs}"); do
  run_dir="$(mktemp -d "${TMPDIR:-/tmp}/vibe-attach-run-${i}-XXXXXX")"
  echo "--- run ${i} ---"
  if ! VIBE_TRACE_DIR="${run_dir}" ./build/vibe_tests --gtest_also_run_disabled_tests "--gtest_filter=${filter}"; then
    echo "--- failed run ${i} ---"
    summarize_run "${run_dir}"
    exit 1
  fi
  summarize_run "${run_dir}"
done
