#!/bin/bash
PACKAGE="com.ea.nfsmw"
LOCAL_CACHE="/media/windroid/SSD KING/NFSMW-RECOMP/tools/shader_cache_run"
mkdir -p "$LOCAL_CACHE"

echo "[MONITOR] Iniciado $(date)" | tee "$LOCAL_CACHE/monitor.log"
echo "[MONITOR] Aguardando .xsh gerado pelo RunTime..." | tee -a "$LOCAL_CACHE/monitor.log"

while true; do
    TS=$(date '+%H:%M:%S')
    
    # Tentar via run-as (APK debugável)
    XSH=$(adb shell "run-as $PACKAGE find files/ -name '*.xsh' 2>/dev/null" 2>/dev/null | tr -d '\r\n ')
    
    if [ -n "$XSH" ]; then
        echo "[$TS] ENCONTRADO: $XSH" | tee -a "$LOCAL_CACHE/monitor.log"
        
        BASENAME=$(basename "$XSH")
        adb shell "run-as $PACKAGE cat $XSH" > "$LOCAL_CACHE/$BASENAME"
        SIZE=$(wc -c < "$LOCAL_CACHE/$BASENAME")
        echo "[$TS] .xsh copiado: $BASENAME ($SIZE bytes)" | tee -a "$LOCAL_CACHE/monitor.log"
        
        if [ "$SIZE" -gt 100 ]; then
            echo "[$TS] SUCCESS - .xsh valido!" | tee -a "$LOCAL_CACHE/monitor.log"
            echo "DONE_OK:$LOCAL_CACHE/$BASENAME" >> "$LOCAL_CACHE/monitor.log"
            break
        fi
    fi
    
    # Verificar também em locais alternativos
    CACHE_XSH=$(adb shell "run-as $PACKAGE find cache/ -name '*.xsh' 2>/dev/null" 2>/dev/null | tr -d '\r\n ')
    if [ -n "$CACHE_XSH" ]; then
        echo "[$TS] Encontrado em cache/: $CACHE_XSH" | tee -a "$LOCAL_CACHE/monitor.log"
        BASENAME=$(basename "$CACHE_XSH")
        adb shell "run-as $PACKAGE cat $CACHE_XSH" > "$LOCAL_CACHE/$BASENAME"
        SIZE=$(wc -c < "$LOCAL_CACHE/$BASENAME")
        if [ "$SIZE" -gt 100 ]; then
            echo "DONE_OK:$LOCAL_CACHE/$BASENAME" >> "$LOCAL_CACHE/monitor.log"
            break
        fi
    fi
    
    echo "[$TS] aguardando..." >> "$LOCAL_CACHE/monitor.log"
    sleep 5
done
