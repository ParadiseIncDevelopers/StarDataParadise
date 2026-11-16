"""Verify Workbench administration against disposable source and restored TLS instances."""

import argparse
import json
from pathlib import Path
import secrets
import tempfile
import time

from workbench_ml_smoke import write_profile
from workbench_routine_smoke import login
from workbench_sql_smoke import instance, run


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def log_fixture(log_dir):
    probe = "synthetic-private-" + secrets.token_hex(16)
    correlation = secrets.token_hex(16)
    records = [
        {"at_utc": 1767225600, "event": "listening", "password": probe, "correlation_id": correlation},
        {"at_utc": 1767225601, "event": probe, "correlation_id": correlation},
    ]
    (log_dir / "private-fixture.txt").write_text(probe, encoding="utf-8")
    with (log_dir / "operational.jsonl").open("a", encoding="utf-8") as log:
        for record in records:
            log.write(json.dumps(record, separators=(",", ":")) + "\n")
        log.flush()
    return probe, correlation


def validate_redaction(root, instance_dir, administrator, secrets_):
    server_bundle = (instance_dir / "tmp" / "support" / "wb-redacted" / "bundle.json").read_text(encoding="utf-8")
    local_summary = (root / "support-summary.json").read_text(encoding="utf-8")
    exported_profiles = (root / "roundtrip-profiles.json").read_text(encoding="utf-8")
    for artifact in (server_bundle, local_summary, exported_profiles):
        require(all(needle not in artifact for needle in
                    (administrator, *secrets_, "PRIVATE KEY", "private-fixture.txt", "admin-v1.json")),
                "support or profile output disclosed a fixture secret")
    entries = json.loads(server_bundle)["logs"]["operational"]
    markers = [record for record in entries if record["at_utc"] in (1767225600, 1767225601)]
    require(len(markers) == 2 and markers[0]["event"] == "listening" and markers[1]["event"] == "redacted_event"
            and markers[0]["correlation"] == markers[1]["correlation"],
            "server bundle did not retain safe evidence and redact fixture log values")
    require(json.loads(local_summary)["secret_scan_passed"] is True,
            "local support summary omitted the observed redaction result")
    require(len(json.loads(exported_profiles)["profiles"]) == 1, "profile round trip lost the safe profile")
    print("S42-T03: sanitized evidence preserved without disclosing any planted secret or credential")


def prepare_scheduled_recipes(port, cert, owner_password):
    with login(port, cert, "local-admin", owner_password) as owner:
        owner.ddl("CREATE TABLE wb_scheduled_sql (id BIGINT PRIMARY KEY)")
        owner.ddl("CREATE TABLE wb_scheduled_train (id BIGINT PRIMARY KEY, feature BIGINT, label BIGINT)")
        rows = ",".join(f"({i},{i},{3*i+7})" for i in range(1, 31))
        owner.ddl("INSERT INTO wb_scheduled_train VALUES " + rows)
        dataset = owner.call("dataset_create", request_token=secrets.token_hex(16),
                             sql="SELECT id, feature, label FROM wb_scheduled_train ORDER BY id",
                             options={"features": [{"column": "feature", "transform": "numeric", "impute": "reject"}],
                                      "target": "label", "target_kind": "numeric",
                                      "split": {"mode": "time", "time": "id", "train": 60, "validation": 20}})
        owner.call("model_create", request_token=secrets.token_hex(16), name="wb_scheduled_model", algorithm="linear")
        return dataset["dataset_id"]


def await_closed_schedules(port, cert, owner_password, fixture):
    with login(port, cert, "local-admin", owner_password) as observer:
        capabilities = observer.call("job_capabilities")
        unavailable = set(capabilities["unavailable_kinds"])
        supported = set(capabilities["schedule_kinds"])
        require(not {"sql_script", "retrain"} & unavailable and
                {"sql_script", "retrain", "compute", "maintenance", "backup"} <= supported,
                "running Server did not report the expected schedule capabilities")
        schedules = {key: fixture[key + "schedule_id"] for key in ("", "sql_", "retrain_")}
        pending = dict(schedules)
        for schedule in schedules.values():
            first = observer.call("job_schedule_status", schedule_id=schedule)
            require(not first["last_job_id"], "a scheduled run already existed when the first Workbench closed")
        end = time.monotonic() + 25
        while time.monotonic() < end:
            for key, schedule in list(pending.items()):
                info = observer.call("job_schedule_status", schedule_id=schedule)
                job_id = info["last_job_id"]
                if not job_id:
                    continue
                result = observer.call("job_status", job_id=job_id)
                require(result["state"] not in {"failed", "cancelled"}, f"{key} schedule failed: {result['reason']}")
                if result["state"] != "succeeded":
                    continue
                output = result["result"]
                if key == "":
                    require(output["count"] == 42000, "schedule ran the wrong computation")
                elif key == "sql_":
                    require(output["statements"] == 2 and output["affected_rows"] == 2,
                            "SQL schedule did not apply the complete script")
                else:
                    require(output["dataset_id"] == fixture["dataset_id"] and output["version"] == 1,
                            "retrain schedule did not publish a version for the pinned dataset")
                    registered = observer.call("model_status", name="wb_scheduled_model")
                    require(registered["active_version"] is None and len(registered["versions"]) == 1,
                            "retrain changed activation or duplicated a version")
                fixture[key + "observed_job_id"] = job_id
                del pending[key]
            if not pending:
                print("Computation, SQL script and model retraining succeeded while Workbench was closed")
                return
            time.sleep(0.1)
    raise RuntimeError("Server scheduler did not run after the first Workbench exited")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("client", "server", "openssl", "scratch", "cli"):
        parser.add_argument("--" + flag, required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="admin-", dir=args.scratch) as directory:
        root = Path(directory)
        conf = root / "openssl.cnf"
        conf.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="ascii")
        cert, key = root / "server.pem", root / "server.key"
        run([args.openssl, "req", "-config", str(conf), "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", str(key), "-out", str(cert), "-days", "2", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1", "-addext", "extendedKeyUsage=serverAuth"], ())
        first_password = secrets.token_urlsafe(24)
        restored_password = secrets.token_urlsafe(24)
        state = root / "scope.json"
        source = root / "source"
        profile = root / "profiles.json"

        def exercise(phase, password):
            print(run([args.client, profile, phase, state, args.cli], (password,),
                      cwd=root, input=password + "\n").strip())

        with instance(args, source, cert, key, first_password, backup_enabled=True) as port:
            write_profile(profile, port, cert)
            planted = log_fixture(source / "logs")
            state.write_text(json.dumps({"dataset_id": prepare_scheduled_recipes(port, cert, first_password)}),
                             encoding="utf-8")
            exercise("source", first_password)
            fixture = json.loads(state.read_text(encoding="utf-8"))
            await_closed_schedules(port, cert, first_password, fixture)
            state.write_text(json.dumps(fixture), encoding="utf-8")
            exercise("reopen", first_password)

            validate_redaction(root, source, first_password, planted)
            extent = (source / "tmp" / "restores" / "wb-verified").resolve(strict=True)
            require(extent.is_relative_to(source) and (extent / "store.sdp").is_file() and
                    not (extent / "admin-v1.json").exists() and not (extent / "server.key").exists(),
                    "isolated restore output included credentials or omitted the store")
            with instance(args, root / "restored", cert, key, restored_password, restored_data=extent) as restored_port:
                require(restored_port != port, "restored fixture reused the source Server endpoint")
                write_profile(profile, restored_port, cert)
                exercise("restored", restored_password)


if __name__ == "__main__":
    main()
