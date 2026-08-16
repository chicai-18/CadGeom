# Turns a SPIR-V binary into a header holding a uint32_t array, so shaders ship
# inside cadgeom.dll instead of as loose files a deployment can lose.
#
# Run in script mode:
#   cmake -DINPUT=x.spv -DOUTPUT=x.spv.h -DSYMBOL=x_spv -P bin2c.cmake

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "bin2c.cmake requires -DINPUT, -DOUTPUT and -DSYMBOL")
endif()

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)

if(hexLength EQUAL 0)
    message(FATAL_ERROR "bin2c: ${INPUT} is empty")
endif()

# SPIR-V is a stream of 32-bit words, and vkCreateShaderModule wants it as one.
# Emitting uint32_t rather than bytes also sidesteps the alignment cast that a
# char array would force on every caller.
math(EXPR remainder "${hexLength} % 8")
if(NOT remainder EQUAL 0)
    message(FATAL_ERROR "bin2c: ${INPUT} is not a whole number of 32-bit words")
endif()

set(body "")
set(index 0)
set(column 0)
while(index LESS hexLength)
    string(SUBSTRING "${hex}" ${index} 8 word)
    string(SUBSTRING "${word}" 0 2 byte0)
    string(SUBSTRING "${word}" 2 2 byte1)
    string(SUBSTRING "${word}" 4 2 byte2)
    string(SUBSTRING "${word}" 6 2 byte3)

    # file(READ ... HEX) walks the file in order and SPIR-V is little-endian on
    # every platform CadGeom targets, so the bytes reverse into a word here.
    string(APPEND body "0x${byte3}${byte2}${byte1}${byte0},")

    math(EXPR column "${column} + 1")
    if(column EQUAL 6)
        string(APPEND body "\n    ")
        set(column 0)
    else()
        string(APPEND body " ")
    endif()

    math(EXPR index "${index} + 8")
endwhile()

get_filename_component(sourceName "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"// Generated from ${sourceName} by cmake/bin2c.cmake. Do not edit.
#pragma once

#include <stdint.h>

static const uint32_t ${SYMBOL}[] = {
    ${body}
};
")
