"""Run the Workbench UI against two real loopback instances with one-use credentials."""

import argparse
from contextlib import ExitStack, contextmanager
import json
from pathlib import Path
import queue
import secrets
import subprocess
import tempfile
import threading


def run(command, **options):
    result = subprocess.run(command, text=True, capture_output=True, encoding="utf-8",
                            errors="replace", timeout=35, **options)
    if result.returncode:
        raise RuntimeError(f"{Path(command[0]).name} failed\n{result.stderr}")
    return result.stdout


@contextmanager
def instance(args, root, certificate, key, password):
    for directory in ("data", "logs", "tmp"):
        (root / directory).mkdir(parents=True)
    config = root / "server.json"
    config.write_text(json.dumps(dict(data_dir="data", log_dir="logs", temp_dir="tmp",
                                      tls_certificate=str(certificate), tls_private_key=str(key), port=0)), encoding="utf-8")
    command = [args.server, "--config", str(config)]
    run([*command, "--bootstrap-admin", "local-admin"], input=(password + "\n") * 2)
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          creationflags=subprocess.CREATE_NO_WINDOW) as server:
        ready = queue.Queue(maxsize=1)
        threading.Thread(target=lambda: ready.put(server.stdout.readline()), daemon=True).start()
        try:
            words = ready.get(timeout=10).split()
            if len(words) != 3 or words[:2] != ["LISTENING", "127.0.0.1"]:
                raise RuntimeError("server did not bind loopback")
            yield int(words[2])
        finally:
            if server.poll() is None:
                server.terminate()
            server.communicate(timeout=6)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + key, required=True, type=Path)
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wb-", dir=args.scratch.resolve()) as folder:
        root = Path(folder)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"])
        passwords = [secrets.token_urlsafe(24), secrets.token_urlsafe(24)]
        with ExitStack() as lifetime:
            ports = [lifetime.enter_context(instance(args, root / name, cert, key, secret))
                     for name, secret in zip(("one", "two"), passwords, strict=True)]
            profiles = [
                dict(name=f"Fixture-{port}-{root.name}", host=host, port=port, user="local-admin", ca_file=str(cert),
                     tls_name="localhost", database="sample", environment="test", read_only=bool(index))
                for index, (port, host) in enumerate(zip(ports, ("localhost", "127.0.0.1"), strict=True))
            ]
            valid = root / "workspace.json"
            valid.write_text(json.dumps(dict(version=1, profiles=profiles)), encoding="utf-8")
            invalid = root / "invalid.json"
            invalid.write_text(json.dumps(dict(version=1, profiles=[{**profiles[0], "script": "invalid"}])), encoding="utf-8")
            output = root / "exported.json"
            evidence = run([args.client, str(valid), str(output), str(invalid)], cwd=root,
                           input=json.dumps(dict(first=passwords[0], second=passwords[1])) + "\n")
            contents = output.read_text(encoding="utf-8") + evidence
            if any(value in contents for value in passwords):
                raise RuntimeError("test credentials appeared in the profile or log")
            print(evidence.strip())


if __name__ == "__main__":
    main()
