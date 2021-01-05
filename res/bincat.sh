#!/bin/sh
# Binary Resource Concatenator
# Copyright: 2021 Jaakko Keränen <jaakko.keranen@iki.fi>
# License: BSD 2-Clause

OUTPUT=--
SIZES=""
for fn in $*; do
    if [ "$OUTPUT" == "--" ]; then        
        OUTPUT=$fn
        rm -f ${OUTPUT}
    else
        vals=(`/bin/ls -l $fn`)
        if [ "$SIZES" == "" ]; then
            SIZES=${vals[4]}
        else
            SIZES=$SIZES\;${vals[4]}
        fi
        cat ${fn} >> ${OUTPUT}
    fi
done
echo $SIZES
