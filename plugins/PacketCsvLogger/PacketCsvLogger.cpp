#include "PacketCsvLogger.h"

#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Utilities/Scanner.h>

#include <string>
#include <chrono>
#include <filesystem>
#include <vector>

namespace {
    std::string UnixTimestampMilliseconds()
    {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(ms);
    }

    // --- Packet field structure (from PacketLoggerWindow) ---
    using StoCHandler_pt = bool(__fastcall*)(GW::Packet::StoC::PacketBase* pak);

    struct StoCHandler {
        uint32_t* fields;
        uint32_t field_count;
        StoCHandler_pt handler_func;
    };

    using StoCHandlerArray = GW::Array<StoCHandler>;

    enum class FieldType {
        Ignore,
        AgentId,
        Float,
        Vect2,
        Vect3,
        Byte,
        Word,
        Dword,
        Blob,
        String16,
        Array8,
        Array16,
        Array32,
        NestedStruct,
        Count
    };

    StoCHandlerArray* GetStoCHandlerArray()
    {
        uintptr_t address = GW::Scanner::Find("\x75\x04\x33\xC0\x5D\xC3\x8B\x41\x08\xA8\x01\x75", "xxxxxxxxxxxx", -6);
        if (!address) return nullptr;
        const uintptr_t StoCHandler_Addr = *(uintptr_t*)address;

        struct GameServer {
            uint8_t h0000[8];
            struct {
                uint8_t h0000[12]{};
                struct {
                    uint8_t h0000[12]{};
                    void* next{};
                    uint8_t h0010[12]{};
                    uint32_t ClientCodecArray[4]{};
                    StoCHandlerArray handlers;
                }* ls_codec{};
                uint8_t h0010[12]{};
                uint32_t ClientCodecArray[4]{};
                StoCHandlerArray handlers;
            }* gs_codec;
        };

        const auto addr = (GameServer**)StoCHandler_Addr;
        if (!(addr && *addr)) return nullptr;
        return &(*addr)->gs_codec->handlers;
    }

    FieldType GetField(const uint32_t type, const uint32_t size, const uint32_t count)
    {
        switch (type) {
        case 0: return FieldType::AgentId;
        case 1: return FieldType::Float;
        case 2: return FieldType::Vect2;
        case 3: return FieldType::Vect3;
        case 4:
        case 8:
            switch (count) {
            case 1: return FieldType::Byte;
            case 2: return FieldType::Word;
            case 4: return FieldType::Dword;
            }
        case 5:
        case 9: return FieldType::Blob;
        case 6:
        case 10: return FieldType::Ignore;
        case 7: return FieldType::String16;
        case 11:
            switch (size) {
            case 1: return FieldType::Array8;
            case 2: return FieldType::Array32;
            case 4: return FieldType::Array32;
            }
        case 12: return FieldType::NestedStruct;
        }
        return FieldType::Count;
    }

    template <typename T>
    void Serialize(uint8_t** bytes, T* val)
    {
        memcpy(val, *bytes, sizeof(T));
        *bytes += sizeof(T);
    }

    // Serialize a single field into its raw value string, advancing *bytes
    std::string SerializeField(const FieldType ft, const uint32_t count, uint8_t** bytes)
    {
        switch (ft) {
        case FieldType::AgentId: {
            uint32_t val;
            Serialize(bytes, &val);
            return std::to_string(val);
        }
        case FieldType::Float: {
            float val;
            Serialize(bytes, &val);
            return std::to_string(val);
        }
        case FieldType::Vect2: {
            float x, y;
            Serialize(bytes, &x);
            Serialize(bytes, &y);
            // Contains comma, so will be quoted in CSV
            return std::to_string(x) + "," + std::to_string(y);
        }
        case FieldType::Vect3: {
            float x, y, z;
            Serialize(bytes, &x);
            Serialize(bytes, &y);
            Serialize(bytes, &z);
            // Contains commas, so will be quoted in CSV
            return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
        }
        case FieldType::Byte:
        case FieldType::Word:
        case FieldType::Dword: {
            uint32_t val;
            Serialize(bytes, &val);
            return std::to_string(val);
        }
        case FieldType::Blob: {
            std::string out;
            for (uint32_t i = 0; i < count; i++) {
                if (i > 0) out += " ";
                out += std::to_string(**bytes);
                ++*bytes;
            }
            return out;
        }
        case FieldType::String16: {
            const auto str = reinterpret_cast<wchar_t*>(*bytes);
            const size_t length = wcsnlen(str, count);
            std::string out;
            for (size_t i = 0; i < length; i++) {
                if (str[i] >= L' ' && str[i] <= L'~') {
                    out += static_cast<char>(str[i]);
                } else {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%X", str[i]);
                    out += buf;
                }
            }
            *bytes += count * 2;
            return out;
        }
        case FieldType::Array8: {
            uint32_t length;
            uint8_t* end = *bytes + count;
            Serialize(bytes, &length);
            std::string out;
            for (size_t i = 0; i < length; i++) {
                uint8_t val;
                Serialize(bytes, &val);
                if (i > 0) out += ",";
                out += std::to_string(val);
            }
            *bytes = end;
            return out;
        }
        case FieldType::Array16: {
            uint32_t length = count;
            Serialize(bytes, &length);
            uint8_t* end = *bytes + count * 2;
            std::string out;
            if (length < 64) {
                for (size_t i = 0; i < length; i++) {
                    uint16_t val;
                    Serialize(bytes, &val);
                    if (i > 0) out += ",";
                    out += std::to_string(val);
                }
            }
            *bytes = end;
            return out;
        }
        case FieldType::Array32: {
            uint32_t length = count;
            Serialize(bytes, &length);
            uint8_t* end = *bytes + count * 4;
            std::string out;
            if (length < 128) {
                for (size_t i = 0; i < length; i++) {
                    uint32_t val;
                    Serialize(bytes, &val);
                    if (i > 0) out += ",";
                    out += std::to_string(val);
                }
            }
            *bytes = end;
            return out;
        }
        default:
            return "";
        }
    }

    // Serialize all fields as individual CSV column strings, recursing for NestedStruct.
    // Each field becomes one value with its type name (e.g. "AgentId(100)").
    // Nested struct repeats its fields for each instance.
    // Returns a vector of field value strings, one per CSV column.
    std::vector<std::string> SerializeAllFields(uint32_t* fields, uint32_t n_fields, uint8_t** bytes)
    {
        std::vector<std::string> out;
        for (uint32_t i = 0; i < n_fields; i++) {
            const uint32_t field = fields[i];
            const uint32_t type = field >> 0 & 0xF;
            const uint32_t size = field >> 4 & 0xF;
            const uint32_t count = field >> 8 & 0xFFFF;

            const FieldType ft = GetField(type, size, count);
            if (ft == FieldType::Ignore) continue;

            if (ft != FieldType::NestedStruct) {
                out.push_back(SerializeField(ft, count, bytes));
            } else {
                uint32_t struct_count;
                Serialize(bytes, &struct_count);
                for (uint32_t rep = 0; rep < struct_count; rep++) {
                    auto nested = SerializeAllFields(fields + i + 1, n_fields - i - 1, bytes);
                    out.insert(out.end(), nested.begin(), nested.end());
                }
                break;
            }
        }
        return out;
    }

    // Cached handler array
    StoCHandlerArray* g_handler_array = nullptr;
    bool g_initialized = false;

    void EnsureHandlers()
    {
        if (g_initialized) return;
        g_initialized = true;
        g_handler_array = GetStoCHandlerArray();
    }
}

DLLAPI ToolboxPlugin* ToolboxPluginInstance()
{
    static PacketCsvLogger instance;
    return &instance;
}

void PacketCsvLogger::Initialize(ImGuiContext* ctx, const ImGuiAllocFns allocator_fns, const HMODULE toolbox_dll)
{
    ToolboxUIPlugin::Initialize(ctx, allocator_fns, toolbox_dll);

    can_show_in_main_window = true;
    show_menubutton = true;
    show_closebutton = true;
    is_resizable = true;
    is_movable = true;

    if (logging_enabled) {
        EnableLogging();
    }
}

void PacketCsvLogger::SignalTerminate()
{
    DisableLogging();
    ToolboxUIPlugin::SignalTerminate();
}

void PacketCsvLogger::Terminate()
{
    DisableLogging();
    ToolboxUIPlugin::Terminate();
}

void PacketCsvLogger::Update(float)
{
}

void PacketCsvLogger::Draw(IDirect3DDevice9*)
{
    const auto visible_ptr = GetVisiblePtr();
    if (!visible_ptr || !*visible_ptr) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(Name(), GetVisiblePtr(), ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::Checkbox("Enable Packet Logging", &logging_enabled)) {
        if (logging_enabled) {
            EnableLogging();
        } else {
            DisableLogging();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Log incoming StoC packets to a CSV file");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(%u packets logged)", packet_count);

    if (csv_file != INVALID_HANDLE_VALUE) {
        ImGui::Text("Output: %ls", csv_path.c_str());
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Ignored Packets")) {
        if (ImGui::Button("Select All")) {
            for (auto& p : ignored_packets) p = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect All")) {
            for (auto& p : ignored_packets) p = false;
        }
        float offset = 0.0f;
        for (size_t i = 0; i < ignored_packets.size(); i++) {
            if (i % 12 == 0) {
                offset = 0.0f;
                ImGui::NewLine();
            }
            ImGui::SameLine(offset, 0);
            offset += 80.0f;
            char buf[32] = {};
            snprintf(buf, sizeof(buf), "%zu###csv_ignore_%zu", i, i);
            ImGui::Checkbox(buf, &ignored_packets[i]);
        }
    }

    ImGui::End();
}

void PacketCsvLogger::DrawSettings()
{
    ToolboxUIPlugin::DrawSettings();

    ImGui::SeparatorText("Logging Options");
    ImGui::Text("CSV columns: UnixTimestampMillis, Opcode, Field1, Field2, ...");
    ImGui::Checkbox("Auto-flush file", &auto_flush);
}

void PacketCsvLogger::LoadSettings(const wchar_t* folder)
{
    ToolboxUIPlugin::LoadSettings(folder);
    csv_folder = folder;
    LoadSetting("logging_enabled", logging_enabled);
    LoadSetting("auto_flush", auto_flush);
    std::string ignored_packets_str;
    LoadSetting("ignored_packets", ignored_packets_str);
    if (!ignored_packets_str.empty()) {
        for (size_t i = 0; i < ignored_packets.size() && i < ignored_packets_str.size(); i++) {
            ignored_packets[i] = ignored_packets_str[i] == '1';
        }
    }
}

void PacketCsvLogger::SaveSettings(const wchar_t* folder)
{
    SaveSetting("logging_enabled", logging_enabled);
    SaveSetting("auto_flush", auto_flush);
    std::string ignored_packets_str;
    ignored_packets_str.reserve(ignored_packets.size());
    for (const auto v : ignored_packets) {
        ignored_packets_str += v ? '1' : '0';
    }
    SaveSetting("ignored_packets", ignored_packets_str);
    ToolboxUIPlugin::SaveSettings(folder);
}

void PacketCsvLogger::WriteToFile(const char* data, size_t len)
{
    if (csv_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(csv_file, data, static_cast<DWORD>(len), &written, nullptr);
    if (auto_flush) {
        FlushFileBuffers(csv_file);
    }
}

void PacketCsvLogger::WriteHeader()
{
    if (csv_file == INVALID_HANDLE_VALUE) return;
    std::string header = "UnixTimestampMillis,Opcode,Fields\n";
    WriteToFile(header.c_str(), header.size());
}

void PacketCsvLogger::EnableLogging()
{
    if (csv_file != INVALID_HANDLE_VALUE) return;

    csv_path = std::filesystem::path(csv_folder) / L"packet_log.csv";

    csv_file = CreateFileW(
        csv_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (csv_file == INVALID_HANDLE_VALUE) {
        logging_enabled = false;
        return;
    }

    packet_count = 0;
    WriteHeader();
    EnsureHandlers();

    for (uint32_t header = 0; header < 512; header++) {
        GW::StoC::RegisterPacketCallback(
            &hook_entry, header,
            [this](GW::HookStatus*, GW::Packet::StoC::PacketBase* pak) -> void {
                if (!logging_enabled || csv_file == INVALID_HANDLE_VALUE) return;
                if (pak->header >= ignored_packets.size()) return;
                if (ignored_packets[pak->header]) return;

                packet_count++;

                std::string row;
                row += UnixTimestampMilliseconds();
                row += ',';
                row += std::to_string(pak->header);

                // Parse fields using the handler table if available
                if (g_handler_array && pak->header < g_handler_array->size()) {
                    const auto& handler = g_handler_array->at(pak->header);
                    if (handler.fields && handler.field_count > 1) {
                        auto packet_raw = reinterpret_cast<uint8_t*>(pak);
                        uint8_t* bytes = packet_raw;
                        uint32_t hdr;
                        Serialize<uint32_t>(&bytes, &hdr); // skip header

                        // Escape helper for CSV: double any internal quotes
                        std::vector<std::string> field_values = SerializeAllFields(
                            handler.fields + 1,
                            handler.field_count - 1,
                            &bytes);
                        for (const auto& val : field_values) {
                            row += ',';
                            // Wrap in quotes if contains comma or quote
                            if (val.find(',') != std::string::npos ||
                                val.find('"') != std::string::npos) {
                                row += '"';
                                for (char c : val) {
                                    if (c == '"') row += "\"\"";
                                    else row += c;
                                }
                                row += '"';
                            } else {
                                row += val;
                            }
                        }
                    }
                }

                row += '\n';
                WriteToFile(row.c_str(), row.size());
            },
            -0x8000
        );
    }
}

void PacketCsvLogger::DisableLogging()
{
    GW::StoC::RemoveCallbacks(&hook_entry);

    if (csv_file != INVALID_HANDLE_VALUE) {
        CloseHandle(csv_file);
        csv_file = INVALID_HANDLE_VALUE;
    }
}