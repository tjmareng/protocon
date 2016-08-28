#!/bin/sh

this_exe_path=$(dirname $(readlink -f "$0"))
source "${this_exe_path}/include.sh"

if [ -n "$3" ]
then
  minsz=$1
  maxsz=$2
  key=$3
  shift 3
elif [ -n "$2" ]
then
  minsz=2
  maxsz=$1
  key=$2
  shift 2
elif [ -n "$1" ]
then
  minsz=2
  maxsz=""
  key=$1
  shift 1
else
  echo 'Need at least one argument.' >&2
  exit 1
fi

domsz=$(calc_domsz "$key")
echo "domsz=$domsz"

if [ -z "$maxsz" ]
then
  maxsz=$(($domsz + 1))
fi

protfile="/tmp/${key}.prot"
printf '%s' "$key" | "$gen_exe" -domsz $domsz -o-prot "$protfile"
"${proj_path}/s/verifn.sh" "$minsz" "$maxsz" "$protfile" "$@"

ret=$?
rm "$protfile"

exit $ret
