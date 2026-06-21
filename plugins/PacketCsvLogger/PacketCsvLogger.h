#pragma once

#include <ToolboxUIPlugin.h>

#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>

#include <windows.h>
#include <string>
#include <array>

class PacketCsvLogger : public ToolboxUIPlugin {
public:
    PacketCsvLogger() = default;
    ~PacketCsvLogger() override = default;

    const char* Name() const override { return "Packet CSV Logger"; }
    const char* Icon() const override { return nullptr; };

    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    void Terminate() override;
    void Update(float delta) override;
    void Draw(IDirect3DDevice9* pDevice) override;
    void DrawSettings() override;
    void LoadSettings(const wchar_t* folder) override;
    void SaveSettings(const wchar_t* folder) override;

private:
    void EnableLogging();
    void DisableLogging();
    void WriteHeader();
    void WriteToFile(const char* data, size_t len);

    // Settings
    bool logging_enabled = false;
    bool auto_flush = true;

    std::array<bool, 512> ignored_packets{};

    GW::HookEntry hook_entry;
    HANDLE csv_file = INVALID_HANDLE_VALUE;
    std::wstring csv_folder;
    std::wstring csv_path;
    uint32_t packet_count = 0;
};