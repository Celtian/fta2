"""Run isolated logging and fatal-process tests without proprietary data."""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--emulator", nargs="*", default=[])
    args = parser.parse_args()
    if os.name != "nt":
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    temporary_root = None
    cross_windows = args.emulator and args.binary.suffix == ".exe" and os.name != "nt"
    if cross_windows:
        # Keep intensive unbuffered I/O on the native Windows filesystem under WSL.
        windows_temp = subprocess.check_output([
            "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe", "-NoProfile",
            "-Command", "[IO.Path]::GetTempPath()"], text=True).strip()
        temporary_root = subprocess.check_output(["wslpath", "-u", windows_temp], text=True).strip()
    with tempfile.TemporaryDirectory(prefix="fta2-diagnostics-", dir=temporary_root, ignore_cleanup_errors=True) as temp:
        work = Path(temp) / "logs with spaces žluťoučký"
        work.mkdir()
        def run(mode, directory=work, success=True):
            path = str(directory)
            if args.emulator and args.binary.suffix == ".exe" and os.name != "nt":
                path = subprocess.check_output(["wslpath", "-w", path], text=True).strip()
            result = subprocess.run([*args.emulator, str(args.binary), mode, path],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            assert (result.returncode == 0) == success, (mode, result.returncode, result.stderr)
            return result
        run("normal")
        logs = sorted(work.glob("*.log"))
        assert len(logs) == 1
        text = logs[0].read_text()
        assert all(message in text for message in ("synthetic-test", "ordinary message 42",
                   "synthetic warning", "synthetic SDL warning", "Session logging closed"))
        assert not list(work.glob("*.dmp")), "Clean exit produced a crash dump"
        for i in range(15):
            (work / f"fta2-19990101-000000-{i:02d}.log").write_text("old")
        run("normal")
        assert len(list(work.glob("*.log"))) == 10
        blocked = Path(temp) / "regular-file"
        blocked.write_text("unchanged")
        run("unwritable", blocked)
        assert blocked.read_text() == "unchanged"
        limit = Path(temp) / "limit"
        run("limit", limit)
        log = next(limit.glob("*.log"))
        assert log.stat().st_size < 65536 + 10000
        assert "fatal after size limit" in log.read_text()
        native = b"native-handlers" in run("capabilities").stdout
        for mode in ("fault", "abort", "terminate"):
            if mode in ("fault", "abort") and not native:
                continue  # ASan retains its own POSIX signal handlers; terminate still logs.
            directory = Path(temp) / mode
            run(mode, directory, False)
            text = next(directory.glob("*.log")).read_text()
            assert "FATAL:" in text and "synthetic operation" in text
            assert "actor=32768" in text and "u=13258" in text
            if mode == "terminate":
                assert "synthetic uncaught exception" in text
            if args.binary.suffix == ".exe":
                dumps = list(directory.glob("*.dmp"))
                assert len(dumps) == 1 and dumps[0].read_bytes()[:4] == b"MDMP", text
        print("Persistent log, retention, Unicode paths, size bound and fatal-process checks passed")


if __name__ == "__main__":
    main()
