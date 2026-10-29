# Loaded by course Makefile before its link prerequisites are expanded.
ifneq ($(SMP),1)
$(error Milestone 1 requires SMP=1)
endif
ifneq ($(CONSOLE),usb_cdc)
$(error Milestone 1 requires CONSOLE=usb_cdc)
endif
ifneq ($(WIFI),none)
$(error Milestone 1 requires WIFI=none)
endif
ifneq ($(filter 1,$(WIFI_JOIN) $(WIFI_NETIF) $(WIFI_STATIC) $(WIFI_DHCP) $(WIFI_DNS) $(WIFI_UDP) $(WIFI_TCP) $(WIFI_TCPBULK)),)
$(error Networking is outside milestone 1)
endif

ifneq ($(PROFILE_ID),inf2004-milestone1-smp1-usb-only-v2)
$(error Use build.ps1 -Target boot to select an isolated object profile)
endif

EXE_FILE := wifi_pico_smp_boot
# USB selection alone leaves the UART sink enabled in the upstream port.
CFLAGS += -DTM_CONSOLE_UART=0 -include ../../config/project.h
# Exclude upstream demo applications without changing their source.
OBJS := $(filter-out ./mtkernel_3/app_program/%,$(OBJS))
C_DEPS := $(filter-out ./mtkernel_3/app_program/%,$(C_DEPS))
BOOT_OBJ := ./mtkernel_3/milestone1/smp_boot_main.o
OBJS += $(BOOT_OBJ)
C_DEPS += $(BOOT_OBJ:.o=.d)

$(BOOT_OBJ): ../../tests/firmware/smp_boot_main.c ../../config/project.h
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

.PHONY: milestone1-config
milestone1-config:
	@echo 'PROFILE: TARGET=$(TARGET) SMP=$(SMP) CONSOLE=$(CONSOLE) TM_CONSOLE_UART=0 WIFI=$(WIFI)'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'ASFLAGS=$(ASFLAGS)'
	@echo 'APPLICATION=../../tests/firmware/smp_boot_main.c'
	@echo 'MAKEFILE_LIST=$(MAKEFILE_LIST)'
