# glslangValidator -x writes the SPIR-V words as a list; native_vk.cpp includes them as an array's initializer: put them in braces.
file(READ ${IN} words)
file(WRITE ${OUT} "{\n${words}\n}\n")
