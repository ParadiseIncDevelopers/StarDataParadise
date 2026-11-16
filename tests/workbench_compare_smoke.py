"""Exercise Compare Studio against two independent disposable TLS Servers."""

import argparse
from pathlib import Path
import secrets
import tempfile

from workbench_ml_smoke import write_profile
from workbench_sql_smoke import instance, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("client", "server", "openssl", "scratch"):
        parser.add_argument("--" + flag, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="compare-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        first = secrets.token_urlsafe(24)
        second = secrets.token_urlsafe(24)
        source, destination = root / "source.json", root / "destination.json"
        with instance(args, root / "source", cert, key, first) as source_port, \
                instance(args, root / "destination", cert, key, second) as destination_port:
            if source_port == destination_port:
                raise RuntimeError("compare fixture reused a TLS endpoint")
            write_profile(source, source_port, cert)
            write_profile(destination, destination_port, cert)
            print(run([args.client, source, destination], (first, second), cwd=root,
                      input=first + "\n" + second + "\n").strip())


if __name__ == "__main__":
    main()
