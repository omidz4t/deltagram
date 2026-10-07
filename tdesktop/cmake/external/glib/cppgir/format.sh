#!/bin/bash
cd $1
echo -n "Running dos2unix     "
git ls-files | grep '\.cpp$\|\.h$\|\.hpp$' | \
  xargs -I {} sh -c "dos2unix '{}' 2>/dev/null; echo -n '.'"
echo
echo -n "Running clang-format "
git ls-files | grep '\.cpp$\|\.h$\|\.hpp$' | \
  xargs -I {} sh -c "clang-format -i {}; echo -n '.'"
echo


