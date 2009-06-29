#! /bin/sh

for file in "$@"; do
	echo
	echo "# automatically generated from $file"
	sed -n \
		-e 's/.*WIN_FUNC(\([^\,]\+\) *\, *\([0-9]\+\)).*/\
		   win2lin(\1, \2)/p'   \
		-e 's/.*WIN_FUNC_PTR(\([^\,]\+\) *\, *\([0-9]\+\)).*/\
		   win2lin(\1, \2)/p'   \
	   $file | sed -e 's/[ \t	]\+//' | sort -u; \
done
