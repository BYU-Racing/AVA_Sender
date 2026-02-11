#!/usr/bin/env bash
set -euo pipefail

# Default values
TARGET_DIR="${1:-.}"
EXTENSIONS=("*.sh" "*.bash" "*.cmake" "*.txt")

echo "Converting files in '$TARGET_DIR' to LF line endings..."

converted=0
skipped=0

for ext in "${EXTENSIONS[@]}"; do
  while IFS= read -r -d '' file; do
    if grep -q $'\r' "$file"; then
      echo "Fixing $file"
      sed -i 's/\r$//' "$file"
      ((converted++))
    else
      ((skipped++))
    fi
  done < <(find "$TARGET_DIR" -type f -name "$ext" -print0)
done

echo
echo "Done."
echo "Converted: $converted"
echo "Skipped (already LF): $skipped"
