"""Чтение результатов симуляции (banks.txt, familys.txt) и подсчёт показателей.

Формат familys.txt: для каждого источника три строки
    USER 0 statistic: ALL: 196 success: 177 declined: 19
    waitDurations: 131 32 139 ...        время ожидания в буфере по каждой отправленной на прибор заявке, мс
    Processing durations: 9 33 25 ...    время обслуживания по каждой обслуженной заявке, мс
Формат banks.txt: по строке на прибор
    Bank 0 0.857114                      коэффициент использования
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import numpy

_HEADER = re.compile(r"USER\s+(\d+)\s+statistic:\s+ALL:\s+(\d+)\s+success:\s+(\d+)\s+declined:\s+(\d+)")


@dataclass(frozen=True)
class FamilyStats:
    id: int
    requests: int        # всего заявок: обслужено + отказов
    success: int
    rejected: int
    wait: list[int]      # время в буфере, мс
    service: list[int]   # время обслуживания, мс

    @property
    def rejection_probability(self) -> float:
        return self.rejected / self.requests if self.requests else 0.0

    @property
    def mean_wait(self) -> float:
        return _mean(self.wait)

    @property
    def mean_service(self) -> float:
        return _mean(self.service)

    @property
    def mean_total(self) -> float:
        """Среднее время пребывания в системе = ожидание + обслуживание."""
        return self.mean_wait + self.mean_service

    def spread_wait(self, variance: bool = False) -> float:
        return _spread(self.wait, variance)

    def spread_service(self, variance: bool = False) -> float:
        return _spread(self.service, variance)


def _mean(values: list[int]) -> float:
    return float(numpy.mean(values)) if values else 0.0


def _spread(values: list[int], variance: bool) -> float:
    if not values:
        return 0.0
    return float(numpy.var(values) if variance else numpy.std(values))


def _numbers_after_colon(line: str, expected_label: str, path: Path, line_no: int) -> list[int]:
    label, sep, rest = line.partition(":")
    if not sep or label.strip() != expected_label:
        raise ValueError(f"{path}:{line_no}: ожидалась строка «{expected_label}: ...», а там: {line[:40]!r}")
    return [int(x) for x in rest.split()]


def parse_families(path: str | Path) -> list[FamilyStats]:
    path = Path(path)
    lines = [ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip()]
    if len(lines) % 3:
        raise ValueError(f"{path}: число непустых строк ({len(lines)}) не кратно трём")
    result = []
    for i in range(0, len(lines), 3):
        match = _HEADER.fullmatch(lines[i].strip())
        if not match:
            raise ValueError(f"{path}:{i + 1}: не похоже на заголовок источника: {lines[i][:60]!r}")
        fid, requests, success, rejected = map(int, match.groups())
        wait = _numbers_after_colon(lines[i + 1], "waitDurations", path, i + 2)
        service = _numbers_after_colon(lines[i + 2], "Processing durations", path, i + 3)
        result.append(FamilyStats(fid, requests, success, rejected, wait, service))
    return sorted(result, key=lambda f: f.id)


def parse_banks(path: str | Path) -> list[tuple[int, float]]:
    path = Path(path)
    result = []
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 3 or parts[0] != "Bank":
            raise ValueError(f"{path}:{n}: ожидалась строка «Bank <номер> <коэффициент>», а там: {line[:40]!r}")
        result.append((int(parts[1]), float(parts[2])))
    return sorted(result)
