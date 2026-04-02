.DEFAULT_GOAL := build

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /C
EXE := .exe
define make-dir
if not exist "$(1)" mkdir "$(1)"
endef
define remove-tree
if exist "$(1)" rmdir /S /Q "$(1)"
endef
define touch-file
type nul > "$(1)"
endef
else
SHELL := /bin/sh
.SHELLFLAGS := -ec
EXE :=
define make-dir
mkdir -p "$(1)"
endef
define remove-tree
rm -rf "$(1)"
endef
define touch-file
touch "$(1)"
endef
endif

-include config/local.mk

SDK_ROOT := $(strip $(SDK_ROOT))
AC6_BIN := $(strip $(AC6_BIN))

ifndef SDK_ROOT
$(error SDK_ROOT is not set. Set it in config/local.mk or pass SDK_ROOT=/path/to/DA145xx_SDK)
endif

REPO_ROOT := $(subst \,/,$(abspath .))
SDK_ROOT_NORM := $(subst \,/,$(abspath $(SDK_ROOT)))
AC6_BIN_NORM := $(strip $(subst \,/,$(AC6_BIN)))

ARMCLANG := $(if $(AC6_BIN_NORM),$(AC6_BIN_NORM)/armclang$(EXE),armclang$(EXE))
ARMLINK := $(if $(AC6_BIN_NORM),$(AC6_BIN_NORM)/armlink$(EXE),armlink$(EXE))
FROMELF := $(if $(AC6_BIN_NORM),$(AC6_BIN_NORM)/fromelf$(EXE),fromelf$(EXE))

ifneq ($(AC6_BIN_NORM),)
ifeq ($(wildcard $(ARMCLANG)),)
$(error armclang not found under AC6_BIN=$(AC6_BIN_NORM))
endif
ifeq ($(wildcard $(ARMLINK)),)
$(error armlink not found under AC6_BIN=$(AC6_BIN_NORM))
endif
ifeq ($(wildcard $(FROMELF)),)
$(error fromelf not found under AC6_BIN=$(AC6_BIN_NORM))
endif
endif

BUILD_DIR := build
TMP_DIR := $(BUILD_DIR)/.tmp
OBJ_DIR := $(TMP_DIR)/obj
TMP_STAMP := $(TMP_DIR)/.dir
OBJ_STAMP := $(OBJ_DIR)/.dir
HEX_FILE := $(BUILD_DIR)/PaddlingPulse.hex
AXF_FILE := $(TMP_DIR)/PaddlingPulse.axf
SYMDEF_FILE := $(TMP_DIR)/PaddlingPulse.symdefs.txt
COMPILER_RSP := $(TMP_DIR)/compiler.rsp
LINKER_VIA := $(TMP_DIR)/linker.via
SCATTER_PP := $(TMP_DIR)/DA14531_armclang.sct

SCATTER_FILE := $(REPO_ROOT)/firmware/config/DA14531_armclang.sct
SYMBOL_FILE := $(SDK_ROOT_NORM)/sdk/common_project_files/misc/da14531_symbols.txt
SYSTEM_LIB := $(SDK_ROOT_NORM)/sdk/platform/system_library/output/Keil_5/da14531.lib

$(foreach file,$(SCATTER_FILE) $(SYMBOL_FILE) $(SYSTEM_LIB),$(if $(wildcard $(file)),,$(error Required SDK file not found: $(file))))

SOURCES := \
	sdk/platform/core_modules/rf/src/rf_585.c \
	sdk/ble_stack/rwble/rwble.c \
	sdk/platform/core_modules/rwip/src/rwip.c \
	sdk/platform/core_modules/rf/src/ble_arp.c \
	sdk/platform/core_modules/rf/src/rf_531.c \
	sdk/ble_stack/profiles/prf.c \
	sdk/ble_stack/profiles/prf_utils.c \
	sdk/ble_stack/profiles/cscp/cscps/src/cscps.c \
	sdk/ble_stack/profiles/cscp/cscps/src/cscps_task.c \
	sdk/ble_stack/profiles/dis/diss/src/diss.c \
	sdk/ble_stack/profiles/dis/diss/src/diss_task.c \
	sdk/app_modules/src/app_default_hnd/app_default_handlers.c \
	sdk/app_modules/src/app_common/app.c \
	sdk/app_modules/src/app_common/app_task.c \
	sdk/app_modules/src/app_sec/app_security.c \
	sdk/app_modules/src/app_sec/app_security_task.c \
	sdk/app_modules/src/app_entry/app_entry_point.c \
	sdk/app_modules/src/app_common/app_msg_utils.c \
	sdk/app_modules/src/app_easy/app_easy_timer.c \
	sdk/app_modules/src/app_easy/app_easy_security.c \
	sdk/app_modules/src/app_easy/app_easy_msg_utils.c \
	sdk/app_modules/src/app_diss/app_diss.c \
	sdk/app_modules/src/app_diss/app_diss_task.c \
	sdk/app_modules/src/app_cscp/app_cscps.c \
	sdk/app_modules/src/app_cscp/app_cscps_task.c \
	sdk/app_modules/src/app_bond_db/app_bond_db.c \
	sdk/app_modules/src/app_common/app_utils.c \
	sdk/app_modules/src/app_easy/app_easy_whitelist.c \
	sdk/app_modules/src/app_easy/app_easy_crypto.c \
	sdk/app_modules/src/app_easy/app_easy_storage.c \
	firmware/app/src/paddling_pulse_board.c \
	firmware/app/src/paddling_pulse_app.c \
	firmware/app/src/paddling_pulse_console.c \
	firmware/app/src/paddling_pulse_sample_store.c \
	firmware/app/src/paddling_pulse_stroke_rate.c \
	sdk/platform/arch/boot/system_DA14531.c \
	sdk/platform/arch/boot/startup_DA14531.c \
	sdk/platform/arch/main/hardfault_handler.c \
	sdk/platform/arch/main/nmi_handler.c \
	sdk/platform/core_modules/arch_console/arch_console.c \
	sdk/platform/core_modules/nvds/src/nvds.c \
	sdk/platform/arch/main/arch_main.c \
	sdk/platform/arch/main/jump_table.c \
	sdk/platform/arch/main/arch_sleep.c \
	sdk/platform/arch/main/arch_system.c \
	sdk/platform/arch/main/arch_hibernation.c \
	sdk/platform/arch/main/arch_rom.c \
	third_party/rand/chacha20.c \
	third_party/hash/hash.c \
	sdk/platform/system_library/src/DA14585_586/system_library_585_586.c \
	sdk/platform/system_library/src/DA14531/system_library_531.c \
	sdk/platform/system_library/src/DA14531_01/system_library_531_01.c \
	sdk/platform/system_library/src/DA14535/system_library_535.c \
	sdk/platform/utilities/otp_cs/otp_cs.c \
	sdk/platform/utilities/otp_hdr/otp_hdr.c \
	sdk/platform/driver/syscntl/syscntl.c \
	sdk/platform/driver/gpio/gpio.c \
	sdk/platform/driver/battery/battery.c \
	sdk/platform/driver/wkupct_quadec/wkupct_quadec.c \
	sdk/platform/driver/adc/adc_531.c \
	sdk/platform/driver/spi/spi_531.c \
	sdk/platform/driver/spi_flash/spi_flash.c \
	sdk/platform/driver/i2c/i2c.c \
	sdk/platform/driver/i2c_eeprom/i2c_eeprom.c \
	sdk/platform/driver/uart/uart.c \
	sdk/platform/driver/hw_otpc/hw_otpc_531.c \
	sdk/platform/driver/trng/trng.c \
	sdk/platform/driver/dma/dma.c

INCLUDE_DIRS := \
	sdk/app_modules/api \
	sdk/ble_stack/controller/em \
	sdk/ble_stack/controller/llc \
	sdk/ble_stack/controller/lld \
	sdk/ble_stack/controller/llm \
	sdk/ble_stack/ea/api \
	sdk/ble_stack/em/api \
	sdk/ble_stack/hci/api \
	sdk/ble_stack/hci/src \
	sdk/ble_stack/host/att \
	sdk/ble_stack/host/att/attc \
	sdk/ble_stack/host/att/attm \
	sdk/ble_stack/host/att/atts \
	sdk/ble_stack/host/gap \
	sdk/ble_stack/host/gap/gapc \
	sdk/ble_stack/host/gap/gapm \
	sdk/ble_stack/host/gatt \
	sdk/ble_stack/host/gatt/gattc \
	sdk/ble_stack/host/gatt/gattm \
	sdk/ble_stack/host/l2c/l2cc \
	sdk/ble_stack/host/l2c/l2cm \
	sdk/ble_stack/host/smp \
	sdk/ble_stack/host/smp/smpc \
	sdk/ble_stack/host/smp/smpm \
	sdk/ble_stack/profiles \
	sdk/ble_stack/profiles/anc \
	sdk/ble_stack/profiles/anc/ancc/api \
	sdk/ble_stack/profiles/anp \
	sdk/ble_stack/profiles/anp/anpc/api \
	sdk/ble_stack/profiles/anp/anps/api \
	sdk/ble_stack/profiles/bas/basc/api \
	sdk/ble_stack/profiles/bas/bass/api \
	sdk/ble_stack/profiles/bcs \
	sdk/ble_stack/profiles/bcs/bcsc/api \
	sdk/ble_stack/profiles/bcs/bcss/api \
	sdk/ble_stack/profiles/blp \
	sdk/ble_stack/profiles/blp/blpc/api \
	sdk/ble_stack/profiles/blp/blps/api \
	sdk/ble_stack/profiles/bms \
	sdk/ble_stack/profiles/bms/bmsc/api \
	sdk/ble_stack/profiles/bms/bmss/api \
	sdk/ble_stack/profiles/cpp \
	sdk/ble_stack/profiles/cpp/cppc/api \
	sdk/ble_stack/profiles/cpp/cpps/api \
	sdk/ble_stack/profiles/cscp \
	sdk/ble_stack/profiles/cscp/cscpc/api \
	sdk/ble_stack/profiles/cscp/cscps/api \
	sdk/ble_stack/profiles/cts \
	sdk/ble_stack/profiles/cts/ctsc/api \
	sdk/ble_stack/profiles/cts/ctss/api \
	sdk/ble_stack/profiles/custom \
	sdk/ble_stack/profiles/custom/custs/api \
	sdk/ble_stack/profiles/dis/disc/api \
	sdk/ble_stack/profiles/dis/diss/api \
	sdk/ble_stack/profiles/find \
	sdk/ble_stack/profiles/find/findl/api \
	sdk/ble_stack/profiles/find/findt/api \
	sdk/ble_stack/profiles/gatt/gatt_client/api \
	sdk/ble_stack/profiles/glp \
	sdk/ble_stack/profiles/glp/glpc/api \
	sdk/ble_stack/profiles/glp/glps/api \
	sdk/ble_stack/profiles/hogp \
	sdk/ble_stack/profiles/hogp/hogpbh/api \
	sdk/ble_stack/profiles/hogp/hogpd/api \
	sdk/ble_stack/profiles/hogp/hogprh/api \
	sdk/ble_stack/profiles/hrp \
	sdk/ble_stack/profiles/hrp/hrpc/api \
	sdk/ble_stack/profiles/hrp/hrps/api \
	sdk/ble_stack/profiles/htp \
	sdk/ble_stack/profiles/htp/htpc/api \
	sdk/ble_stack/profiles/htp/htpt/api \
	sdk/ble_stack/profiles/lan \
	sdk/ble_stack/profiles/lan/lanc/api \
	sdk/ble_stack/profiles/lan/lans/api \
	sdk/ble_stack/profiles/pasp \
	sdk/ble_stack/profiles/pasp/paspc/api \
	sdk/ble_stack/profiles/pasp/pasps/api \
	sdk/ble_stack/profiles/prox/proxm/api \
	sdk/ble_stack/profiles/prox/proxr/api \
	sdk/ble_stack/profiles/rscp \
	sdk/ble_stack/profiles/rscp/rscpc/api \
	sdk/ble_stack/profiles/rscp/rscps/api \
	sdk/ble_stack/profiles/scpp \
	sdk/ble_stack/profiles/scpp/scppc/api \
	sdk/ble_stack/profiles/scpp/scpps/api \
	sdk/ble_stack/profiles/suota/suotar/api \
	sdk/ble_stack/profiles/tip \
	sdk/ble_stack/profiles/tip/tipc/api \
	sdk/ble_stack/profiles/tip/tips/api \
	sdk/ble_stack/profiles/uds \
	sdk/ble_stack/profiles/uds/udsc/api \
	sdk/ble_stack/profiles/uds/udss/api \
	sdk/ble_stack/profiles/wss \
	sdk/ble_stack/profiles/wss/wssc/api \
	sdk/ble_stack/profiles/wss/wsss/api \
	sdk/ble_stack/rwble \
	sdk/ble_stack/rwble_hl \
	sdk/common_project_files \
	sdk/platform/arch \
	sdk/platform/arch/boot \
	sdk/platform/arch/boot/ARM \
	sdk/platform/arch/boot/GCC \
	sdk/platform/arch/compiler \
	sdk/platform/arch/compiler/ARM \
	sdk/platform/arch/compiler/GCC \
	sdk/platform/arch/ll \
	sdk/platform/arch/main \
	sdk/platform/core_modules/arch_console \
	sdk/platform/core_modules/common/api \
	sdk/platform/core_modules/crypto \
	sdk/platform/core_modules/dbg/api \
	sdk/platform/core_modules/gtl/api \
	sdk/platform/core_modules/gtl/src \
	sdk/platform/core_modules/h4tl/api \
	sdk/platform/core_modules/ke/api \
	sdk/platform/core_modules/ke/src \
	sdk/platform/core_modules/nvds/api \
	sdk/platform/core_modules/rf/api \
	sdk/platform/core_modules/rwip/api \
	sdk/platform/driver/adc \
	sdk/platform/driver/battery \
	sdk/platform/driver/ble \
	sdk/platform/driver/dma \
	sdk/platform/driver/gpio \
	sdk/platform/driver/hw_otpc \
	sdk/platform/driver/i2c \
	sdk/platform/driver/i2c_eeprom \
	sdk/platform/driver/pdm \
	sdk/platform/driver/reg \
	sdk/platform/driver/rtc \
	sdk/platform/driver/spi \
	sdk/platform/driver/spi_flash \
	sdk/platform/driver/spi_hci \
	sdk/platform/driver/syscntl \
	sdk/platform/driver/systick \
	sdk/platform/driver/timer \
	sdk/platform/driver/trng \
	sdk/platform/driver/uart \
	sdk/platform/driver/wkupct_quadec \
	sdk/platform/include \
	sdk/platform/system_library/include \
	third_party/hash \
	third_party/irng \
	third_party/rand \
	firmware/app/include \
	firmware/config \
	sdk/platform/utilities/otp_cs \
	sdk/platform/utilities/otp_hdr \
	sdk/platform/include/CMSIS/5.9.0/CMSIS/Core/Include

DEFINES := __DA14531__ __MICROLIB ARMCM0P
FORCED_INCLUDES := da14531_config_basic.h da14531_config_advanced.h user_config.h

resolve-path = $(if $(filter firmware firmware/%,$(1)),$(REPO_ROOT)/$(1),$(SDK_ROOT_NORM)/$(1))
obj-name = $(subst /,_,$(basename $(1)))
obj-file = $(OBJ_DIR)/$(call obj-name,$(1)).o
dep-file = $(OBJ_DIR)/$(call obj-name,$(1)).d

OBJ_FILES := $(foreach src,$(SOURCES),$(call obj-file,$(src)))
DEP_FILES := $(OBJ_FILES:.o=.d)

.PHONY: build clean

build: $(HEX_FILE)

clean:
	$(call remove-tree,$(BUILD_DIR))

$(TMP_STAMP):
	$(call make-dir,$(BUILD_DIR))
	$(call make-dir,$(TMP_DIR))
	$(call touch-file,$@)

$(OBJ_STAMP): | $(TMP_STAMP)
	$(call make-dir,$(OBJ_DIR))
	$(call touch-file,$@)

$(COMPILER_RSP): Makefile | $(TMP_STAMP)
	$(file >$@,--target=arm-arm-none-eabi)
	$(file >>$@,-mcpu=cortex-m0plus -mfpu=none)
	$(file >>$@,-mlittle-endian)
	$(file >>$@,-mthumb)
	$(file >>$@,-std=c99)
	$(file >>$@,-Oz)
	$(file >>$@,-flto)
	$(file >>$@,-fno-rtti)
	$(file >>$@,-funsigned-char)
	$(file >>$@,-fshort-enums)
	$(file >>$@,-fshort-wchar)
	$(file >>$@,-fno-function-sections)
	$(file >>$@,-Wno-packed)
	$(file >>$@,-Wno-missing-variable-declarations)
	$(file >>$@,-Wno-missing-prototypes)
	$(file >>$@,-Wno-missing-noreturn)
	$(file >>$@,-Wno-sign-conversion)
	$(file >>$@,-Wno-nonportable-include-path)
	$(file >>$@,-Wno-reserved-id-macro)
	$(file >>$@,-Wno-unused-macros)
	$(file >>$@,-Wno-documentation-unknown-command)
	$(file >>$@,-Wno-documentation)
	$(file >>$@,-Wno-license-management)
	$(file >>$@,-Wno-parentheses-equality)
	$(foreach def,$(DEFINES),$(file >>$@,-D$(def)))
	$(foreach inc,$(INCLUDE_DIRS),$(file >>$@,"-I$(call resolve-path,$(inc))"))
	$(foreach header,$(FORCED_INCLUDES),$(file >>$@,-include $(header)))
	@echo Generated $@

$(SCATTER_PP): $(SCATTER_FILE) $(REPO_ROOT)/firmware/config/da14531_config_basic.h $(REPO_ROOT)/firmware/config/da14531_config_advanced.h | $(TMP_STAMP)
	"$(ARMCLANG)" -E -P --target=arm-arm-none-eabi -mcpu=cortex-m0plus -xc "-I$(REPO_ROOT)/firmware/config" "-I$(SDK_ROOT_NORM)/sdk/common_project_files" "$(SCATTER_FILE)" -o "$@"

$(LINKER_VIA): Makefile $(OBJ_FILES) $(SCATTER_PP) | $(TMP_STAMP)
	$(file >$@,--cpu=Cortex-M0plus)
	$(file >>$@,--scatter="$(SCATTER_PP)")
	$(file >>$@,--library_type=microlib)
	$(file >>$@,--lto)
	$(file >>$@,--no_debug)
	$(file >>$@,--strict)
	$(file >>$@,"$(SYMBOL_FILE)")
	$(file >>$@,--symdefs="$(SYMDEF_FILE)")
	$(file >>$@,--any_placement=best_fit)
	$(file >>$@,--datacompressor off)
	$(foreach obj,$(OBJ_FILES),$(file >>$@,"$(obj)"))
	$(file >>$@,"$(SYSTEM_LIB)")
	$(file >>$@,--output "$(AXF_FILE)")
	@echo Generated $@

$(HEX_FILE): $(AXF_FILE)
	"$(FROMELF)" --i32combined --output "$@" "$(AXF_FILE)"

$(AXF_FILE): $(LINKER_VIA) | $(TMP_STAMP)
	"$(ARMLINK)" --via "$(LINKER_VIA)"

define compile-rule
$(call obj-file,$(1)): $(call resolve-path,$(1)) $(COMPILER_RSP) | $(OBJ_STAMP)
	"$(ARMCLANG)" @$(COMPILER_RSP) -MMD -MP -MF "$(call dep-file,$(1))" -c "$(call resolve-path,$(1))" -o "$$@"
endef

$(foreach src,$(SOURCES),$(eval $(call compile-rule,$(src))))

-include $(DEP_FILES)
