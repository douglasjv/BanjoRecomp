if (NOT DEFINED INPUT_FILE OR NOT DEFINED ARRAY_NAME OR NOT DEFINED OUTPUT_C OR NOT DEFINED OUTPUT_H)
    message(FATAL_ERROR "Usage: cmake -DINPUT_FILE=... -DARRAY_NAME=... -DOUTPUT_C=... -DOUTPUT_H=... -P file_to_c.cmake")
endif()

file(READ "${INPUT_FILE}" FILE_BYTES HEX)
string(LENGTH "${FILE_BYTES}" FILE_BYTES_LENGTH)

if (FILE_BYTES_LENGTH EQUAL 0)
    message(FATAL_ERROR "Failed to open file ${INPUT_FILE}! (Or it's empty)")
endif()

math(EXPR FILE_SIZE "${FILE_BYTES_LENGTH} / 2")
math(EXPR LAST_INDEX "${FILE_SIZE} - 1")

get_filename_component(OUTPUT_C_DIR "${OUTPUT_C}" DIRECTORY)
get_filename_component(OUTPUT_H_DIR "${OUTPUT_H}" DIRECTORY)

if (OUTPUT_C_DIR)
    file(MAKE_DIRECTORY "${OUTPUT_C_DIR}")
endif()

if (OUTPUT_H_DIR)
    file(MAKE_DIRECTORY "${OUTPUT_H_DIR}")
endif()

set(C_FILE_CONTENT "#include <stddef.h>\n")
string(APPEND C_FILE_CONTENT "extern const char ${ARRAY_NAME}[${FILE_SIZE}];\n")
string(APPEND C_FILE_CONTENT "const char ${ARRAY_NAME}[${FILE_SIZE}] = {")

foreach (INDEX RANGE ${LAST_INDEX})
    math(EXPR HEX_OFFSET "${INDEX} * 2")
    string(SUBSTRING "${FILE_BYTES}" ${HEX_OFFSET} 2 HEX_BYTE)
    math(EXPR BYTE_VALUE "0x${HEX_BYTE}")

    if (BYTE_VALUE GREATER 127)
        math(EXPR BYTE_VALUE "${BYTE_VALUE} - 256")
    endif()

    string(APPEND C_FILE_CONTENT "${BYTE_VALUE}, ")
endforeach ()

string(APPEND C_FILE_CONTENT "};\n")
string(APPEND C_FILE_CONTENT "extern const size_t ${ARRAY_NAME}_size;\n")
string(APPEND C_FILE_CONTENT "const size_t ${ARRAY_NAME}_size = sizeof(${ARRAY_NAME}) / sizeof(${ARRAY_NAME}[0]);\n")

file(WRITE "${OUTPUT_C}" "${C_FILE_CONTENT}")

set(H_FILE_CONTENT
    "#ifdef __cplusplus\n"
)
string(APPEND H_FILE_CONTENT "  extern \"C\" {\n")
string(APPEND H_FILE_CONTENT "#endif\n")
string(APPEND H_FILE_CONTENT "#include <stddef.h>\n")
string(APPEND H_FILE_CONTENT "extern const char ${ARRAY_NAME}[${FILE_SIZE}];\n")
string(APPEND H_FILE_CONTENT "extern const size_t ${ARRAY_NAME}_size;\n")
string(APPEND H_FILE_CONTENT "#ifdef __cplusplus\n")
string(APPEND H_FILE_CONTENT "  }\n")
string(APPEND H_FILE_CONTENT "#endif\n")

file(WRITE "${OUTPUT_H}" "${H_FILE_CONTENT}")
