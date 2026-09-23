#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NFSMW Recomp - Extrator de Shaders para XenosRecomp
====================================================

DESCOBERTA TECNICA IMPORTANTE:
O NFSMW usa o formato proprietario EA **ABKC** para armazenar shaders compilados.
Esse formato e INCOMPATIVEL com o ShaderContainer (0x102A1100) que o XenosRecomp procura.

COMO OS SHADERS FUNCIONAM NO NFSMW RECOMP:
1. O RunTime (ReXGlue/Xenia) traduz shaders em RUNTIME capturando PM4 packets
2. Os shaders ja traduzidos sao salvos em cache .xsh (Xenos SHader cache)
3. Na proxima execucao, o .xsh e carregado para evitar recompilacao

ESTE SCRIPT FAZ:
  Modo A) Escanear o .xsh gerado pelo RunTime apos o jogo rodar
  Modo B) Escanear os arquivos ABKC do NFSMW e extrair microcode Xenos bruto
  Modo C) Listar informacoes de diagnostico sobre os shaders encontrados

PARA ALIMENTAR O XENOSRECOMP:
  Use o arquivo .xsh gerado pelo RunTime apos rodar o jogo uma vez.
  O .xsh contem ShaderContainers (0x102A1100) prontos para o XenosRecomp.

Uso:
  python3 extraer_shaders_nfsmw.py [--modo abkc|xsh|info]
"""

import os
import sys
import glob
import struct
import pathlib
import argparse

# ============================================================================
# CONSTANTES
# ============================================================================

SHADER_CONTAINER_MAGIC = 0x102A1100   # Formato XenosRecomp (big-endian)
ABKC_MAGIC = b'ABKC'
S10A_MAGIC = b'S10A'
FX_MAGIC = b'FX'
XSH_MAGIC = 0x48534558  # 'XESH' - formato do shader storage do RunTime


def parse_args():
    p = argparse.ArgumentParser(description='NFSMW Shader Extractor para XenosRecomp')
    p.add_argument('--modo', choices=['abkc', 'xsh', 'info'], default='info',
                   help='Modo: abkc=extrair do ABKC, xsh=usar cache do RunTime, info=diagnostico')
    p.add_argument('--game-root', type=str, default=None,
                   help='Caminho para o game_root (padrao: ../game_root relativo ao script)')
    p.add_argument('--xsh-file', type=str, default=None,
                   help='Arquivo .xsh do RunTime para processar (modo xsh)')
    return p.parse_args()


# ============================================================================
# LEITURA DE ZZDATA / ZDIR
# ============================================================================

class ZDIREntry:
    def __init__(self, name_hash, arch_id, sector_off, csize, usize, flags):
        self.name_hash = name_hash
        self.arch_id = arch_id
        self.sector_off = sector_off
        self.csize = csize
        self.usize = usize
        self.flags = flags

    def __repr__(self):
        return (f'ZDIREntry(hash=0x{self.name_hash:08x}, '
                f'ZZDATA{self.arch_id}.BIN sec=0x{self.sector_off:x}, '
                f'usize={self.usize})')


def load_zdir(zdir_path):
    with open(zdir_path, 'rb') as f:
        data = f.read()
    entries = []
    for i in range(len(data) // 24):
        h, arch, off, csize, usize, flags = struct.unpack('<IIIIII', data[i*24:(i+1)*24])
        entries.append(ZDIREntry(h, arch, off, csize, usize, flags))
    return entries


# ============================================================================
# MODO: INFO (DIAGNOSTICO)
# ============================================================================

def modo_info(game_root, out_dir):
    """Analisa os shaders do NFSMW e imprime diagnostico."""
    nfs_dir = os.path.join(game_root, 'NFS')
    zdir_path = os.path.join(nfs_dir, 'ZDIR.BIN')

    if not os.path.exists(zdir_path):
        print('[!] ZDIR.BIN nao encontrado em ' + nfs_dir)
        return

    entries = load_zdir(zdir_path)
    print('[*] Total de entradas ZDIR: ' + str(len(entries)))

    stats = {'FX': 0, 'ABKC': 0, 'other': 0, 'abkc_total_shaders': 0}
    abkc_entries = []
    fx_entries = []

    for i, e in enumerate(entries):
        path = os.path.join(nfs_dir, 'ZZDATA' + str(e.arch_id) + '.BIN')
        if not os.path.exists(path):
            continue
        try:
            with open(path, 'rb') as f:
                f.seek(e.sector_off * 2048)
                magic = f.read(4)
        except Exception:
            continue

        if magic == ABKC_MAGIC:
            stats['ABKC'] += 1
            with open(path, 'rb') as f:
                f.seek(e.sector_off * 2048)
                hdr = f.read(min(96, e.usize))
            if len(hdr) >= 96:
                n_shaders = struct.unpack('>H', hdr[0x5e:0x60])[0]
                stats['abkc_total_shaders'] += n_shaders
                abkc_entries.append((i, e, n_shaders))
        elif magic[:2] == FX_MAGIC:
            stats['FX'] += 1
            fx_entries.append((i, e))
        else:
            stats['other'] += 1

    print()
    print('=' * 60)
    print('  DIAGNOSTICO DE SHADERS NFSMW')
    print('=' * 60)
    print('  Arquivos ABKC (cache de shaders compilados): ' + str(stats['ABKC']))
    print('  Permutacoes de shader no total (ABKC):       ' + str(stats['abkc_total_shaders']))
    print('  Arquivos FX (descritores de efeitos):        ' + str(stats['FX']))
    print('  Outros arquivos:                             ' + str(stats['other']))
    print()
    print('  FORMATO: EA ABKC (proprietario)')
    print('  O NFSMW usa formato ABKC (EA Binary Compiled shaders)')
    print('  INCOMPATIVEL diretamente com XenosRecomp (espera 0x102A1100)')
    print()
    print('  SOLUCAO RECOMENDADA:')
    print('  1. Instalar o APK no dispositivo Android (adb install)')
    print('  2. Rodar o jogo pelo menos ate o menu principal')
    print('  3. O RunTime gera um arquivo .xsh com ShaderContainers reais')
    print('  4. Usar o .xsh com XenosRecomp para pre-compilacao SPIR-V')
    print()
    print('  LOCALIZACAO DO .xsh no dispositivo Android:')
    print('  /data/user/0/<package>/files/cache/<TITLE_ID>.xsh')
    print('  Recuperar com: adb pull /data/user/0/.../files/cache/ ./shader_cache/')
    print('=' * 60)

    print()
    print('  Sample dos maiores ABKC (mais permutacoes):')
    abkc_entries.sort(key=lambda x: x[2], reverse=True)
    for idx, (i, e, n) in enumerate(abkc_entries[:10]):
        print('    ZZDATA' + str(e.arch_id) + '.BIN sec=0x' + hex(e.sector_off) + ' - ' + str(n) + ' permutacoes (' + str(e.usize) + ' bytes)')

    print()
    print('  Arquivos FX encontrados (descritores de shaders):')
    for i, e in fx_entries:
        print('    Entry ' + str(i) + ': hash=0x' + format(e.name_hash, '08x') + ' ZZDATA' + str(e.arch_id) + '.BIN sec=0x' + hex(e.sector_off) + ' (' + str(e.usize) + ' bytes)')


# ============================================================================
# MODO: XSH (CACHE DO RUNTIME)
# ============================================================================

def modo_xsh(xsh_file, out_dir, xenosrecomp_bin):
    """Processa um arquivo .xsh do RunTime."""
    if not xsh_file or not os.path.exists(xsh_file):
        print()
        print('[!] Arquivo .xsh nao encontrado.')
        print()
        print('    O arquivo .xsh e gerado pelo RunTime quando o jogo executa.')
        print('    Para obte-lo:')
        print()
        print('    1. Instale o APK no Android:')
        print('       adb install -r android/app/build/outputs/apk/debug/app-debug.apk')
        print()
        print('    2. Execute o jogo ate o menu principal')
        print()
        print('    3. Recupere o cache de shaders:')
        print('       adb pull /data/data/com.nfsmw.recomp/files/ ./shader_cache/')
        print()
        print('    4. Execute este script com o .xsh recuperado:')
        print('       python3 extraer_shaders_nfsmw.py --modo xsh --xsh-file shader_cache/XXXX.xsh')
        return

    print('[*] Processando .xsh: ' + xsh_file)
    with open(xsh_file, 'rb') as f:
        data = f.read()

    if len(data) < 8:
        print('[!] Arquivo muito pequeno, invalido')
        return

    magic = struct.unpack('<I', data[0:4])[0]
    if magic != XSH_MAGIC:
        print('[!] Magic invalido: 0x' + format(magic, '08x') + ' (esperado XESH=0x' + format(XSH_MAGIC, '08x') + ')')
        return

    version = struct.unpack('<I', data[4:8])[0]
    print('[*] XESH versao: ' + str(version))

    out_dir_path = pathlib.Path(out_dir)
    out_dir_path.mkdir(parents=True, exist_ok=True)

    pos = 8
    count = 0
    while pos < len(data) - 16:
        if pos + 16 > len(data):
            break
        shader_type = struct.unpack('<I', data[pos:pos+4])[0]
        ucode_count = struct.unpack('<I', data[pos+4:pos+8])[0]
        ucode_hash = struct.unpack('<Q', data[pos+8:pos+16])[0]
        pos += 16

        if ucode_count == 0 or ucode_count > 0x10000:
            break

        ucode_bytes = ucode_count * 4
        if pos + ucode_bytes > len(data):
            break

        ucode = data[pos:pos+ucode_bytes]
        pos += ucode_bytes

        out_path = out_dir_path / ('shader_' + format(ucode_hash, '016x') + '_' + str(shader_type) + '.bin')
        with open(out_path, 'wb') as f:
            f.write(ucode)
        count += 1

    print('[+] ' + str(count) + ' shaders extraidos do .xsh para ' + out_dir)

    if count > 0 and xenosrecomp_bin and os.path.exists(xenosrecomp_bin):
        print()
        print('[*] Para recompilar com XenosRecomp:')
        shader_common = os.path.join(os.path.dirname(xenosrecomp_bin),
                                     '..', 'XenosRecomp', 'shader_common.h')
        output_cpp = os.path.join(os.path.dirname(xenosrecomp_bin),
                                  'shader_cache_nfsmw.cpp')
        print('    ' + xenosrecomp_bin + ' "' + out_dir + '" "' + output_cpp + '" "' + shader_common + '"')


# ============================================================================
# MODO: ABKC (EXTRACAO DIRETA)
# ============================================================================

def extrair_abkc(game_root, out_dir):
    """Extrai microcode Xenos bruto dos arquivos ABKC do NFSMW."""
    nfs_dir = os.path.join(game_root, 'NFS')
    zdir_path = os.path.join(nfs_dir, 'ZDIR.BIN')

    out_path = pathlib.Path(out_dir)
    out_path.mkdir(parents=True, exist_ok=True)

    if not os.path.exists(zdir_path):
        print('[!] ZDIR.BIN nao encontrado: ' + zdir_path)
        return 0

    entries = load_zdir(zdir_path)
    total_extracted = 0
    abkc_count = 0

    print('[*] Escaneando ' + str(len(entries)) + ' entradas ZDIR...')

    for i, e in enumerate(entries):
        arch_path = os.path.join(nfs_dir, 'ZZDATA' + str(e.arch_id) + '.BIN')
        if not os.path.exists(arch_path):
            continue

        try:
            with open(arch_path, 'rb') as f:
                f.seek(e.sector_off * 2048)
                magic = f.read(4)

            if magic != ABKC_MAGIC:
                continue

            with open(arch_path, 'rb') as f:
                f.seek(e.sector_off * 2048)
                abkc_data = f.read(e.usize)

            if len(abkc_data) < 96:
                continue

            abkc_count += 1

            for j in range(0, len(abkc_data) - 4, 4):
                if abkc_data[j:j+4] == S10A_MAGIC:
                    if j + 12 > len(abkc_data):
                        break
                    count_s = struct.unpack('>I', abkc_data[j+8:j+12])[0]
                    if count_s == 0 or count_s > 1000:
                        break

                    s10a_blob = abkc_data[j:]
                    fname = 'ZZDATA' + str(e.arch_id) + '_sec' + format(e.sector_off, '08x') + '_hash' + format(e.name_hash, '08x') + '_s10a.bin'
                    out_file = out_path / fname
                    with open(out_file, 'wb') as f:
                        f.write(s10a_blob)

                    total_extracted += 1
                    print('    [+] ABKC entry ' + str(i) + ': ' + str(count_s) + ' shaders -> ' + fname)
                    break

        except Exception:
            pass

    print()
    print('=' * 60)
    print('  ABKC processados: ' + str(abkc_count))
    print('  Blobs S10A extraidos: ' + str(total_extracted))
    print()
    print('  ATENCAO: O formato S10A nao e compativel com XenosRecomp!')
    print('  Use o modo "xsh" com o cache gerado pelo RunTime.')
    print('=' * 60)

    return total_extracted


# ============================================================================
# MAIN
# ============================================================================

def main():
    args = parse_args()

    script_dir = pathlib.Path(__file__).resolve().parent

    if args.game_root:
        game_root = pathlib.Path(args.game_root)
    else:
        game_root = script_dir.parent / 'game_root'

    out_dir = script_dir / 'xenosrecomp' / 'shaders_nfsmw'
    xenosrecomp_bin = script_dir / 'xenosrecomp' / 'build' / 'XenosRecomp' / 'XenosRecomp'

    print('=' * 60)
    print('  NFSMW Recomp - Extrator de Shaders para XenosRecomp')
    print('=' * 60)
    print('  Game root:  ' + str(game_root))
    print('  Output:     ' + str(out_dir))
    print('  Modo:       ' + args.modo)
    print()

    if not game_root.exists():
        print('[!] game_root nao encontrado: ' + str(game_root))
        print('    Coloque os arquivos do jogo em game_root/NFS/')
        sys.exit(1)

    if args.modo == 'info':
        modo_info(str(game_root), str(out_dir))

    elif args.modo == 'abkc':
        print('[*] Extraindo microcode bruto dos arquivos ABKC...')
        print('[!] NOTA: Formato ABKC nao e compativel diretamente com XenosRecomp.')
        print('    Use o modo "xsh" com o cache do RunTime para uso real com XenosRecomp.')
        print()
        extrair_abkc(str(game_root), str(out_dir))

    elif args.modo == 'xsh':
        xsh = args.xsh_file
        if not xsh:
            for loc in [script_dir.parent, script_dir, pathlib.Path('/tmp')]:
                found = list(loc.glob('**/*.xsh'))
                if found:
                    xsh = str(found[0])
                    print('[*] Encontrado .xsh: ' + xsh)
                    break
        modo_xsh(xsh, str(out_dir), str(xenosrecomp_bin))

    print()
    print('[*] Concluido.')


if __name__ == '__main__':
    main()
