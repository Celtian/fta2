#!/bin/sh
# Resolve the launcher rather than the caller's working directory.
package_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd) || exit 1
exec "$package_dir/bin/fta2-ce" "$@"
