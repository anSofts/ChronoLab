#!/usr/bin/env python3

import ast
import json
import re
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTEXT_FILES = {
    "MainWindow": ROOT / "src/app/MainWindow.cpp",
    "AudioCapture": ROOT / "src/audio/AudioCapture.cpp",
    "QObject": ROOT / "src/audio/WavFile.cpp",
    "SignalPlotWidget": ROOT / "src/widgets/SignalPlotWidget.cpp",
    "TimegrapherPlotWidget": ROOT / "src/widgets/TimegrapherPlotWidget.cpp",
}
TRANSLATED_LANGUAGES = ("en", "fr", "de", "es")
TR_PATTERN = re.compile(
    r'(?:QObject::)?tr\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)', re.DOTALL
)
STRING_PATTERN = re.compile(r'"(?:\\.|[^"\\])*"')
PLACEHOLDER_PATTERN = re.compile(r"%\d+")


def source_messages(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    messages: set[str] = set()
    for match in TR_PATTERN.finditer(text):
        literals = STRING_PATTERN.findall(match.group(1))
        messages.add("".join(ast.literal_eval(literal) for literal in literals))
    return messages


def load_catalog(language: str) -> dict[str, dict[str, str]]:
    path = ROOT / "translations" / f"chronolab_{language}.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("language") != language:
        raise ValueError(f"{path.name}: invalid language metadata")
    return data.get("translations", {})


def main() -> int:
    catalogs = {language: load_catalog(language)
                for language in TRANSLATED_LANGUAGES}
    reference = catalogs["en"]
    failures: list[str] = []

    for context, source_path in CONTEXT_FILES.items():
        expected = source_messages(source_path)
        actual = set(reference.get(context, {}))
        for message in sorted(expected - actual):
            failures.append(f"en/{context}: missing source string {message!r}")
        for message in sorted(actual - expected):
            failures.append(f"en/{context}: obsolete source string {message!r}")

    reference_keys = {
        (context, source)
        for context, messages in reference.items()
        for source in messages
    }
    for language, catalog in catalogs.items():
        keys = {
            (context, source)
            for context, messages in catalog.items()
            for source in messages
        }
        for context, source in sorted(reference_keys - keys):
            failures.append(f"{language}/{context}: missing {source!r}")
        for context, source in sorted(keys - reference_keys):
            failures.append(f"{language}/{context}: unexpected {source!r}")

        for context, messages in catalog.items():
            for source, translated in messages.items():
                if not translated:
                    failures.append(f"{language}/{context}: empty {source!r}")
                if Counter(PLACEHOLDER_PATTERN.findall(source)) != Counter(
                        PLACEHOLDER_PATTERN.findall(translated)):
                    failures.append(
                        f"{language}/{context}: placeholders differ for {source!r}"
                    )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1

    print(
        f"ChronoLab MULTI5 catalogs verified: "
        f"{len(reference_keys)} messages × {len(TRANSLATED_LANGUAGES) + 1} languages."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
