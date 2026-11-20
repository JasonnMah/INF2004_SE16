# Separate qualification image; existing UART/HTTP profiles are unchanged.
ifneq ($(SMP)-$(CONSOLE)-$(WIFI),1-usb_cdc-none)
$(error Flash qualification requires SMP=1 CONSOLE=usb_cdc WIFI=none)
endif
ifneq ($(PROFILE_ID),inf2004-flash-cache-smp1-v1)
$(error Use build.ps1 -Target flash)
endif
EXE_FILE := wifi_pico_flash_cache_test
LNKFILE := ../../config/flash_map.ld
CFLAGS += -DTM_CONSOLE_UART=0 -include ../../config/project.h \
 -I../.. -I../../../shared/uart_protocol/src
OBJS := $(filter-out ./mtkernel_3/app_program/%,$(OBJS))
C_DEPS := $(filter-out ./mtkernel_3/app_program/%,$(C_DEPS))
FLASH_NAMES := tests/firmware/flash_cache_main.c batch_store.c flash_port.c buttons.c
FLASH_OBJS := $(addprefix ./mtkernel_3/flash_test/,$(FLASH_NAMES:.c=.o))
OBJS += $(FLASH_OBJS)
C_DEPS += $(FLASH_OBJS:.o=.d)
mtkernel_3/flash_test/%.o: ../../%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
UART_PROTOCOL_ROOT := ../../../shared/uart_protocol
UART_LIBRARY_DIR := ./mtkernel_3/flash_shared
UART_ENDPOINTS :=
include $(UART_PROTOCOL_ROOT)/library.mk
.PHONY: flash-config
flash-config:
	@echo 'PROFILE: flash cache qualification; last 16 KiB reserved; no UART or network'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'ASFLAGS=$(ASFLAGS)'
