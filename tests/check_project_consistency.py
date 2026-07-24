#!/usr/bin/env python3

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def main() -> int:
    failures: list[str] = []
    cmake = read("CMakeLists.txt")
    version_match = re.search(
        r"project\(ChronoLab\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        cmake,
        re.DOTALL,
    )
    if not version_match:
        print("CMakeLists.txt: project version not found", file=sys.stderr)
        return 1

    version = version_match.group(1)
    required_version_occurrences = {
        "src/main.cpp": f'setApplicationVersion(QStringLiteral("{version}"))',
        "src/app/MainWindow.cpp": f"ChronoLab {version} — Open Timegrapher",
        "README.md": f"Current status:** {version}",
        "docs/ALGORITHM.md": f"ChronoLab {version} uses",
        "CHANGELOG.md": f"## {version} —",
    }
    for relative_path, expected in required_version_occurrences.items():
        if expected not in read(relative_path):
            failures.append(
                f"{relative_path}: missing current version marker {expected!r}"
            )

    main_window = read("src/app/MainWindow.cpp")
    if "m_liveDataTimer->setInterval(33);" not in main_window:
        failures.append("MainWindow.cpp: live-data interval is not 33 ms")
    if "m_liveDataTimer->setTimerType(Qt::PreciseTimer);" not in main_window:
        failures.append("MainWindow.cpp: live-data timer is not precise")
    if "m_analysisWatcher->isRunning()" not in main_window:
        failures.append("MainWindow.cpp: asynchronous DSP backpressure is missing")

    source_title = f"ChronoLab {version} — Open Timegrapher"
    source_footer = (
        f"ChronoLab {version} · GPL-3.0-or-later · "
        "Elaborazione locale, nessun dato inviato"
    )
    degraded_status = "Segnale temporaneamente instabile"
    for language in ("en", "fr", "de", "es"):
        path = ROOT / "translations" / f"chronolab_{language}.json"
        catalog = json.loads(path.read_text(encoding="utf-8"))
        messages = catalog.get("translations", {}).get("MainWindow", {})
        for source in (source_title, source_footer, degraded_status):
            if not messages.get(source):
                failures.append(
                    f"{path.name}: missing or empty translation for {source!r}"
                )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1

    print(
        f"ChronoLab {version} project consistency verified: "
        "version markers, MULTI5 live-lock strings, 33 ms precise timer "
        "and DSP backpressure."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
