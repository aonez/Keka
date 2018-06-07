#! /bin/sh
# check script for Lzip - LZMA lossless data compressor
# Copyright (C) 2008-2017 Antonio Diaz Diaz.
#
# This script is free software: you have unlimited permission
# to copy, distribute and modify it.

LC_ALL=C
export LC_ALL
objdir=`pwd`
testdir=`cd "$1" ; pwd`
LZIP="${objdir}"/lzip
framework_failure() { echo "failure in testing framework" ; exit 1 ; }

if [ ! -f "${LZIP}" ] || [ ! -x "${LZIP}" ] ; then
	echo "${LZIP}: cannot execute"
	exit 1
fi

[ -e "${LZIP}" ] 2> /dev/null ||
	{
	echo "$0: a POSIX shell is required to run the tests"
	echo "Try bash -c \"$0 $1 $2\""
	exit 1
	}

if [ -d tmp ] ; then rm -rf tmp ; fi
mkdir tmp
cd "${objdir}"/tmp || framework_failure

cat "${testdir}"/test.txt > in || framework_failure
in_lz="${testdir}"/test.txt.lz
fail=0
test_failed() { fail=1 ; printf " $1" ; [ -z "$2" ] || printf "($2)" ; }

printf "testing lzip-%s..." "$2"

"${LZIP}" -fkqm4 in
{ [ $? = 1 ] && [ ! -e in.lz ] ; } || test_failed $LINENO
"${LZIP}" -fkqm274 in
{ [ $? = 1 ] && [ ! -e in.lz ] ; } || test_failed $LINENO
for i in bad_size -1 0 4095 513MiB 1G 1T 1P 1E 1Z 1Y 10KB ; do
	"${LZIP}" -fkqs $i in
	{ [ $? = 1 ] && [ ! -e in.lz ] ; } || test_failed $LINENO $i
done
"${LZIP}" -lq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq < in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -cdq in
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -cdq < in
[ $? = 2 ] || test_failed $LINENO
# these are for code coverage
"${LZIP}" -lt "${in_lz}" 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdl "${in_lz}" > out 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdt "${in_lz}" > out 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -t -- nx_file 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --help > /dev/null || test_failed $LINENO
"${LZIP}" -n1 -V > /dev/null || test_failed $LINENO
"${LZIP}" -m 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -z 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --bad_option 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --t 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --test=2 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --output= 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" --output 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
printf "LZIP\001-.............................." | "${LZIP}" -t 2> /dev/null
printf "LZIP\002-.............................." | "${LZIP}" -t 2> /dev/null
printf "LZIP\001+.............................." | "${LZIP}" -t 2> /dev/null

printf "\ntesting decompression..."

"${LZIP}" -lq "${in_lz}" || test_failed $LINENO
"${LZIP}" -t "${in_lz}" || test_failed $LINENO
"${LZIP}" -cd "${in_lz}" > copy || test_failed $LINENO
cmp in copy || test_failed $LINENO

rm -f copy
cat "${in_lz}" > copy.lz || framework_failure
"${LZIP}" -dk copy.lz || test_failed $LINENO
cmp in copy || test_failed $LINENO
printf "to be overwritten" > copy || framework_failure
"${LZIP}" -d copy.lz 2> /dev/null
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -df copy.lz
{ [ $? = 0 ] && [ ! -e copy.lz ] && cmp in copy ; } || test_failed $LINENO

printf "to be overwritten" > copy || framework_failure
"${LZIP}" -df -o copy < "${in_lz}" || test_failed $LINENO
cmp in copy || test_failed $LINENO

rm -f copy
"${LZIP}" < in > anyothername || test_failed $LINENO
"${LZIP}" -dv --output copy - anyothername - < "${in_lz}" 2> /dev/null
{ [ $? = 0 ] && cmp in copy && cmp in anyothername.out ; } ||
	test_failed $LINENO
rm -f copy anyothername.out

"${LZIP}" -lq in "${in_lz}"
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -lq nx_file.lz "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -tq in "${in_lz}"
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -tq nx_file.lz "${in_lz}"
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cdq in "${in_lz}" > copy
{ [ $? = 2 ] && cat copy in | cmp in - ; } || test_failed $LINENO
"${LZIP}" -cdq nx_file.lz "${in_lz}" > copy
{ [ $? = 1 ] && cmp in copy ; } || test_failed $LINENO
rm -f copy
cat "${in_lz}" > copy.lz || framework_failure
for i in 1 2 3 4 5 6 7 ; do
	printf "g" >> copy.lz || framework_failure
	"${LZIP}" -alvv copy.lz "${in_lz}" > /dev/null 2>&1
	[ $? = 2 ] || test_failed $LINENO $i
	"${LZIP}" -atvvvv copy.lz "${in_lz}" 2> /dev/null
	[ $? = 2 ] || test_failed $LINENO $i
done
"${LZIP}" -dq in copy.lz
{ [ $? = 2 ] && [ -e copy.lz ] && [ ! -e copy ] && [ ! -e in.out ] ; } ||
	test_failed $LINENO
"${LZIP}" -dq nx_file.lz copy.lz
{ [ $? = 1 ] && [ ! -e copy.lz ] && [ ! -e nx_file ] && cmp in copy ; } ||
	test_failed $LINENO

cat in in > in2 || framework_failure
cat "${in_lz}" "${in_lz}" > in2.lz || framework_failure
"${LZIP}" -lq in2.lz || test_failed $LINENO
"${LZIP}" -t in2.lz || test_failed $LINENO
"${LZIP}" -cd in2.lz > copy2 || test_failed $LINENO
cmp in2 copy2 || test_failed $LINENO

"${LZIP}" --output=copy2 < in2 || test_failed $LINENO
"${LZIP}" -lq copy2.lz || test_failed $LINENO
"${LZIP}" -t copy2.lz || test_failed $LINENO
"${LZIP}" -cd copy2.lz > copy2 || test_failed $LINENO
cmp in2 copy2 || test_failed $LINENO

printf "\ngarbage" >> copy2.lz || framework_failure
"${LZIP}" -tvvvv copy2.lz 2> /dev/null || test_failed $LINENO
rm -f copy2
"${LZIP}" -alq copy2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq copy2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -atq < copy2.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -adkq copy2.lz
{ [ $? = 2 ] && [ ! -e copy2 ] ; } || test_failed $LINENO
"${LZIP}" -adkq -o copy2 < copy2.lz
{ [ $? = 2 ] && [ ! -e copy2 ] ; } || test_failed $LINENO
printf "to be overwritten" > copy2 || framework_failure
"${LZIP}" -df copy2.lz || test_failed $LINENO
cmp in2 copy2 || test_failed $LINENO

printf "\ntesting   compression..."

"${LZIP}" -cf "${in_lz}" > out 2> /dev/null	# /dev/null is a tty on OS/2
[ $? = 1 ] || test_failed $LINENO
"${LZIP}" -cFvvm36 "${in_lz}" > out 2> /dev/null || test_failed $LINENO
"${LZIP}" -cd out | "${LZIP}" -d > copy || test_failed $LINENO
cmp in copy || test_failed $LINENO

for i in s4Ki 0 1 2 3 4 5 6 7 8 9 ; do
	"${LZIP}" -k -$i in || test_failed $LINENO $i
	mv -f in.lz copy.lz || test_failed $LINENO $i
	printf "garbage" >> copy.lz || framework_failure
	"${LZIP}" -df copy.lz || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i
done

for i in s4Ki 0 1 2 3 4 5 6 7 8 9 ; do
	"${LZIP}" -c -$i in > out || test_failed $LINENO $i
	printf "g" >> out || framework_failure
	"${LZIP}" -cd out > copy || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i
done

for i in s4Ki 0 1 2 3 4 5 6 7 8 9 ; do
	"${LZIP}" -$i < in > out || test_failed $LINENO $i
	"${LZIP}" -d < out > copy || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i
done

for i in s4Ki 0 1 2 3 4 5 6 7 8 9 ; do
	"${LZIP}" -f -$i -o out < in || test_failed $LINENO $i
	"${LZIP}" -df -o copy < out.lz || test_failed $LINENO $i
	cmp in copy || test_failed $LINENO $i
done

cat in in in in in in in in > in8 || framework_failure
"${LZIP}" -1s12 -S100k -o out < in8 || test_failed $LINENO
"${LZIP}" -t out00001.lz out00002.lz || test_failed $LINENO
"${LZIP}" -cd out00001.lz out00002.lz | cmp in8 - || test_failed $LINENO
rm -f out00001.lz
"${LZIP}" -1ks4Ki -b100000 in8 || test_failed $LINENO
"${LZIP}" -t in8.lz || test_failed $LINENO
"${LZIP}" -cd in8.lz | cmp in8 - || test_failed $LINENO
rm -f in8
"${LZIP}" -0 -S100k -o out < in8.lz || test_failed $LINENO
"${LZIP}" -t out00001.lz out00002.lz || test_failed $LINENO
"${LZIP}" -cd out00001.lz out00002.lz | cmp in8.lz - || test_failed $LINENO
rm -f out00001.lz out00002.lz
"${LZIP}" -0kF -b100k in8.lz || test_failed $LINENO
"${LZIP}" -t in8.lz.lz || test_failed $LINENO
"${LZIP}" -cd in8.lz.lz | cmp in8.lz - || test_failed $LINENO
rm -f in8.lz in8.lz.lz

printf "\ntesting bad input..."

cat "${in_lz}" "${in_lz}" "${in_lz}" > in3.lz || framework_failure
if dd if=in3.lz of=trunc.lz bs=14752 count=1 2> /dev/null &&
   [ -e trunc.lz ] && cmp in2.lz trunc.lz > /dev/null 2>&1 ; then
	for i in 6 20 14734 14753 14754 14755 14756 14757 14758 ; do
		dd if=in3.lz of=trunc.lz bs=$i count=1 2> /dev/null
		"${LZIP}" -lq trunc.lz
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -t trunc.lz 2> /dev/null
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -tq < trunc.lz
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -cdq trunc.lz > out
		[ $? = 2 ] || test_failed $LINENO $i
		"${LZIP}" -dq < trunc.lz > out
		[ $? = 2 ] || test_failed $LINENO $i
	done
else
	printf "\nwarning: skipping truncation test: 'dd' does not work on your system."
fi

cat "${in_lz}" > ingin.lz || framework_failure
printf "g" >> ingin.lz || framework_failure
cat "${in_lz}" >> ingin.lz || framework_failure
"${LZIP}" -lq ingin.lz
[ $? = 2 ] || test_failed $LINENO
"${LZIP}" -t ingin.lz || test_failed $LINENO
"${LZIP}" -cd ingin.lz > copy || test_failed $LINENO
cmp in copy || test_failed $LINENO
"${LZIP}" -t < ingin.lz || test_failed $LINENO
"${LZIP}" -d < ingin.lz > copy || test_failed $LINENO
cmp in copy || test_failed $LINENO

echo
if [ ${fail} = 0 ] ; then
	echo "tests completed successfully."
	cd "${objdir}" && rm -r tmp
else
	echo "tests failed."
fi
exit ${fail}
