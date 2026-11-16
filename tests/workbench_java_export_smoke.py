"""Exercise the Java export wizard and build its actual output after the fixture Server exits."""

import argparse
import json
import os
from pathlib import Path
import queue
import secrets
import socket
import subprocess
import sys
import tempfile
import threading
import time

from workbench_routine_smoke import PERMISSION_DENIED, instance, login, require, run


PURE = "export_offline_increment"
HOSTED = "export_hosted_lookup"
HIDDEN = "export_hidden_table"


def metadata(session, name):
    return next(obj for obj in session.call("metadata")["objects"]
                if obj.get("kind") == 6 and obj["qualified_name"][-1] == name)


def seed(port, cert, admin, guest):
    with login(port, cert, "local-admin", admin) as controller:
        controller.ddl(f"CREATE TABLE {HIDDEN} (id BIGINT PRIMARY KEY, value BIGINT)")
        controller.ddl(f"INSERT INTO {HIDDEN} VALUES (5,9)")
        controller.ddl(f"CREATE FUNCTION {PURE}(p IN BIGINT) RETURN BIGINT AS\nBEGIN\n  RETURN p + 2;\nEND;")
        controller.ddl(f"CREATE FUNCTION {HOSTED}(p IN BIGINT) RETURN BIGINT AS\nBEGIN\n"
                       f"  RETURN (SELECT h.value FROM {HIDDEN} AS h WHERE h.id = :p);\nEND;")
        controller.ddl("CREATE USER export_guest")
        controller.call("password_set", username="export_guest", new_password=guest,
                        request_token=secrets.token_hex(16))
        controller.ddl(f"GRANT EXECUTE ON ROUTINE {HOSTED} TO USER export_guest")
        refused = controller.raw("routine_export", routine=HOSTED, mode="standalone")
        detail = refused.get("error", {})
        require(not refused["ok"] and detail.get("line") == 3 and detail.get("column", 0) > 1,
                "Hosted code did not report its exact source location")
        assets = {
            "owner_hosted": metadata(controller, HOSTED), "owner_pure": metadata(controller, PURE),
            "unsupported_location": f"{detail['line']}:{detail['column']}",
        }
    with login(port, cert, "export_guest", guest) as observer:
        assets["guest_hosted"] = metadata(observer, HOSTED)
        refused = observer.raw("routine_export", routine=HOSTED, mode="connected")
        require(not refused["ok"] and refused.get("error", {}).get("code") == PERMISSION_DENIED and
                "body" not in refused and HIDDEN not in json.dumps(refused),
                "Restricted export returned dependency detail")
    return assets


def capture(stream, pending):
    for line in stream:
        pending.put(line)
    pending.put(None)


def evidence(pending, passwords, *, until_ready):
    captured = []
    end = time.monotonic() + 75
    while True:
        remaining = end - time.monotonic()
        require(remaining > 0, "Qt Java export fixture did not resolve")
        try:
            line = pending.get(timeout=remaining)
        except queue.Empty:
            raise RuntimeError("Qt Java export fixture did not resolve") from None
        if line is None or (until_ready and line.strip() == "READY"):
            require(not until_ready or line is not None,
                    "Qt Java export did not generate both artifacts\n" + "".join(captured))
            return "".join(captured)
        require(all(password not in line for password in passwords), "Fixture leaked a credential")
        captured.append(line)


def verify_shutdown(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            pass
    except OSError:
        return
    raise RuntimeError("Fixture Server is still reachable")


def verify_offline_result(root, jdk):
    source = root / "standalone-java" / "src" / "main" / "java"
    classes = root / "positive-classes"
    classes.mkdir()
    check = root / "CheckResult.java"
    check.write_text(
        "import stardataparadise.exported.Routines;\n"
        "public class CheckResult {\n"
        "  public static void main(String[] args) {\n"
        f'    Object result = Routines.invoke("main.public.{PURE}", 5L).returned();\n'
        "    if (!Long.valueOf(7L).equals(result)) throw new AssertionError(result);\n"
        '    System.out.println("Offline generated function returned 7");\n'
        "  }\n"
        "}\n", encoding="utf-8")
    environment = dict(os.environ)
    for name in ("JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS", "CLASSPATH"):
        environment.pop(name, None)
    java = jdk / "bin"
    run([java / "javac.exe", "--release", "17", "-encoding", "UTF-8", "-proc:none",
         "-d", classes, *sorted(source.rglob("*.java")), check], (), cwd=root, env=environment)
    print(run([java / "java.exe", "-cp", classes, "CheckResult"], (), cwd=root, env=environment).strip())


def exercise(args, root, cert, key):
    administrator = secrets.token_urlsafe(24)
    guest = secrets.token_urlsafe(24)
    passwords = (administrator, guest)
    client = None
    pending = queue.Queue()
    try:
        with instance(args, root / "server", cert, key, administrator) as port:
            assets = seed(port, cert, administrator, guest)
            assets.update(python=sys.executable, toolkit=str(args.toolkit), jdk=str(args.jdk),
                          jdbc=str(args.jdbc), gson=str(args.gson))
            profiles = [
                dict(name=label, user=user, host="127.0.0.1", port=port, ca_file=str(cert),
                     tls_name="localhost", database="main", environment="test", read_only=False)
                for label, user in (("Owner Java export", "local-admin"), ("Guest Java export", "export_guest"))
            ]
            profile_path, asset_path = root / "profiles.json", root / "assets.json"
            profile_path.write_text(json.dumps({"version": 1, "profiles": profiles}), encoding="utf-8")
            asset_path.write_text(json.dumps(assets), encoding="utf-8")
            client = subprocess.Popen([args.client, profile_path, asset_path], cwd=root, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                      errors="replace", creationflags=subprocess.CREATE_NO_WINDOW)
            threading.Thread(target=capture, args=(client.stdout, pending), daemon=True).start()
            client.stdin.write("\n".join((*passwords, "")))
            client.stdin.flush()
            online = evidence(pending, passwords, until_ready=True)

        verify_shutdown(port)
        client.stdin.write("GO\n")
        client.stdin.flush()
        client.stdin.close()
        offline = evidence(pending, passwords, until_ready=False)
        require(client.wait(timeout=10) == 0, "Qt export fixture failed\n" + online + offline)
        print(online + offline, end="")
        verify_shutdown(port)
        verify_offline_result(root, args.jdk)

        for artifact in (root / "standalone-java", root / "connected-java"):
            for file in artifact.rglob("*"):
                if file.is_file():
                    data = file.read_bytes()
                    require(all(password.encode() not in data for password in passwords),
                            "A generated Java artifact contains a fixture credential")
    finally:
        if client:
            if client.poll() is None:
                client.kill()
                client.wait(timeout=8)
            if not client.stdin.closed:
                client.stdin.close()
            client.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("client", "server", "openssl", "scratch", "toolkit", "jdk", "jdbc", "gson"):
        parser.add_argument("--" + option, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="java-export-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        exercise(args, root, cert, key)


if __name__ == "__main__":
    main()

