#!/usr/bin/env python3

import argparse
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def run(cmd, *, cwd=None, env=None, check=True, capture_output=True, timeout=None):
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            env=env,
            check=False,
            text=True,
            capture_output=capture_output,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(
            f"command timed out after {timeout}s: {' '.join(cmd)}\n"
            f"stdout:\n{exc.stdout or ''}\n"
            f"stderr:\n{exc.stderr or ''}"
        ) from exc
    if check and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def http_json(method, url, *, body=None, bearer_token=None, timeout=5):
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if bearer_token:
        headers["Authorization"] = f"Bearer {bearer_token}"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            payload = response.read().decode("utf-8")
            if not payload:
                return response.status, {}
            try:
                return response.status, json.loads(payload)
            except json.JSONDecodeError:
                return response.status, payload
    except urllib.error.HTTPError as exc:
        payload = exc.read().decode("utf-8")
        if not payload:
            parsed = {}
        else:
            try:
                parsed = json.loads(payload)
            except json.JSONDecodeError:
                parsed = payload
        return exc.code, parsed


def hub_is_healthy(hub_url):
    try:
        status, _ = http_json("GET", f"{hub_url}/health")
        return status == 200
    except urllib.error.URLError:
        return False


def wait_for(predicate, *, timeout_s, interval_s=0.1, description):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval_s)
    raise RuntimeError(f"timed out waiting for {description}")


def terminate_process(proc):
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def collect_process_output(proc):
    if proc is None or proc.stdout is None:
        return ""
    try:
        return proc.stdout.read()
    except Exception:
        return ""


def parse_args():
    parser = argparse.ArgumentParser(description="Real Core↔Hub relay smoke proof")
    parser.add_argument("--core-repo", default=str(pathlib.Path(__file__).resolve().parents[1]))
    parser.add_argument("--hub-repo", default=str(pathlib.Path(__file__).resolve().parents[2] / "Sentrits-Hub"))
    parser.add_argument("--hub-bin", default=None)
    parser.add_argument("--sentrits-bin", default=None)
    parser.add_argument("--hub-listen", default="127.0.0.1:19080")
    parser.add_argument("--admin-port", default="19185")
    parser.add_argument("--remote-port", default="19186")
    parser.add_argument("--database-url", default=os.environ.get("DATABASE_URL", ""))
    parser.add_argument("--jwt-secret", default=os.environ.get("JWT_SECRET", "dev-secret"))
    return parser.parse_args()


def main():
    args = parse_args()
    core_repo = pathlib.Path(args.core_repo)
    hub_repo = pathlib.Path(args.hub_repo)
    hub_bin = pathlib.Path(args.hub_bin) if args.hub_bin else None
    sentrits_bin = pathlib.Path(args.sentrits_bin) if args.sentrits_bin else core_repo / "build" / "sentrits"
    if not sentrits_bin.exists():
        raise RuntimeError(f"sentrits binary not found: {sentrits_bin}")
    if not args.database_url:
        raise RuntimeError("DATABASE_URL is required")

    temp_home = pathlib.Path(tempfile.mkdtemp(prefix="sentrits-relay-proof-"))
    serve_proc = None
    hub_proc = None
    failure = None

    try:
        env = os.environ.copy()
        env["HOME"] = str(temp_home)

        storage_root = temp_home / ".sentrits"
        host_identity_path = storage_root / "host_identity.json"
        hub_url = f"http://{args.hub_listen}"
        attach_trace_path = temp_home / "relay-observe.trace"
        serve_cmd = [
            str(sentrits_bin),
            "serve",
            "--admin-port",
            args.admin_port,
            "--remote-port",
            args.remote_port,
            "--no-discovery",
        ]

        serve_proc = subprocess.Popen(
            serve_cmd,
            cwd=str(core_repo),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        wait_for(
            lambda: host_identity_path.exists() and json.loads(host_identity_path.read_text()).get("hostId"),
            timeout_s=5,
            description="initial host identity generation",
        )
        host_identity = json.loads(host_identity_path.read_text())
        host_id = host_identity["hostId"]
        display_name = host_identity.get("displayName", "Sentrits Host")

        terminate_process(serve_proc)
        serve_proc = None

        hub_env = os.environ.copy()
        hub_env["DATABASE_URL"] = args.database_url
        hub_env["JWT_SECRET"] = args.jwt_secret
        hub_env["LISTEN_ADDR"] = args.hub_listen
        if hub_bin is None:
            hub_bin = temp_home / "sentrits-hub"
            run(
                ["go", "build", "-o", str(hub_bin), "./cmd/hub"],
                cwd=str(hub_repo),
                env=hub_env,
            )
        hub_proc = subprocess.Popen(
            [str(hub_bin)],
            cwd=str(hub_repo),
            env=hub_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        wait_for(
            lambda: hub_is_healthy(hub_url),
            timeout_s=10,
            description="hub health",
        )

        email = f"relay-proof-{int(time.time())}@example.com"
        password = "relay-proof-pass"
        status, registered = http_json(
            "POST",
            f"{hub_url}/api/v1/auth/register",
            body={"email": email, "password": password},
        )
        if status != 201:
            raise RuntimeError(f"account register failed: {status} {registered}")
        account_token = registered["token"]

        status, host_register = http_json(
            "POST",
            f"{hub_url}/api/v1/hosts/register",
            body={"host_id": host_id, "display_name": display_name},
            bearer_token=account_token,
        )
        if status != 200:
            raise RuntimeError(f"host register failed: {status} {host_register}")
        hub_token = host_register["hub_token"]

        status, device_register = http_json(
            "POST",
            f"{hub_url}/api/v1/devices/register",
            body={"device_name": "relay-proof-device", "device_type": "desktop"},
            bearer_token=account_token,
        )
        if status != 201:
            raise RuntimeError(f"device register failed: {status} {device_register}")
        device_token = device_register["device_token"]

        run(
            [str(sentrits_bin), "host", "set-hub", hub_url, hub_token],
            cwd=str(core_repo),
            env=env,
        )

        host_identity = json.loads(host_identity_path.read_text())
        provider_commands = host_identity.get("providerCommands", {})
        provider_commands["codex"] = [
            "/bin/bash",
            "-lc",
            "for i in 1 2 3 4 5 6; do echo relay-proof-$i; sleep 1; done",
        ]
        host_identity["providerCommands"] = provider_commands
        host_identity_path.write_text(json.dumps(host_identity))

        serve_proc = subprocess.Popen(
            serve_cmd,
            cwd=str(core_repo),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        wait_for(
            lambda: http_json(
                "GET",
                f"{hub_url}/api/v1/devices/hosts",
                bearer_token=device_token,
            )[1],
            timeout_s=10,
            description="host listing availability",
        )

        wait_for(
            lambda: any(
                host.get("host_id") == host_id and host.get("online") is True
                for host in http_json(
                    "GET",
                    f"{hub_url}/api/v1/devices/hosts",
                    bearer_token=device_token,
                )[1]
            ),
            timeout_s=15,
            description="host online heartbeat in hub",
        )
        time.sleep(4)

        session_result = run(
            [
                str(sentrits_bin),
                "session",
                "start",
                "--host",
                "127.0.0.1",
                "--port",
                args.admin_port,
                "--provider",
                "codex",
                "--title",
                "relay-proof",
            ],
            cwd=str(core_repo),
            env=env,
        )
        tokens = session_result.stdout.strip().split()
        if len(tokens) < 2 or tokens[0] != "session":
            raise RuntimeError(f"unexpected session start output: {session_result.stdout}")
        session_id = tokens[1]

        relay_result = run(
            [
                str(sentrits_bin),
                "relay",
                "observe",
                "--hub-url",
                hub_url,
                "--token",
                device_token,
                "--host-id",
                host_id,
                "--session-id",
                session_id,
            ],
            cwd=str(core_repo),
            env={**env, "VIBE_ATTACH_TRACE_PATH": str(attach_trace_path)},
            timeout=30,
        )

        if "relay-proof" not in relay_result.stdout:
            raise RuntimeError(
                "relay observe did not receive expected session output\n"
                f"stdout:\n{relay_result.stdout}\n"
                f"stderr:\n{relay_result.stderr}"
            )

        print("relay smoke proof passed")
        print(f"host_id={host_id}")
        print(f"session_id={session_id}")
        return 0
    except Exception as exc:
        failure = exc
        raise
    finally:
        if serve_proc is not None:
            terminate_process(serve_proc)
        if hub_proc is not None:
            terminate_process(hub_proc)
        if failure is not None:
            serve_output = collect_process_output(serve_proc)
            hub_output = collect_process_output(hub_proc)
            attach_trace = attach_trace_path.read_text() if attach_trace_path.exists() else ""
            if serve_output:
                print("\n=== Core serve output ===", file=sys.stderr)
                print(serve_output, file=sys.stderr)
            if hub_output:
                print("\n=== Hub output ===", file=sys.stderr)
                print(hub_output, file=sys.stderr)
            if attach_trace:
                print("\n=== Relay observe trace ===", file=sys.stderr)
                print(attach_trace, file=sys.stderr)
        shutil.rmtree(temp_home, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
