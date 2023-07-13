#!/bin/bash
BaseFile=termbench.cpp

CLANG=$(which clang++)
if [[ -x "${CLANG}" ]]
then
  CLLinkFlags="-Wformat"
  CLCompileFlags="-O3 -Ofast"
  CC="${CLANG}"
  output=termbench_release_clang
else
	echo "ABORTING: no compiler detected"
fi

if [[ -x "${CC}" ]]
then
  echo Building release: ./${output}
  "${CC}" -O3 ${CLCompileFlags} ${CLLinkFlags} ${BaseFile} -o "${output}"
  strip "${output}"
fi
