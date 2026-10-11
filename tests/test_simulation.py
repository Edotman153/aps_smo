"""Запускает настоящий симулятор на пару секунд и проверяет инварианты результатов."""
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from smo_stats import parse_banks, parse_families  # noqa: E402

BINARIES = [ROOT / "build" / "smo", ROOT / "build" / "smo.exe", ROOT / "build" / "Release" / "smo.exe"]


@pytest.fixture(scope="module")
def results(tmp_path_factory):
    binary = next((b for b in BINARIES if b.exists()), None)
    if binary is None:
        pytest.skip("симулятор не собран: cmake -S . -B build && cmake --build build")
    out = tmp_path_factory.mktemp("out")
    # timeout: симулятор обязан завершиться сам
    done = subprocess.run([str(binary), str(out), "2", "--quiet"], capture_output=True, text=True, timeout=30)
    assert done.returncode == 0, done.stderr
    return parse_families(out / "familys.txt"), parse_banks(out / "banks.txt")


def test_all_sources_and_banks_present(results):
    families, banks = results
    assert [f.id for f in families] == [0, 1, 2, 3, 4]
    assert [b for b, _ in banks] == list(range(7))


def test_request_counts_are_consistent(results):
    families, _ = results
    for f in families:
        assert f.requests > 0
        assert f.requests == f.success + f.rejected
        assert len(f.wait) == f.success               # ожидание записано для каждой отправленной заявки
        assert f.success - 7 <= len(f.service) <= f.success   # недообслуженных не больше, чем приборов


def test_utilization_in_range(results):
    _, banks = results
    for _, u in banks:
        assert 0.0 <= u <= 1.0
