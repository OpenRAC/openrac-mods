# SPDX-License-Identifier: GPL-3.0-or-later
# The archipelago_data test: the committed apworld/rac1/data.py is what dump_table prints.
#   cmake -DTOOL=<archipelago_dump_table> -DDATA=<data.py> -DOUT=<scratch file> -P check_data.cmake
execute_process(COMMAND ${TOOL} OUTPUT_FILE ${OUT} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "archipelago_dump_table failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${OUT} ${DATA} RESULT_VARIABLE same)
if(NOT same EQUAL 0)
  message(FATAL_ERROR "apworld/rac1/data.py is out of date: cmake --build build --target archipelago_data")
endif()
