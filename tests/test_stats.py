import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from smo_stats import parse_banks, parse_families  # noqa: E402

FAMILYS = """\
USER 0 statistic: ALL: 10 success: 8 declined: 2
waitDurations: 100 200 300 400
Processing durations: 10 20 30
USER 1 statistic: ALL: 0 success: 0 declined: 0
waitDurations:{sp}
Processing durations:{sp}
""".replace("{sp}", " ")  # симулятор пишет пустой список как «метка: » с пробелом


def test_parse_families(tmp_path):
    f = tmp_path / "familys.txt"
    f.write_text(FAMILYS, encoding="utf-8")
    first, second = parse_families(f)

    assert (first.id, first.requests, first.success, first.rejected) == (0, 10, 8, 2)
    assert first.wait == [100, 200, 300, 400]
    assert first.service == [10, 20, 30]
    assert first.rejection_probability == pytest.approx(0.2)


def test_means_and_spreads_are_not_swapped(tmp_path):
    """Регрессия: Дбп (ожидание) и Добсл (обслуживание) когда-то менялись местами."""
    f = tmp_path / "familys.txt"
    f.write_text(FAMILYS, encoding="utf-8")
    first, _ = parse_families(f)

    assert first.mean_wait == pytest.approx(250)
    assert first.mean_service == pytest.approx(20)
    assert first.mean_total == pytest.approx(270)
    assert first.spread_wait() == pytest.approx(111.803, abs=1e-3)       # std [100..400]
    assert first.spread_service() == pytest.approx(8.165, abs=1e-3)      # std [10,20,30]
    assert first.spread_wait(variance=True) == pytest.approx(12500)
    assert first.spread_service(variance=True) == pytest.approx(66.667, abs=1e-3)


def test_source_without_requests_does_not_crash(tmp_path):
    f = tmp_path / "familys.txt"
    f.write_text(FAMILYS, encoding="utf-8")
    _, empty = parse_families(f)

    assert empty.rejection_probability == 0.0
    assert empty.mean_total == 0.0
    assert empty.spread_wait() == 0.0


def test_parse_families_rejects_garbage(tmp_path):
    f = tmp_path / "familys.txt"
    f.write_text("hello\nworld\n!\n", encoding="utf-8")
    with pytest.raises(ValueError, match="заголовок"):
        parse_families(f)

    f.write_text("USER 0 statistic: ALL: 1 success: 1 declined: 0\n", encoding="utf-8")
    with pytest.raises(ValueError, match="кратно трём"):
        parse_families(f)


def test_parse_banks(tmp_path):
    f = tmp_path / "banks.txt"
    f.write_text("Bank 1 0.5\nBank 0 0.857114\n", encoding="utf-8")
    assert parse_banks(f) == [(0, 0.857114), (1, 0.5)]
