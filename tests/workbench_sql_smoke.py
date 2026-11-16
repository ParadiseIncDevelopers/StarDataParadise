"""Run SQL Workbench acceptance against two disposable TLS Server instances."""

import argparse
from contextlib import ExitStack, contextmanager
import json
from pathlib import Path
import queue
import secrets
import subprocess
import tempfile
import threading


def run(command, passwords, expected=0, **options):
    result = subprocess.run(command, text=True, capture_output=True, encoding="utf-8",
                            errors="replace", timeout=90, **options)
    for password in passwords:
        if password in result.stdout or password in result.stderr:
            raise RuntimeError("fixture credential appeared in process output")
    if result.returncode != expected:
        raise RuntimeError(f"{Path(command[0]).name} exited {result.returncode}\n{result.stdout}{result.stderr}")
    return result.stdout


@contextmanager
def instance(args, root, cert, key, password, *, routine_test_instance=False, mlp_cpu_enabled=True,
             backup_enabled=False, restored_data=None):
    for name in ("data", "logs", "tmp"):
        (root / name).mkdir(parents=True)
    config = root / "server.json"
    settings = {"data_dir": "data", "log_dir": "logs", "temp_dir": "tmp",
                "tls_certificate": str(cert), "tls_private_key": str(key), "port": 0}
    if routine_test_instance:
        settings["routine_test_instance"] = True
    if not mlp_cpu_enabled:
        settings["mlp_cpu_enabled"] = False
    if backup_enabled:
        (root / "backups").mkdir()
        settings["backup"] = "backups"
    if restored_data is not None:
        settings["data_dir"] = str(restored_data.resolve(strict=True))
    config.write_text(json.dumps(settings), encoding="utf-8")
    command = [str(args.server), "--config", str(config)]
    run([*command, "--initialize-storage" if restored_data is None else "--check-storage"], (password,))
    run([*command, "--bootstrap-admin", "local-admin"], (password,), input=(password + "\n") * 2)
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          creationflags=subprocess.CREATE_NO_WINDOW) as server:
        ready = queue.Queue(maxsize=1)
        threading.Thread(target=lambda: ready.put(server.stdout.readline()), daemon=True).start()
        try:
            words = ready.get(timeout=12).split()
            if len(words) != 3 or words[:2] != ["LISTENING", "127.0.0.1"]:
                raise RuntimeError("SQL fixture Server did not bind loopback")
            yield int(words[2])
        finally:
            if server.poll() is None:
                server.terminate()
            server.communicate(timeout=8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + key, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sql-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        secrets_ = [secrets.token_urlsafe(24), secrets.token_urlsafe(24)]
        with ExitStack() as lifetime:
            ports = [lifetime.enter_context(instance(args, root / name, cert, key, password))
                     for name, password in zip(("one", "two"), secrets_, strict=True)]
            profiles = [dict(name=f"Workbench SQL fixture {n}", host="127.0.0.1", port=port,
                             user="local-admin", ca_file=str(cert), tls_name="localhost",
                             database="main", environment="test", read_only=False)
                        for n, port in enumerate(ports, start=1)]
            profile_path = root / "profiles.json"
            profile_path.write_text(json.dumps(dict(version=1, profiles=profiles)), encoding="utf-8")
            settings = root / "workspace.json"
            command = [str(args.client), str(profile_path), str(settings)]
            credentials = json.dumps(dict(first=secrets_[0], second=secrets_[1])) + "\n"
            evidence = run([*command, "exercise"], secrets_, cwd=root, input=credentials)
            print(evidence.strip())
            run([*command, "crash"], secrets_, expected=77, cwd=root, input=credentials)
            saved = Path(str(settings) + ".sql-drafts.json").read_text(encoding="utf-8")
            if any(password in saved for password in secrets_) or '"parameters"' in saved:
                raise RuntimeError("SQL recovery file contains credentials or bind values")
            evidence = run([*command, "restore"], secrets_, cwd=root, input=credentials)
            print(evidence.strip())


if __name__ == "__main__":
    main()
