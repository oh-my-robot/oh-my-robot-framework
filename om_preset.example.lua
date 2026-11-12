-- Example preset for project-local configuration.
-- Copy this file to <project-root>/om_preset.lua and update paths for the current project.

local preset = {
  board = {name = "rm-c-board"},
  os = {name = "freertos"},
  toolchain_default = {
    name = "gnu-rm",
  },
  toolchain_presets = {
    ["gnu-rm"] = {
      sdk = "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10",
      bin = "D:/Toolchains/gcc-arm-none-eabi-10.3-2021.10/bin",
    },
    ["armclang"] = {
      sdk = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG",
      bin = "D:/Program Files/ProgramTools/Keil_v5/ARM/ARMCLANG/bin",
    },
    ["tiarmclang"] = {
      sdk = "D:/ProgramFiles/ProgramTools/TI/CCS/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS",
      bin = "D:/ProgramFiles/ProgramTools/TI/CCS/ccs/tools/compiler/ti-cgt-armllvm_4.0.2.LTS/bin",
    },
  },
}

function get_preset()
  return preset
end
