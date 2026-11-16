"""Exercise ML Studio and durable jobs over an isolated local TLS Server."""

import argparse
import json
from pathlib import Path
import secrets
import tempfile

from workbench_sql_smoke import instance, run


def write_profile(path, port, cert):
    profile = dict(name="Workbench ML test", host="127.0.0.1", port=port, user="local-admin",
                   ca_file=str(cert), tls_name="localhost", database="main", environment="test",
                   read_only=False)
    path.write_text(json.dumps(dict(version=1, profiles=[profile])), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + flag, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ml-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        administrator = secrets.token_urlsafe(24)
        profile = root / "profiles.json"
        metadata = root / "fixture-state.json"
        recent = root / "workbench-jobs.json"

        def exercise(phase):
            print(run([args.client, profile, phase, metadata, recent], (administrator,),
                      cwd=root, input=administrator + "\n").strip())

        with instance(args, root / "server", cert, key, administrator) as port:
            write_profile(profile, port, cert)
            exercise("train")
            if not metadata.is_file() or not recent.is_file():
                raise RuntimeError("first process did not preserve checkpoint or Job Center identity")
            for artifact in (metadata, recent):
                if administrator in artifact.read_text(encoding="utf-8"):
                    raise RuntimeError("Workbench saved the fixture credential")
            exercise("reopen")
        with instance(args, root / "disabled-server", cert, key, administrator, mlp_cpu_enabled=False) as port:
            write_profile(profile, port, cert)
            exercise("disabled")


if __name__ == "__main__":
    main()
