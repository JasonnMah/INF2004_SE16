# UART regression application; source/flags selected before link prerequisites.
ifneq ($(SMP)-$(CONSOLE)-$(WIFI),1-usb_cdc-none)
$(error UART requires SMP=1 CONSOLE=usb_cdc WIFI=none)
endif
UART_FAULT ?= 0
APP_VERBOSE_DIAGNOSTICS ?= 0
APP_SENSOR_EMULATOR ?= 1
ifeq ($(filter $(UART_FAULT),0 1 2 3),)
$(error UART_FAULT must be 0, 1, 2 or 3)
endif
ifeq ($(filter $(APP_VERBOSE_DIAGNOSTICS),0 1),)
$(error APP_VERBOSE_DIAGNOSTICS must be 0 or 1)
endif
ifeq ($(APP_SENSOR_EMULATOR),0)
ifeq ($(UART_FAULT),1)
$(error Use the physical disconnected-wire test for two-board command timeout)
endif
EXPECTED_PROFILE := inf2004-uart-smp1-two-board-fault$(UART_FAULT)-diag$(APP_VERBOSE_DIAGNOSTICS)-shared-v4
EXE_FILE := wifi_pico_uart_two_board
ifneq ($(UART_FAULT),0)
EXE_FILE := $(EXE_FILE)_f$(UART_FAULT)
endif
else ifeq ($(APP_SENSOR_EMULATOR),1)
EXPECTED_PROFILE := inf2004-uart-smp1-fault$(UART_FAULT)-diag$(APP_VERBOSE_DIAGNOSTICS)-shared-v4
EXE_FILE := wifi_pico_uart_loopback_f$(UART_FAULT)
else
$(error APP_SENSOR_EMULATOR must be 0 or 1)
endif
ifeq ($(APP_VERBOSE_DIAGNOSTICS),1)
EXE_FILE := $(EXE_FILE)_verbose
endif
ifneq ($(PROFILE_ID),$(EXPECTED_PROFILE))
$(error Use build.ps1 to select an isolated object profile)
endif
CFLAGS += -DAPP_SENSOR_EMULATOR=$(APP_SENSOR_EMULATOR) -DAPP_VERBOSE_DIAGNOSTICS=$(APP_VERBOSE_DIAGNOSTICS) \
 -DTM_CONSOLE_UART=0 -DUART_FAULT=$(UART_FAULT) -include ../../config/project.h \
 -I../.. -I../../tests/firmware -I../../../shared/uart_protocol/src -I../../../uart_protocol/src
OBJS := $(filter-out ./mtkernel_3/app_program/%,$(OBJS))
C_DEPS := $(filter-out ./mtkernel_3/app_program/%,$(C_DEPS))
PROJECT_SRCS := main.c communication.c lab_capture.c uart_io.c rx_ring.c buttons.c ui.c status.c kernel_io.c
ifeq ($(APP_SENSOR_EMULATOR),1)
PROJECT_SRCS := $(filter-out lab_capture.c,$(PROJECT_SRCS))
PROJECT_SRCS += tests/firmware/sensor_emulator.c tests/firmware/fault_injection.c
else ifneq ($(UART_FAULT),0)
PROJECT_SRCS += tests/firmware/fault_injection.c
endif
PROJECT_OBJS := $(addprefix ./mtkernel_3/uart/,$(PROJECT_SRCS:.c=.o))
SHARED_SRCS := protocol/crc16.c protocol/frame.c protocol/stream_parser.c protocol/reliability.c pico2/pico2_protocol.c
BASELINE_SRCS :=
ifeq ($(APP_SENSOR_EMULATOR),1)
SHARED_SRCS += pico1/pico1_protocol.c
BASELINE_SRCS := demo/mock_logger.c
endif
SHARED_OBJS := $(addprefix ./mtkernel_3/uart_shared/,$(SHARED_SRCS:.c=.o))
BASELINE_OBJS := $(addprefix ./mtkernel_3/uart_baseline/,$(BASELINE_SRCS:.c=.o))
OBJS += $(PROJECT_OBJS) $(SHARED_OBJS) $(BASELINE_OBJS)
C_DEPS += $(PROJECT_OBJS:.o=.d) $(SHARED_OBJS:.o=.d) $(BASELINE_OBJS:.o=.d)
mtkernel_3/uart/%.o: ../../%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
mtkernel_3/uart_shared/%.o: ../../../shared/uart_protocol/src/%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
mtkernel_3/uart_baseline/%.o: ../../../uart_protocol/src/%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
.PHONY: uart-config
uart-config:
	@echo 'PROFILE: TARGET=$(TARGET) SMP=$(SMP) CONSOLE=$(CONSOLE) UART_FAULT=$(UART_FAULT) SENSOR_EMULATOR=$(APP_SENSOR_EMULATOR) DIAGNOSTICS=$(APP_VERBOSE_DIAGNOSTICS) WIFI=$(WIFI)'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'ASFLAGS=$(ASFLAGS)'
