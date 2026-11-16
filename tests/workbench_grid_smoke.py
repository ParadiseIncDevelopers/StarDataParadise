"""Run a Workbench table acceptance executable with a disposable loopback TLS Server."""

import argparse
import json
from pathlib import Path
import secrets
import tempfile

from process_memory import run_measured
from workbench_sql_smoke import instance, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + key, required=True, type=lambda value: Path(value).resolve())
    parser.add_argument("--metrics", type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="table-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        password = secrets.token_urlsafe(24)
        with instance(args, root / "server", cert, key, password) as port:
            profile = dict(name="Workbench table test", host="127.0.0.1", port=port, user="local-admin",
                           ca_file=str(cert), tls_name="localhost", database="main", environment="test",
                           read_only=False)
            profile_path = root / "profiles.json"
            profile_path.write_text(json.dumps(dict(version=1, profiles=[profile])), encoding="utf-8")
            command = [args.client, profile_path]
            if args.metrics:
                evidence = run_measured(command, password, args.metrics, cwd=root)
            else:
                evidence = run(command, (password,), cwd=root, input=password + "\n")
            print(evidence.strip())


if __name__ == "__main__":
    main()

