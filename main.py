"""Просмотр результатов симуляции СМО: две таблицы (источники и приборы).

Использование:  python main.py [каталог_с_результатами] [--variance]
Каталог по умолчанию - текущий; в нём должны лежать banks.txt и familys.txt (их пишет ./smo).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from smo_stats import FamilyStats, parse_banks, parse_families


def family_rows(families: list[FamilyStats], variance: bool) -> list[list[str]]:
    return [
        [
            str(f.id),
            str(f.requests),
            f"{f.rejection_probability:.3f}",
            str(f.rejected),
            f"{f.mean_total:.1f}",
            f"{f.mean_wait:.1f}",
            f"{f.mean_service:.1f}",
            f"{f.spread_wait(variance):.1f}",
            f"{f.spread_service(variance):.1f}",
        ]
        for f in families
    ]


def make_table_window(title: str, headers: list[str], tooltips: list[str], rows: list[list[str]], offset: int):
    from PyQt6.QtCore import Qt
    from PyQt6.QtWidgets import QMainWindow, QTableWidget, QTableWidgetItem

    window = QMainWindow()
    window.setWindowTitle(title)
    window.setGeometry(400 + offset, 200 + offset, 800, 400)

    table = QTableWidget(len(rows), len(headers))
    table.setHorizontalHeaderLabels(headers)
    for col, tip in enumerate(tooltips):
        table.horizontalHeaderItem(col).setToolTip(tip)
    for r, row in enumerate(rows):
        for c, value in enumerate(row):
            item = QTableWidgetItem(value)
            item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEditable)
            table.setItem(r, c, item)
    table.resizeColumnsToContents()
    window.setCentralWidget(table)
    return window


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("data_dir", nargs="?", default=".", type=Path, help="каталог с banks.txt и familys.txt")
    parser.add_argument("--variance", action="store_true",
                        help="в колонках Дбп/Добсл показывать дисперсию, а не стандартное отклонение")
    args = parser.parse_args()

    families_file = args.data_dir / "familys.txt"
    banks_file = args.data_dir / "banks.txt"
    for f in (families_file, banks_file):
        if not f.is_file():
            sys.exit(f"Не найден файл {f}. Сначала запусти симуляцию: ./build/smo {args.data_dir}")
    try:
        families = parse_families(families_file)
        banks = parse_banks(banks_file)
    except ValueError as e:
        sys.exit(f"Не удалось прочитать результаты: {e}")

    from PyQt6.QtWidgets import QApplication

    spread = "дисперсия" if args.variance else "стандартное отклонение"
    app = QApplication(sys.argv)
    family_window = make_table_window(
        "Источники",
        ["№ источника", "Количество заявок", "p", "Отказы", "Тпреб", "Тбп", "Тобсл", "Дбп", "Добсл"],
        [
            "Номер источника заявок",
            "Всего заявок источника: обслужено + отказов",
            "Вероятность отказа: отказы / количество заявок",
            "Заявок, вытесненных из буфера",
            "Среднее время пребывания заявки в системе (Тбп + Тобсл), мс",
            "Среднее время ожидания в буфере, мс",
            "Среднее время обслуживания на приборе, мс",
            f"Время ожидания в буфере: {spread}",
            f"Время обслуживания: {spread}",
        ],
        family_rows(families, args.variance),
        offset=0,
    )
    bank_window = make_table_window(
        "Приборы",
        ["№ прибора", "Коэффициент использования"],
        ["Номер прибора", "Доля времени, в которую прибор обслуживал заявки"],
        [[str(i), f"{u:.3f}"] for i, u in banks],
        offset=40,
    )
    family_window.show()
    bank_window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
