#!/usr/bin/env bash

set -euo pipefail

HERE="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
DIR="$HERE/../implementation/typescript"
npm run --prefix "$DIR" build
npm run --prefix "$DIR" bundle

echo '#!/usr/bin/env node' > "$HERE/main.js"
cat "$DIR/main.js" >> "$HERE/main.js"
chmod +x "$HERE/main.js"
