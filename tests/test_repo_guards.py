"""Repo-level guards for CLAUDE.md rules that can be checked statically."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_raw_data_is_gitignored() -> None:
    """Rule 7: raw recordings under data/ never get committed."""
    lines = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
    assert "/data/*" in lines
    assert "!/data/README.md" in lines


def test_precommit_blocks_raw_data() -> None:
    config = (ROOT / ".pre-commit-config.yaml").read_text(encoding="utf-8")
    assert "id: forbid-raw-data" in config
