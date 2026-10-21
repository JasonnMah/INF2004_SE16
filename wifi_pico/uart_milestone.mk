# Independent UART/IPC milestone; the physical boot regression stays available.
ifneq ($(SMP),1)
$(error UART integration requires SMP=1)
endif
ifneq ($(CONSOLE),usb_cdc)
$(error UART integration requires USB CDC diagnostics)
endif
ifneq ($(WIFI),none)
$(error HTTP/radio are outside this milestone)
endif
UART_FAULT ?= 0
ifeq ($(filter $(UART_FAULT),0 1 2 3),)
$(error UART_FAULT must be 0 normal, 1 command timeout, 2 receive timeout, or 3 lost END ACK)
endif
ifneq ($(PROFILE_ID),inf2004-uart-smp1-fault$(UART_FAULT)-v1)
$(error Use build_uart_loopback.ps1 to select an isolated object profile)
endif

EXE_FILE := wifi_pico_uart_loopback_f$(UART_FAULT)
CFLAGS += -DTM_CONSOLE_UART=0 -DUART_FAULT=$(UART_FAULT) -include ../../config/milestone1.h \
          -I../../app_program/uart -I../../app_program/platform/include -I../../../uart_protocol/src
OBJS := $(filter-out ./mtkernel_3/app_program/%,$(OBJS))
C_DEPS := $(filter-out ./mtkernel_3/app_program/%,$(C_DEPS))
PROJECT_SRCS := ../../app_program/uart_app_main.c \
                $(wildcard ../../app_program/uart/*.c) \
                ../../app_program/platform/rt_bridge.c ../../app_program/platform/protocol_adapter.c
PROJECT_OBJS := $(patsubst ../../app_program/%,./mtkernel_3/uart/%,$(PROJECT_SRCS:.c=.o))
BASELINE_SRCS := protocol/crc16.c protocol/frame.c protocol/stream_parser.c protocol/reliability.c \
                 pico1/pico1_protocol.c pico2/pico2_protocol.c demo/mock_logger.c
BASELINE_OBJS := $(addprefix ./mtkernel_3/uart_baseline/,$(BASELINE_SRCS:.c=.o))
OBJS += $(PROJECT_OBJS) $(BASELINE_OBJS)
C_DEPS += $(PROJECT_OBJS:.o=.d) $(BASELINE_OBJS:.o=.d)

mtkernel_3/uart/%.o: ../../app_program/%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
mtkernel_3/uart_baseline/%.o: ../../../uart_protocol/src/%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

.PHONY: uart-config
uart-config:
	@echo 'PROFILE: TARGET=$(TARGET) SMP=$(SMP) CONSOLE=$(CONSOLE) TM_CONSOLE_UART=0 UART_FAULT=$(UART_FAULT) WIFI=$(WIFI)'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'ASFLAGS=$(ASFLAGS)'
