#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Configuración
###############################################################################

BASE="/home/yo/jitterentropy-library/tests/raw-entropy"
REC_DIR="$BASE/recording_userspace"
VAL_RUNTIME_DIR="$BASE/validation-runtime"
VAL_RESTART_DIR="$BASE/validation-restart"
EA_CPP="/home/yo/jitterentropy-library/tests/SP800-90B_EntropyAssessment/cpp"

OUT_ROOT="${OUT_ROOT:-$BASE/results-correlation-sweep}"
CSV_OUT="${CSV_OUT:-$OUT_ROOT/runtime_restart_entropy.csv}"

# Barrido inicial pequeño
OSR_VALUES=(1 3)
HLOOPCNT_VALUES=(0 3)

# Tamaños de muestra
RUNTIME_EVENTS="${RUNTIME_EVENTS:-1000000}"
RESTART_EVENTS="${RESTART_EVENTS:-1000}"
RESTART_REPEATS="${RESTART_REPEATS:-1000}"

# Paralelismo
MAX_JOBS="${MAX_JOBS:-$(nproc)}"

###############################################################################
# Utilidades
###############################################################################

require_bin() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: falta el binario '$1'" >&2
        exit 1
    }
}

abs_path() {
    python3 - <<'PY' "$1"
import os, sys
print(os.path.abspath(sys.argv[1]))
PY
}

extract_runtime_entropy() {
    local f="$1"
    awk '
        BEGIN { IGNORECASE=1 }
        /min\(H_original/ {
            line=$0
            while (match(line, /[0-9]+\.[0-9]+/)) {
                val=substr(line, RSTART, RLENGTH)
                line=substr(line, RSTART + RLENGTH)
            }
        }
        /H_bitstring/ {
            line=$0
            while (match(line, /[0-9]+\.[0-9]+/)) {
                val=substr(line, RSTART, RLENGTH)
                line=substr(line, RSTART + RLENGTH)
            }
        }
        END {
            if (val != "") print val;
            else print "NA";
        }
    ' "$f"
}

extract_restart_metrics() {
    local f="$1"
    awk '
        BEGIN {
            IGNORECASE=1
            hi=""
            hc=""
            hr=""
            minv=""
            finalv=""
        }

        function last_float(s,    tmp, v) {
            v=""
            tmp=s
            while (match(tmp, /[0-9]+\.[0-9]+/)) {
                v=substr(tmp, RSTART, RLENGTH)
                tmp=substr(tmp, RSTART + RLENGTH)
            }
            return v
        }

        /^[[:space:]]*H_I[[:space:]]*[:=]/ {
            v=last_float($0)
            if (v != "") hi=v
        }

        /^[[:space:]]*H_C[[:space:]]*[:=]/ {
            v=last_float($0)
            if (v != "") hc=v
        }

        /^[[:space:]]*H_R[[:space:]]*[:=]/ {
            v=last_float($0)
            if (v != "") hr=v
        }

        /min\(H_r,[[:space:]]*H_c,[[:space:]]*H_I\)/ {
            v=last_float($0)
            if (v != "") minv=v
        }

        /min\(H_R,[[:space:]]*H_C,[[:space:]]*H_I\)/ {
            v=last_float($0)
            if (v != "") minv=v
        }

        /Final entropy estimate/ {
            v=last_float($0)
            if (v != "") finalv=v
        }

        END {
            if (hi == "") hi="NA"
            if (hc == "") hc="NA"
            if (hr == "") hr="NA"
            if (minv == "") minv="NA"
            if (finalv == "") finalv="NA"
            printf "%s,%s,%s,%s,%s\n", hi, hc, hr, minv, finalv
        }
    ' "$f"
}

extract_restart_threshold() {
    local logf="$1"
    awk '
        {
            while (match($0, /[0-9]+\.[0-9]+/)) {
                v=substr($0, RSTART, RLENGTH)
                if (v == "0.333" || v == "0.333000") {
                    print v
                    exit
                }
                $0=substr($0, RSTART + RLENGTH)
            }
        }
        END {
            if (NR == 0) print "NA"
        }
    ' "$logf" 2>/dev/null || echo "NA"
}

extract_health_flags() {
    local f="$1"
    local rct=0
    local apt=0
    local lag=0

    grep -q "RCT" "$f" && rct=1 || true
    grep -q "APT" "$f" && apt=1 || true
    grep -q "Lag" "$f" && lag=1 || true

    printf '%s,%s,%s\n' "$rct" "$apt" "$lag"
}

wait_for_slot() {
    while true; do
        local running
        running=$(jobs -rp | wc -l)
        [[ "$running" -lt "$MAX_JOBS" ]] && break
        sleep 0.2
    done
}

###############################################################################
# Comprobaciones
###############################################################################

require_bin taskset
require_bin awk
require_bin sed
require_bin grep
require_bin find
require_bin python3

[[ -d "$REC_DIR" ]] || { echo "ERROR: no existe $REC_DIR" >&2; exit 1; }
[[ -d "$VAL_RUNTIME_DIR" ]] || { echo "ERROR: no existe $VAL_RUNTIME_DIR" >&2; exit 1; }
[[ -d "$VAL_RESTART_DIR" ]] || { echo "ERROR: no existe $VAL_RESTART_DIR" >&2; exit 1; }
[[ -x "$REC_DIR/jitterentropy-hashtime" ]] || { echo "ERROR: no existe $REC_DIR/jitterentropy-hashtime" >&2; exit 1; }
[[ -x "$EA_CPP/ea_non_iid" ]] || { echo "ERROR: no existe $EA_CPP/ea_non_iid" >&2; exit 1; }
[[ -x "$EA_CPP/ea_restart" ]] || { echo "ERROR: no existe $EA_CPP/ea_restart" >&2; exit 1; }

mkdir -p "$OUT_ROOT"

CSV_ABS=$(abs_path "$CSV_OUT")
LOCKDIR="$OUT_ROOT/.csv-lock"

echo "osr,hloopcnt,core,runtime_entropy,restart_h_i,restart_h_c,restart_h_r,restart_min_hirc,restart_final,restart_threshold,rct,apt,lag,runtime_result_file,restart_result_file,run_dir" > "$CSV_OUT"

###############################################################################
# Worker
###############################################################################

run_one() {
    local osr="$1"
    local hloop="$2"
    local core="$3"

    local tag="osr_${osr}__hloop_${hloop}"
    local run_dir="$OUT_ROOT/$tag"
    local meas_dir="$run_dir/measurements"
    local rt_dir="$run_dir/runtime_analysis"
    local rs_dir="$run_dir/restart_analysis"

    mkdir -p "$meas_dir" "$rt_dir" "$rs_dir"

    local runtime_prefix="$meas_dir/jent-raw-noise"
    local restart_prefix="$meas_dir/jent-raw-noise-restart"

    local runtime_gen_log="$run_dir/runtime_generate.log"
    local restart_gen_log="$run_dir/restart_generate.log"
    local runtime_proc_log="$run_dir/runtime_process.log"
    local restart_proc_log="$run_dir/restart_process.log"

    echo "[core $core] Ejecutando $tag"

    ############################################################################
    # Limpiar restos anteriores
    ############################################################################
    find "$meas_dir" -maxdepth 1 -type f -name 'jent-raw-noise*' -delete 2>/dev/null || true
    find "$rt_dir" -maxdepth 1 -type f -delete 2>/dev/null || true
    find "$rs_dir" -maxdepth 1 -type f -delete 2>/dev/null || true

    ############################################################################
    # Generación runtime
    ############################################################################
    (
        cd "$REC_DIR"
        taskset -c "$core" \
            ./jitterentropy-hashtime \
                "$RUNTIME_EVENTS" \
                1 \
                "$runtime_prefix" \
                --osr "$osr" \
                --hloopcnt "$hloop"
    ) >"$runtime_gen_log" 2>&1

    ############################################################################
    # Generación restart
    ############################################################################
    (
        cd "$REC_DIR"
        taskset -c "$core" \
            ./jitterentropy-hashtime \
                "$RESTART_EVENTS" \
                "$RESTART_REPEATS" \
                "$restart_prefix" \
                --osr "$osr" \
                --hloopcnt "$hloop"
    ) >"$restart_gen_log" 2>&1

    ############################################################################
    # Procesado runtime
    ############################################################################
    (
        cd "$VAL_RUNTIME_DIR"
        BUILD_EXTRACT="yes" \
        ./processdata.sh "$meas_dir" "$rt_dir"
    ) >"$runtime_proc_log" 2>&1

    ############################################################################
    # Procesado restart
    ############################################################################
    (
        cd "$VAL_RESTART_DIR"
        ENTROPYDATA_DIR="$meas_dir" \
        RESULTS_DIR="$rs_dir" \
        BUILD_EXTRACT="yes" \
        ./processdata.sh
    ) >"$restart_proc_log" 2>&1

    ############################################################################
    # Localizar resultados
    ############################################################################
    local runtime_result=""
    runtime_result=$(find "$rt_dir" -maxdepth 1 -type f -name 'jent-raw-noise-0001.minentropy_*bits.txt' | sort | grep 'FF_8bits' | head -n1 || true)
    [[ -n "$runtime_result" ]] || runtime_result=$(find "$rt_dir" -maxdepth 1 -type f -name 'jent-raw-noise-0001.minentropy_*bits.txt' | sort | head -n1 || true)

    local restart_result=""
    restart_result=$(find "$rs_dir" -maxdepth 1 -type f -name 'jent-raw-noise-restart-consolidated.minentropy_*bits.txt' | sort | grep 'FF_8bits' | head -n1 || true)
    [[ -n "$restart_result" ]] || restart_result=$(find "$rs_dir" -maxdepth 1 -type f -name 'jent-raw-noise-restart-consolidated.minentropy_*bits.txt' | sort | head -n1 || true)

    ############################################################################
    # Extraer métricas
    ############################################################################
    local runtime_entropy="NA"
    local restart_h_i="NA"
    local restart_h_c="NA"
    local restart_h_r="NA"
    local restart_min_hirc="NA"
    local restart_final="NA"
    local restart_threshold="NA"

    [[ -n "${runtime_result:-}" && -f "$runtime_result" ]] && runtime_entropy=$(extract_runtime_entropy "$runtime_result")

    if [[ -n "${restart_result:-}" && -f "$restart_result" ]]; then
        local restart_metrics
        restart_metrics=$(extract_restart_metrics "$restart_result")
        IFS=',' read -r restart_h_i restart_h_c restart_h_r restart_min_hirc restart_final <<< "$restart_metrics"
    fi

    restart_threshold=$(extract_restart_threshold "$restart_proc_log")

    local health
    health=$(extract_health_flags "$runtime_gen_log")
    local rct apt lag
    IFS=',' read -r rct apt lag <<< "$health"

    ############################################################################
    # Depuración si algo falla
    ############################################################################
    if [[ -z "${runtime_result:-}" ]]; then
        echo "[core $core] No se encontró resultado runtime en $rt_dir" >&2
        echo "---- ficheros runtime_analysis ($tag) ----" >&2
        find "$rt_dir" -maxdepth 1 -type f | sort >&2 || true
        echo "---- runtime_process.log ($tag) ----" >&2
        tail -n 100 "$runtime_proc_log" >&2 || true
    fi

    if [[ -z "${restart_result:-}" ]]; then
        echo "[core $core] No se encontró resultado restart en $rs_dir" >&2
        echo "---- ficheros restart_analysis ($tag) ----" >&2
        find "$rs_dir" -maxdepth 1 -type f | sort >&2 || true
        echo "---- restart_process.log ($tag) ----" >&2
        tail -n 100 "$restart_proc_log" >&2 || true
    fi

    if [[ "$restart_h_i" == "NA" && -n "${restart_result:-}" && -f "$restart_result" ]]; then
        echo "[core $core] No se pudieron parsear las métricas de restart en $restart_result" >&2
        echo "---- restart result file ($tag) ----" >&2
        sed -n '1,160p' "$restart_result" >&2 || true
    fi

    ############################################################################
    # Escritura segura del CSV
    ############################################################################
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        sleep 0.05
    done

    {
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "$osr" \
            "$hloop" \
            "$core" \
            "$runtime_entropy" \
            "$restart_h_i" \
            "$restart_h_c" \
            "$restart_h_r" \
            "$restart_min_hirc" \
            "$restart_final" \
            "$restart_threshold" \
            "$rct" \
            "$apt" \
            "$lag" \
            "${runtime_result:-NA}" \
            "${restart_result:-NA}" \
            "$run_dir"
    } >> "$CSV_ABS"

    rmdir "$LOCKDIR"

    echo "[core $core] Terminado $tag -> runtime=$runtime_entropy H_I=$restart_h_i H_C=$restart_h_c H_R=$restart_h_r min=$restart_min_hirc final=$restart_final"
}

###############################################################################
# Lanzamiento en paralelo
###############################################################################

cores=()
while IFS= read -r c; do
    cores+=("$c")
done < <(seq 0 $(( $(nproc) - 1 )))

idx=0
for osr in "${OSR_VALUES[@]}"; do
    for hloop in "${HLOOPCNT_VALUES[@]}"; do
        wait_for_slot
        core="${cores[$(( idx % ${#cores[@]} ))]}"
        run_one "$osr" "$hloop" "$core" &
        idx=$((idx + 1))
    done
done

wait

echo
echo "CSV generado en:"
echo "  $CSV_OUT"
echo
echo "Contenido:"
cat "$CSV_OUT"
