#! /bin/sh

# Generate exports symbol table from C files

input="$1"
output="$2"
exports=$(basename "$output" .h)
exec >"$output"

echo "/* automatically generated from src */";

sed -n -e '/^\(wstdcall\|wfastcall\|noregparm\|__attribute__\)/{
:more
N
s/\([^{]\)$/\1/
t more
s/\n{$/;/
p
}' $input

echo "#ifdef CONFIG_X86_64";

sed -n \
	-e 's/.*WIN_FUNC(\([^\,]\+\) *\, *\([0-9]\+\)).*/'\
'WIN_FUNC_DECL(\1, \2)/p' \
	-e 's/.*WIN_FUNC_PTR(\([^\,]\+\) *\, *\([0-9]\+\)).*/'\
'WIN_FUNC_DECL(\1, \2)/p' $input | sort -u

echo "#endif"
echo "extern struct wrap_export $exports[];"
echo "struct wrap_export $exports[] = {"

sed -n \
	-e 's/.*WIN_FUNC(_win_\([^\,]\+\) *\, *\([0-9]\+\)).*/'\
'	WIN_WIN_SYMBOL(\1, \2),/p' \
	-e 's/.*WIN_FUNC(\([^\,]\+\) *\, *\([0-9]\+\)).*/'\
'	WIN_SYMBOL(\1, \2),/p' \
	-e 's/.*WIN_SYMBOL_MAP(\("[^"]\+"\)[ ,\n]\+\([^)]\+\)).*/'\
'	{\1, (generic_func)\2},/p' $input | sort -u

echo "	{NULL, NULL}"
echo "};"
