# Convierte un fichero en un array C: cmake -DIN=... -DOUT=... -DNAME=... -P embed_file.cmake
file(READ ${IN} hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE ${OUT} "// Generado desde ${IN}\n#pragma once\n#include <stddef.h>\n"
    "static const unsigned char ${NAME}[] = {${bytes}};\n"
    "static const size_t ${NAME}_len = ${size};\n")
