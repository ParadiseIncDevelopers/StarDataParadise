"""Exercise the Qt PL/SQL Studio and authenticated debugger isolation over TLS."""

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import secrets
import socket
import ssl
import struct
import tempfile
import time

from workbench_sql_smoke import instance, run


ROUTINE = "studio_increment"
PERMISSION_DENIED = 1006
MAX_FRAME = 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Session:
    def __init__(self, stream):
        self.stream = stream
        self.id = 0

    def send(self, operation, **body):
        self.id += 1
        data = json.dumps({"v": 1, "id": self.id, "op": operation, "body": body}).encode("utf-8")
        require(1 <= len(data) <= MAX_FRAME, "invalid request size")
        self.stream.sendall(struct.pack("!I", len(data)) + data)

    def read_exact(self, size):
        data = bytearray(size)
        remaining = memoryview(data)
        while remaining:
            count = self.stream.recv_into(remaining)
            require(count > 0, "Server closed the response")
            remaining = remaining[count:]
        return data

    def receive(self):
        size, = struct.unpack("!I", self.read_exact(4))
        require(1 <= size <= MAX_FRAME, "invalid response size")
        result = json.loads(self.read_exact(size))
        require(result["v"] == 1 and result["id"] == self.id, "unexpected response envelope")
        return result

    def raw(self, operation, **body):
        self.send(operation, **body)
        return self.receive()

    def call(self, operation, **body):
        reply = self.raw(operation, **body)
        require(reply["ok"], f"{operation} failed with code {reply.get('error', {}).get('code')}")
        return reply["body"]

    def ddl(self, sql):
        statement = self.call("query.prepare", sql=sql)["statement_id"]
        self.call("execute", statement_id=statement, request_token=secrets.token_hex(16))
        self.call("statement_close", statement_id=statement)


@contextmanager
def login(port, cert, user, password):
    context = ssl.create_default_context(cafile=str(cert))
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    with socket.create_connection(("127.0.0.1", port), timeout=10) as raw:
        with context.wrap_socket(raw, server_hostname="localhost") as tls:
            session = Session(tls)
            session.session_id = session.call("hello")["session_id"]
            session.call("auth", username=user, password=password)
            yield session


def verify_debugger_isolation(port, cert, administrator):
    visitor = secrets.token_urlsafe(24)
    with login(port, cert, "local-admin", administrator) as owner:
        owner.ddl("CREATE USER debug_visitor")
        owner.call("password_set", username="debug_visitor", new_password=visitor,
                   request_token=secrets.token_hex(16))
        owner.ddl("GRANT DEBUG ON GLOBAL TO USER debug_visitor")
        owner.ddl(f"GRANT EXECUTE ON ROUTINE {ROUTINE} TO USER debug_visitor")

        with login(port, cert, "local-admin", administrator) as target, \
                login(port, cert, "debug_visitor", visitor) as other:
            prepared = target.call("query.prepare", sql=f"SELECT {ROUTINE}(5)")["statement_id"]
            binding = owner.call("debug_open", session_id=target.session_id, routine=ROUTINE, revision=1)["debug_id"]
            owner.call("debug_breakpoint", debug_id=binding, line=4, revision=1)
            target.send("execute", statement_id=prepared)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                reply = owner.call("debug_status", debug_id=binding)
                if reply["state"] == "paused":
                    break
                time.sleep(0.01)
            else:
                raise RuntimeError("isolated routine did not reach the active breakpoint")

            stack = reply["stack"]
            require(len(stack) == 1 and stack[0]["routine"][-1] == ROUTINE and stack[0]["revision"] == 1,
                    "owner must see the paused routine identity and revision")
            local = owner.call("debug_watch", debug_id=binding, name="local_value")["value"]
            require(local == {"t": "i64", "v": "5"}, "owner must see a real local value")
            denied_requests = [
                ("debug_open", dict(session_id=target.session_id, routine=ROUTINE, revision=1)),
                ("debug_status", dict(debug_id=binding)),
                ("debug_watch", dict(debug_id=binding, name="local_value")),
            ]
            for op, fields in denied_requests:
                denied = other.raw(op, **fields)
                require(not denied["ok"] and denied["error"]["code"] == PERMISSION_DENIED and "body" not in denied,
                        f"unauthorized {op} exposed debugger data")
            owner.call("debug_continue", debug_id=binding)
            result = target.receive()
            require(result["ok"], "authorized debugger could not finish")
            target.call("fetch", result_id=result["body"]["result_id"], batch_rows=4)
            owner.call("debug_close", debug_id=binding)
    print("S39-T03: foreign target attach, paused stack and locals denied over TLS")


def verify_test_instance_guard(port, cert, administrator):
    with login(port, cert, "local-admin", administrator) as owner:
        owner.ddl("CREATE FUNCTION fixture_only(p IN BIGINT) RETURN BIGINT AS BEGIN RETURN p; END;")
        statement = owner.call("query.prepare", sql="SELECT fixture_only(5)")["statement_id"]
        denied = owner.raw("routine_test", statement_id=statement, parameters={}, expected={"sets": []})
        require(not denied["ok"] and denied["error"]["code"] == PERMISSION_DENIED,
                "non-test instance allowed a routine test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + key, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="routine-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        administrator = secrets.token_urlsafe(24)
        with instance(args, root / "server", cert, key, administrator, routine_test_instance=True) as port:
            profile = dict(name="Routine Studio test", host="127.0.0.1", port=port, user="local-admin",
                           ca_file=str(cert), tls_name="localhost", database="main", environment="test",
                           read_only=False)
            profile_path = root / "profiles.json"
            profile_path.write_text(json.dumps(dict(version=1, profiles=[profile])), encoding="utf-8")
            print(run([args.client, profile_path], (administrator,), cwd=root, input=administrator + "\n").strip())
            verify_debugger_isolation(port, cert, administrator)
        with instance(args, root / "non-test", cert, key, administrator) as port:
            verify_test_instance_guard(port, cert, administrator)
        print("Routine test rejected on a non-test Server even for an administrator")


if __name__ == "__main__":
    main()
