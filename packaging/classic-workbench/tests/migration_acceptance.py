"""Run Classic migration acceptance against a fresh disposable StarDataParadise Server.

No user profiles, saved connections or existing Server data are used.
Requires the sibling Server build and Classic wbcopytables; no ODBC driver.
"""

import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[3]
SERVER = REPO.parent / "StarDataParadiseServer"
sys.path.insert(0, str(SERVER))
from compatibility.tests.fixture import BASEDIR, BIN, SHELL, HIDDEN, Fixture, query, require, run
from compatibility.tests.workbench_native import ENVIRONMENT

PROBE = Path(__file__).with_name("migration_gui_probe.py")


def run_probe(executable, fixture, artifact_dir, timeout, source_charset=""):
    artifact_dir.mkdir(parents=True, exist_ok=True)
    with fixture.scratch("classic-home-migration") as root:
        with fixture.disposable(root / "database") as instance:
            env, classic, _ = instance
            port, uuid, version, datadir = query(instance, "SELECT @@port,@@server_uuid,@@version,@@datadir").split("\t")
            password = secrets.token_hex(24)
            source, target = "sdp_migration_source", "sdp_migration_target"
            init = """
                CREATE SCHEMA sdp_migration_source DEFAULT CHARACTER SET utf8mb4;
                CREATE TABLE sdp_migration_source.items(
                    id BIGINT PRIMARY KEY, label VARCHAR(64), cost DECIMAL(18,4),
                    payload BLOB, raw_key VARBINARY(16), notes TEXT, optional_value INT NULL) ENGINE=InnoDB;
                INSERT INTO sdp_migration_source.items VALUES
                    (9007199254740993,CONVERT(0xC4B07374616E62756C USING utf8mb4),123.4500,0x%s,
                     0x00275CFF,CONVERT(0xC4B07374616E62756C USING utf8mb4),NULL),
                    (9007199254740994,'Second row',-0.0100,0x00FF,0x414243,'plain text',42),
                    (9007199254740995,'',0.0000,X'',X'','',0);
                CREATE USER 'sdp_probe_source'@'localhost' IDENTIFIED BY '%s' REQUIRE SSL;
                CREATE USER 'sdp_probe_target'@'localhost' IDENTIFIED BY '%s' REQUIRE SSL;
                GRANT SELECT, SHOW VIEW ON sdp_migration_source.* TO 'sdp_probe_source'@'localhost';
                GRANT ALL ON sdp_migration_target.* TO 'sdp_probe_target'@'localhost';
            """ % (bytes(range(256)).hex(), password, password)
            initialized = run(classic, env, 20, script=init)
            require(initialized.returncode == 0, "Disposable migration fixture initialization failed")
            profile, temporary = root / "profile", root / "tmp"
            profile.mkdir()
            temporary.mkdir()
            result_path = root / "result.json"
            config_path = root / "config.json"
            config_path.write_text(json.dumps({
                "fixture": "sdp-disposable-migration-v1", "port": int(port), "uuid": uuid,
                "datadir": datadir, "version": version, "password": password,
                "source_user": "sdp_probe_source", "target_user": "sdp_probe_target",
                "source_charset": source_charset,
                "source_schema": source, "target_schema": target,
                "ca": str(root / "database/data/ca.pem"), "result": str(result_path),
            }), encoding="utf-8")
            process_env = {k: v for k, v in os.environ.items() if k.upper() in ENVIRONMENT}
            process_env.update(SDP_MIGRATION_PROBE_CONFIG=str(config_path), TEMP=str(temporary), TMP=str(temporary))
            command = [str(executable), "--configdir=" + str(profile), "--run-script=" + str(PROBE),
                       "--quit-when-done", "--log-level=warning"]
            log_path = root / "process.log"
            result = {"ok": False, "phase": "launch", "error": "No result was produced"}
            try:
                with log_path.open("xb") as output:
                    process = subprocess.Popen(command, cwd=executable.parent, env=process_env,
                                               stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                               creationflags=HIDDEN)
                    try:
                        deadline = time.monotonic() + timeout
                        while time.monotonic() < deadline:
                            if result_path.is_file():
                                try:
                                    result = json.loads(result_path.read_text(encoding="utf-8"))
                                    break
                                except (json.JSONDecodeError, PermissionError):
                                    pass
                            require(process.poll() is None, "Classic exited before publishing its probe result")
                            time.sleep(0.2)
                        else:
                            raise RuntimeError("Migration acceptance timed out (inspect the process log)")
                        require(result.get("ok") is True,
                                "Migration acceptance failed at %s: %s" % (result.get("phase"), result.get("error")))
                        require(process.wait(timeout=30) == 0, "Classic did not exit cleanly")
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            process.wait(timeout=15)
                require(query(instance, "SELECT COUNT(*) FROM sdp_migration_target.items_copy") == "3",
                        "Independent CLI verification of copied rows failed")
                require(query(instance, "SELECT id,cost,HEX(label),HEX(payload),HEX(raw_key),HEX(notes),IFNULL(optional_value,'NULL') "
                                        "FROM sdp_migration_source.items ORDER BY id") ==
                        query(instance, "SELECT id,cost,HEX(display_name),HEX(payload),HEX(raw_key),HEX(notes),IFNULL(optional_value,'NULL') "
                                        "FROM sdp_migration_target.items_copy ORDER BY id"),
                        "Independent CLI source/target typed data comparison failed")
                result["independent_cli_verified"] = True
                print("PASS native wizard source -> selection -> mapping -> preview (zero writes) -> schema -> transfer")
                print("PASS native wbcopytables BIGINT/DECIMAL/UTF8/BLOB/NULL and unchanged source; no ODBC needed")
            except Exception as error:
                result["runner_error"] = str(error).replace(password, "<redacted>")
                raise
            finally:
                config_path.unlink(missing_ok=True)
                (artifact_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
                if log_path.exists():
                    (artifact_dir / "process.log").write_text(
                        log_path.read_text(encoding="utf-8", errors="replace").replace(password, "<redacted>"),
                        encoding="utf-8")
                for log in (profile / "log").glob("*.log"):
                    (artifact_dir / log.name).write_text(log.read_text(encoding="utf-8", errors="replace")
                                                       .replace(password, "<redacted>"), encoding="utf-8")
                print("MIGRATION_ACCEPTANCE_RESULT " + str(artifact_dir / "result.json"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path, help="Built StarDataParadise Classic executable")
    parser.add_argument("--mysqld", type=Path, default=BIN / "mysqld.exe")
    parser.add_argument("--mysql", type=Path, default=BIN / "mysql.exe")
    parser.add_argument("--basedir", type=Path, default=BASEDIR)
    parser.add_argument("--artifacts", type=Path, default=REPO / "out/mysql-migration/home-ui-migration-acceptance")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--source-charset", default="", choices=("", "utf8mb4"),
                        help="Optional explicit source charset; default uses the normal native connection settings")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    require((exe.parent / "wbcopytables.exe").is_file(), "Classic package is missing wbcopytables.exe")
    fixture = Fixture(args.mysqld.resolve(strict=True), args.mysql.resolve(strict=True),
                      args.basedir.resolve(strict=True), SHELL, None)
    run_probe(exe, fixture, args.artifacts.resolve(), args.timeout, args.source_charset)


if __name__ == "__main__":
    main()
