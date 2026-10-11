#!/usr/bin/env bash
# Собирает симулятор, прогоняет его и показывает результаты.
# Использование: ./run.sh [секунд]    (по умолчанию 15)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -S . -B build >/dev/null
cmake --build build -j

./build/smo out "${1:-15}" --quiet
python3 main.py out
