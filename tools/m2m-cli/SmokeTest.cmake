# ctest script: gen -> run (with truth, MIDI out, report) -> baseline write +
# check -> expected failure exit codes. Invoked with -DCLI=<m2m-cli> -DOUT=<dir>.

function(run_cli expected_rc)
    execute_process(COMMAND ${CLI} ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL expected_rc)
        message(FATAL_ERROR "m2m-cli ${ARGN}\nexpected exit ${expected_rc}, got ${rc}\n${out}\n${err}")
    endif()
endfunction()

file(REMOVE_RECURSE ${OUT})

run_cli(0 gen level_bursts --out ${OUT} --format pcm16)
run_cli(0 run --mode trigger ${OUT}/level_bursts.wav --truth ${OUT}/level_bursts.truth.json
          --out ${OUT}/level_bursts.mid --report ${OUT}/level_bursts.report.json --block 128)
foreach(f level_bursts.wav level_bursts.truth.json level_bursts.mid level_bursts.report.json)
    if(NOT EXISTS ${OUT}/${f})
        message(FATAL_ERROR "missing output ${OUT}/${f}")
    endif()
endforeach()

run_cli(0 eval --mode trigger --scenarios level_bursts --write-baseline ${OUT}/baseline.json --report ${OUT}/eval.json)
run_cli(0 eval --mode trigger --scenarios level_bursts --baseline ${OUT}/baseline.json)

# Exit codes: I/O error 1, usage 2, engine not available 3, regression 4.
run_cli(1 run --mode trigger ${OUT}/does_not_exist.wav)
run_cli(2 eval --mode nonsense)
run_cli(2 eval --mode trigger --scenarios no_such_scenario)
run_cli(3 eval --mode melody)

file(READ ${OUT}/baseline.json baseline)
string(REGEX REPLACE "\"minF1\": [0-9.e+-]+" "\"minF1\": 1.5" baseline "${baseline}")
file(WRITE ${OUT}/strict_baseline.json "${baseline}")
run_cli(4 eval --mode trigger --scenarios level_bursts --baseline ${OUT}/strict_baseline.json)
