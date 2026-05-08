#!/bin/bash
# setup_and_push.sh
# Jalankan di Termux setelah clone repo
# Usage: bash setup_and_push.sh <nama_repo_github>
# Contoh: bash setup_and_push.sh amlgui

REPO_NAME=${1:-amlgui}
GITHUB_USER="brruham-arch"

echo "=== [1/4] Clone repo ==="
cd ~
git clone https://github.com/$GITHUB_USER/$REPO_NAME.git
cd $REPO_NAME

echo "=== [2/4] Salin semua file project ==="
# Jalankan ini dari folder hasil extract zip
# cp -r /storage/emulated/0/amlgui/* .

echo "=== [3/4] Push ke GitHub ==="
git add .
git commit -m "init: AML ImGui overlay v1.0"
git push origin main

echo "=== [4/4] Trigger build dan pantau ==="
gh workflow run build.yml
sleep 3
gh run watch $(gh run list --limit 1 --json databaseId -q '.[0].databaseId')
