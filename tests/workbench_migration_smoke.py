"""Run Migration Studio against fresh PostgreSQL, MySQL and StarDataParadise TLS instances."""

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time

from workbench_ml_smoke import write_profile
from workbench_sql_smoke import instance, run


DATABASE = "migration_source"
READER = "fixture_reader"
AMOUNT = "123456789012345678901234567.12345678901"
CAPTION = "İzmir – Şırnak"


def make_certificates(openssl, root):
    config = root / "openssl.cnf"
    config.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
    extensions = root / "server.ext"
    extensions.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                          "extendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n", encoding="ascii")
    ca, ca_key = root / "ca.pem", root / "ca.key"
    cert, key, request = root / "server.pem", root / "server.key", root / "server.csr"
    run([openssl, "req", "-config", config, "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
         "-subj", "/CN=Migration Fixture CA", "-addext", "basicConstraints=critical,CA:TRUE",
         "-addext", "keyUsage=critical,keyCertSign,cRLSign", "-out", ca, "-keyout", ca_key], ())
    run([openssl, "req", "-config", config, "-newkey", "rsa:2048", "-nodes",
         "-subj", "/CN=localhost", "-out", request, "-keyout", key], ())
    run([openssl, "x509", "-req", "-in", request, "-CA", ca, "-CAkey", ca_key, "-CAcreateserial",
         "-days", "2", "-extfile", extensions, "-out", cert], ())
    return ca, cert, key


def loopback_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


@contextmanager
def database_process(command, directory, port, shutdown, passwords):
    output = directory / "server.log"
    with output.open("wb") as log, subprocess.Popen(
            command, cwd=directory, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW) as process:
        ready = False
        try:
            deadline = time.monotonic() + 35
            while process.poll() is None and time.monotonic() < deadline:
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                        ready = True
                    break
                except OSError:
                    time.sleep(0.1)
            if not ready:
                detail = output.read_text(encoding="utf-8", errors="replace")[-3500:]
                for password in passwords:
                    detail = detail.replace(password, "[removed]")
                raise RuntimeError(f"{Path(command[0]).name} did not start on its fixture endpoint\n{detail}")
            yield
        finally:
            if process.poll() is None:
                try:
                    shutdown()
                    process.wait(timeout=8)
                except (RuntimeError, OSError, subprocess.TimeoutExpired):
                    process.terminate()
                    process.wait(timeout=10)


def fixture_tables(timestamp_type):
    customers = []
    captions = {1: f"'{CAPTION}'", 2: "NULL", 3: "''"}
    for key in range(1, 97):
        caption = captions.get(key, f"'source row {key}'")
        customers.append(f"({key},{caption},{AMOUNT},'2026-01-12 08:43:00')")
    invoices = ",".join(f"({key},{key},45.01000000001)" for key in range(1, 9))
    return f"""
        CREATE TABLE customer (
            id BIGINT PRIMARY KEY,
            caption VARCHAR(100),
            amount DECIMAL(38,11) NOT NULL,
            observed_at {timestamp_type} NOT NULL
        );
        CREATE TABLE invoice (
            id BIGINT PRIMARY KEY,
            customer_id BIGINT NOT NULL,
            amount DECIMAL(38,11) NOT NULL,
            CONSTRAINT invoice_customer FOREIGN KEY (customer_id) REFERENCES customer(id)
        );
        INSERT INTO customer VALUES {",".join(customers)};
        INSERT INTO invoice VALUES {invoices};
        CREATE VIEW customer_view AS SELECT id, caption FROM customer;
    """


def source_settings(provider, port, schema, driver, ca):
    return dict(provider=provider, host="localhost", port=port, database=DATABASE,
                schema=schema, username=READER, driver=str(driver), ca=str(ca))


@contextmanager
def postgres(args, root, ca, cert, key, password, reader):
    root.mkdir()
    data = root / "data"
    programs = args.postgresql / "bin"
    password_file = root / "initial-password"
    password_file.write_text(password + "\n", encoding="ascii")
    try:
        run([programs / "initdb.exe", "--pgdata", data, "--username", "fixture_admin",
             "--auth", "scram-sha-256", "--encoding", "UTF8", "--locale", "C", "--pwfile", password_file],
            (password,))
    finally:
        password_file.unlink()

    port = loopback_port()
    command = [programs / "postgres.exe", "-D", data, "-h", "127.0.0.1", "-p", str(port), "-c", "ssl=on",
               "-c", f"ssl_cert_file={cert}", "-c", f"ssl_key_file={key}", "-c", "timezone=UTC"]

    def stop():
        run([programs / "pg_ctl.exe", "stop", "-D", data, "-m", "immediate", "-w"], (password,))

    def sql(query, database=DATABASE):
        env = {key: value for key, value in os.environ.items() if not key.upper().startswith("PG")}
        env.update(PGPASSWORD=password, PGSSLMODE="verify-full", PGSSLROOTCERT=str(ca),
                   PGHOST="localhost", PGHOSTADDR="127.0.0.1", PGPORT=str(port), PGCLIENTENCODING="UTF8")
        return run([programs / "psql.exe", "-X", "--no-password", "--quiet", "--tuples-only", "--no-align",
                    "--set", "ON_ERROR_STOP=1", "--username", "fixture_admin", "--dbname", database],
                   (password, reader), env=env, input=query)

    with database_process(command, root, port, stop, (password, reader)):
        sql(f"CREATE DATABASE {DATABASE};", database="postgres")
        sql(f"""
            {fixture_tables("TIMESTAMP WITH TIME ZONE")}
            CREATE FUNCTION customer_function() RETURNS BIGINT LANGUAGE SQL AS $$ SELECT 1::bigint; $$;
            CREATE FUNCTION customer_changed() RETURNS TRIGGER LANGUAGE plpgsql AS $$ BEGIN RETURN NEW; END; $$;
            CREATE TRIGGER customer_audit BEFORE UPDATE ON customer FOR EACH ROW EXECUTE FUNCTION customer_changed();
            CREATE USER {READER} WITH PASSWORD '{reader}';
            GRANT USAGE ON SCHEMA public TO {READER};
            GRANT SELECT ON ALL TABLES IN SCHEMA public TO {READER};
        """)
        release = run([programs / "postgres.exe", "--version"], ()).strip()
        yield source_settings("postgresql", port, "public", programs / "libpq.dll", ca), sql, release


@contextmanager
def mysql(args, root, ca, cert, key, password, reader):
    root.mkdir()
    data = root / "data"
    programs = args.mysql / "bin"
    daemon = [programs / "mysqld.exe", "--no-defaults", f"--basedir={args.mysql}", f"--datadir={data}"]
    run([*daemon, "--initialize-insecure"], (password, reader))
    port = loopback_port()
    command = [*daemon, "--console", "--bind-address=127.0.0.1", f"--port={port}", "--mysqlx=OFF",
               "--require-secure-transport=ON", f"--ssl-ca={ca}", f"--ssl-cert={cert}", f"--ssl-key={key}",
               "--default-time-zone=+00:00"]
    client = ["--no-defaults", "--protocol=TCP", "--host=localhost", f"--port={port}", "--user=root",
              "--ssl-mode=VERIFY_IDENTITY", f"--ssl-ca={ca}"]
    authenticated = False
    environment = {key: value for key, value in os.environ.items() if not key.upper().startswith("MYSQL")}
    environment["MYSQL_TEST_LOGIN_FILE"] = str(root / "fixture-login.cnf")

    def stop():
        options = [] if authenticated else ["--skip-password"]
        run([programs / "mysqladmin.exe", *client, *options, "shutdown"], (password, reader),
            env=dict(environment, MYSQL_PWD=password if authenticated else ""))

    def sql(query):
        options = [] if authenticated else ["--skip-password"]
        return run([programs / "mysql.exe", *client, *options, "--default-character-set=utf8mb4",
                    "--batch", "--skip-column-names"], (password, reader), input=query,
                   env=dict(environment, MYSQL_PWD=password if authenticated else ""))

    with database_process(command, root, port, stop, (password, reader)):
        sql(f"ALTER USER 'root'@'localhost' IDENTIFIED BY '{password}';")
        authenticated = True
        sql(f"""
            CREATE DATABASE {DATABASE} CHARACTER SET utf8mb4;
            USE {DATABASE};
            {fixture_tables("TIMESTAMP")}
            CREATE FUNCTION customer_function() RETURNS BIGINT DETERMINISTIC NO SQL RETURN 1;
            CREATE TRIGGER customer_audit BEFORE UPDATE ON customer FOR EACH ROW SET NEW.caption = NEW.caption;
            CREATE USER '{READER}'@'localhost' IDENTIFIED BY '{reader}';
            GRANT SELECT, SHOW VIEW ON {DATABASE}.* TO '{READER}'@'localhost';
            GRANT SHOW_ROUTINE ON *.* TO '{READER}'@'localhost';
        """)
        release = run([programs / "mysqld.exe", "--version"], ()).strip()
        yield source_settings("mysql", port, DATABASE, args.mysql / "lib" / "libmysql.dll", ca), sql, release


def exercise(args, parent, destination_credentials, source, provider):
    ca, cert, key = destination_credentials
    password, source_admin, source_reader = [secrets.token_urlsafe(28) for _ in range(3)]
    secrets_used = (password, source_admin, source_reader)
    with source(args, parent / provider, ca, cert, key, source_admin, source_reader) as (settings, update, version):
        with instance(args, parent / f"{provider}-destination", cert, key, password) as port:
            profile = parent / f"{provider}-profile.json"
            write_profile(profile, port, ca)
            credentials = parent / f"{provider}-source.json"
            credentials.write_text(json.dumps(settings), encoding="utf-8")

            def smoke(phase):
                result = run([args.client, profile, credentials, phase], secrets_used, cwd=parent,
                             input=f"{password}\n{source_reader}\n")
                if result:
                    print(result.strip(), flush=True)

            print(f"Real TLS source: {version}", flush=True)
            smoke("initial")
            smoke("resume")
            checkpoint = parent / f"{provider}-stale_journal.json"
            saved = checkpoint.read_bytes()
            if any(secret.encode("ascii") in saved for secret in secrets_used):
                raise RuntimeError("fixture credential appeared in the persisted migration checkpoint")
            table = f"{DATABASE}.customer" if provider == "mysql" else "public.customer"
            update(f"UPDATE {table} SET caption='edited after pause' WHERE id=96;")
            smoke("changed")
            if checkpoint.read_bytes() != saved:
                raise RuntimeError("changed source rewrote the persisted migration checkpoint")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client", "server", "openssl", "scratch", "postgresql", "mysql"):
        parser.add_argument("--" + key, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mig-", dir=args.scratch) as directory:
        root = Path(directory)
        credentials = make_certificates(args.openssl, root)
        exercise(args, root, credentials, postgres, "postgresql")
        exercise(args, root, credentials, mysql, "mysql")


if __name__ == "__main__":
    main()
