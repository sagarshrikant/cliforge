# =============================================================================
# run_v2.cmake — behavioural test for v2 features (quantity model, on-error,
# range, units, typed compound fields).
#
# Args: CLIFORGE_BIN, GCC_BIN, SCHEMA_FILE, DRIVER, OUT_DIR
# =============================================================================
foreach(VAR CLIFORGE_BIN GCC_BIN SCHEMA_FILE DRIVER OUT_DIR)
    if(NOT DEFINED ${VAR} OR "${${VAR}}" STREQUAL "")
        message(FATAL_ERROR "run_v2.cmake: ${VAR} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# 1. generate
execute_process(COMMAND "${CLIFORGE_BIN}" -o "${OUT_DIR}" "${SCHEMA_FILE}"
                RESULT_VARIABLE RC ERROR_VARIABLE ERR)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "generate failed: ${ERR}")
endif()

# 2. compile driver + generated code (clean: -Wall -Wextra -Wpedantic)
set(APP "${OUT_DIR}/app")
execute_process(
    COMMAND "${GCC_BIN}" -std=c99 -Wall -Wextra -Wpedantic
            -I "${OUT_DIR}" "${DRIVER}" "${OUT_DIR}/cmdline.c" -o "${APP}"
    RESULT_VARIABLE RC OUTPUT_VARIABLE OUT ERROR_VARIABLE ERR)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "compile failed:\n${OUT}\n${ERR}")
endif()
if(NOT "${ERR}" STREQUAL "")
    message(FATAL_ERROR "compile produced warnings:\n${ERR}")
endif()

# helper: run app, require exit code, and that stdout contains a substring
function(_run label expect_rc needle)
    execute_process(COMMAND ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE er)
    if(NOT rc EQUAL ${expect_rc})
        message(FATAL_ERROR "[${label}] expected exit ${expect_rc}, got ${rc}\nout: ${out}\nerr: ${er}")
    endif()
    if(NOT needle STREQUAL "")
        string(FIND "${out}" "${needle}" pos)
        if(pos EQUAL -1)
            message(FATAL_ERROR "[${label}] stdout missing '${needle}'\nout: ${out}")
        endif()
    endif()
    message(STATUS "st_v2 [${label}] PASS")
endfunction()

# 3a. typed compound + quantity conversion + enum + units
_run("parse" 0 "job.period_ns=2000000000"
     "${APP}" --job "level=3,mode=safe,period=2s,size=4MiB" --timeout=1s --retries=2)
_run("size"  0 "job.size_bytes=4194304"
     "${APP}" --job "level=3,mode=safe,period=2s,size=4MiB")
_run("mode"  0 "job.mode=1"
     "${APP}" --job "mode=safe,level=3,period=2s,size=4MiB")
_run("verbosity-default" 0 "verbosity=1" "${APP}")
_run("timeout-units-ok"  0 "timeout_ns=1000000000" "${APP}" --timeout=1s)

# 3b. on-error=warn + range: --retries=9 clamps to 5, still exits 0
_run("range-warn-clamp" 0 "retries=5" "${APP}" --retries=9)

# 3c. units restriction: 2h not in [ms,s] -> on-error=exit default -> non-zero
_run("units-reject" 2 "" "${APP}" --timeout=2h)

message(STATUS "st_v2_features ALL PASS")
