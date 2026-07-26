#!/usr/bin/env bash

set -euo pipefail

HERE="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
DIR="$HERE/../implementation/typescript"
npm run --prefix "$DIR" build
npm run --prefix "$DIR" bundle

for tool in main dag; do
  echo '#!/usr/bin/env node' > "$HERE/$tool.js"
  cat "$DIR/$tool.js" >> "$HERE/$tool.js"
  chmod +x "$HERE/$tool.js"
done
