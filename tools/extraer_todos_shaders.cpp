#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <xxhash.h>

#pragma pack(push, 1)
struct ZDIREntry {
    uint32_t name_hash;
    uint32_t arch_id;
    uint32_t sector_off;
    uint32_t csize;
    uint32_t usize;
    uint32_t flags;
};

struct ShaderContainerHeader {
    uint32_t flags;
    uint32_t virtual_size;
    uint32_t physical_size;
    uint32_t field_c;
    uint32_t constant_table_offset;
    uint32_t definition_table_offset;
    uint32_t shader_offset;
    uint32_t field_1c;
    uint32_t field_20;
};

struct XeshHeader {
    uint32_t magic;           // 'XESH' = 0x48534558
    uint32_t version_swapped; // byte_swap(0x20201219) = 0x19122020
};

struct XeshShaderEntryHeader {
    uint64_t ucode_data_hash;
    uint32_t ucode_dword_count_and_type; // bit 31: type (0=PS, 1=VS), bits 0-30: dwords
};
#pragma pack(pop)

static inline uint32_t swap32(uint32_t v) {
    return __builtin_bswap32(v);
}

struct ExtractedShader {
    uint64_t hash;
    uint32_t type; // 0=PS, 1=VS
    uint32_t dword_count;
    std::vector<uint8_t> ucode;
};

int main(int argc, char** argv) {
    std::string game_root = "/media/windroid/SSD KING/NFSMW-RECOMP/game_root/NFS";
    std::string existing_xsh = "/media/windroid/SSD KING/NFSMW-RECOMP/tools/shader_cache_run/454107D9.xsh";
    std::string output_xsh = "/media/windroid/SSD KING/NFSMW-RECOMP/tools/shader_cache_run/454107D9_completo.xsh";

    std::unordered_map<uint64_t, ExtractedShader> all_shaders;

    // 1. Carregar shaders já existentes capturados na corrida real
    if (std::filesystem::exists(existing_xsh)) {
        std::ifstream fx(existing_xsh, std::ios::binary);
        if (fx) {
            XeshHeader xhdr;
            fx.read(reinterpret_cast<char*>(&xhdr), sizeof(xhdr));
            while (fx) {
                XeshShaderEntryHeader shdr;
                if (!fx.read(reinterpret_cast<char*>(&shdr), sizeof(shdr))) break;
                uint32_t type = (shdr.ucode_dword_count_and_type >> 31) & 1;
                uint32_t dword_count = shdr.ucode_dword_count_and_type & 0x7FFFFFFF;
                std::vector<uint8_t> ucode(dword_count * 4);
                if (!fx.read(reinterpret_cast<char*>(ucode.data()), ucode.size())) break;

                ExtractedShader s;
                s.hash = shdr.ucode_data_hash;
                s.type = type;
                s.dword_count = dword_count;
                s.ucode = std::move(ucode);
                all_shaders[s.hash] = std::move(s);
            }
            std::cout << "[+] Carregados " << all_shaders.size() << " shaders existentes da corrida real do .xsh" << std::endl;
        }
    }

    // 2. Ler ZDIR.BIN
    std::string zdir_path = game_root + "/ZDIR.BIN";
    std::ifstream zf(zdir_path, std::ios::binary);
    if (!zf) {
        std::cerr << "[-] Erro ao abrir ZDIR.BIN em " << zdir_path << std::endl;
        return 1;
    }
    zf.seekg(0, std::ios::end);
    size_t zsize = zf.tellg();
    zf.seekg(0, std::ios::beg);
    std::vector<ZDIREntry> zentries(zsize / sizeof(ZDIREntry));
    zf.read(reinterpret_cast<char*>(zentries.data()), zsize);

    std::cout << "[*] Escaneando todos os " << zentries.size() << " arquivos do jogo..." << std::endl;

    size_t extracted_from_game = 0;
    std::unordered_map<uint32_t, std::ifstream> zz_streams;

    for (size_t idx = 0; idx < zentries.size(); ++idx) {
        const auto& ze = zentries[idx];
        uint32_t arch = ze.arch_id;
        if (zz_streams.find(arch) == zz_streams.end()) {
            std::string zz_name = game_root + "/ZZDATA" + std::to_string(arch) + ".BIN";
            zz_streams.emplace(arch, std::ifstream(zz_name, std::ios::binary));
        }
        auto& in_file = zz_streams[arch];
        if (!in_file) continue;

        in_file.seekg(uint64_t(ze.sector_off) * 2048, std::ios::beg);
        std::vector<uint8_t> file_data(ze.usize);
        in_file.read(reinterpret_cast<char*>(file_data.data()), ze.usize);

        if (file_data.size() < sizeof(ShaderContainerHeader)) continue;

        // Escanear por ShaderContainers (0x102A....)
        const size_t max_scan = file_data.size() - sizeof(ShaderContainerHeader);
        for (size_t i = 0; i <= max_scan; i += 4) {
            uint32_t raw_flags = *reinterpret_cast<const uint32_t*>(file_data.data() + i);
            uint32_t flags = swap32(raw_flags);

            if ((flags & 0xFFFF0000) == 0x102A0000) {
                uint32_t virt_size = swap32(*reinterpret_cast<const uint32_t*>(file_data.data() + i + 4));
                uint32_t phys_size = swap32(*reinterpret_cast<const uint32_t*>(file_data.data() + i + 8));

                if (phys_size > 0 && (phys_size % 4) == 0 && phys_size <= 0x10000 &&
                    virt_size > 0 && virt_size <= 0x10000) {
                    
                    size_t ucode_off = i + virt_size;
                    if (ucode_off + phys_size <= file_data.size()) {
                        const uint8_t* ucode_ptr = file_data.data() + ucode_off;
                        uint64_t hash = XXH3_64bits(ucode_ptr, phys_size);
                        uint32_t type = (flags & 1); // 0=PS, 1=VS
                        uint32_t dword_count = phys_size / 4;

                        if (all_shaders.find(hash) == all_shaders.end()) {
                            ExtractedShader es;
                            es.hash = hash;
                            es.type = type;
                            es.dword_count = dword_count;
                            es.ucode.assign(ucode_ptr, ucode_ptr + phys_size);
                            all_shaders[hash] = std::move(es);
                            ++extracted_from_game;
                        }
                    }
                }
            }
        }
    }

    std::cout << "[+] Extraídos " << extracted_from_game << " novos shaders únicos dos arquivos de jogo!" << std::endl;
    std::cout << "[+] Total consolidado de shaders únicos: " << all_shaders.size() << std::endl;

    // 3. Salvar o arquivo 454107D9_completo.xsh
    std::ofstream out(output_xsh, std::ios::binary);
    if (!out) {
        std::cerr << "[-] Erro ao criar " << output_xsh << std::endl;
        return 1;
    }

    XeshHeader xhdr;
    xhdr.magic = 0x48534558; // 'XESH'
    xhdr.version_swapped = swap32(0x20201219); // 0x19122020
    out.write(reinterpret_cast<const char*>(&xhdr), sizeof(xhdr));

    uint32_t total_vs = 0;
    uint32_t total_ps = 0;

    for (const auto& [hash, s] : all_shaders) {
        XeshShaderEntryHeader ehdr;
        ehdr.ucode_data_hash = s.hash;
        ehdr.ucode_dword_count_and_type = (s.type << 31) | (s.dword_count & 0x7FFFFFFF);
        out.write(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
        out.write(reinterpret_cast<const char*>(s.ucode.data()), s.ucode.size());

        if (s.type == 1) total_vs++;
        else total_ps++;
    }

    out.close();
    std::cout << "[✓] Arquivo gerado com sucesso: " << output_xsh << std::endl;
    std::cout << "    Tamanho: " << std::filesystem::file_size(output_xsh) << " bytes" << std::endl;
    std::cout << "    Vertex Shaders (VS): " << total_vs << std::endl;
    std::cout << "    Pixel Shaders  (PS): " << total_ps << std::endl;

    return 0;
}
