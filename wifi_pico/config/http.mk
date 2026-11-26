# Independent known-byte HTTP stage. No protocol or Sensor objects are linked.
ifneq ($(SMP)-$(CONSOLE)-$(WIFI)-$(WIFI_JOIN)-$(WIFI_NETIF)-$(WIFI_DHCP),1-usb_cdc-cyw43-1-1-1)
$(error HTTP test requires SMP=1 CONSOLE=usb_cdc WIFI=cyw43 WIFI_JOIN=1 WIFI_NETIF=1 WIFI_DHCP=1)
endif
ifneq ($(filter 1,$(WIFI_STATIC) $(WIFI_DNS) $(WIFI_UDP) $(WIFI_TCP) $(WIFI_TCPBULK)),)
$(error Do not include upstream network qualification/echo applications in HTTP test)
endif
HTTP_BUILD_CHECK ?= 0
ifneq ($(PROFILE_ID),inf2004-http-known-smp1-check$(HTTP_BUILD_CHECK)-v2)
$(error Use build.ps1 -Target http to select an isolated object profile)
endif
EXE_FILE := wifi_pico_http_known_bytes
ifeq ($(HTTP_BUILD_CHECK),1)
EXE_FILE := $(EXE_FILE)_buildcheck
endif
CFLAGS += -DTM_APP_HTTP=1 -DHTTP_BUILD_CHECK=$(HTTP_BUILD_CHECK) -DTM_CONSOLE_UART=0 \
 -DAPP_SENSOR_EMULATOR=0 -include ../../config/project.h \
 -I. -I../.. -I../../tests/firmware -I../../../sensor_pico/lib/uart_protocol/src
OBJS := $(filter-out ./mtkernel_3/app_program/%,$(OBJS))
C_DEPS := $(filter-out ./mtkernel_3/app_program/%,$(C_DEPS))
HTTP_SRCS := tests/firmware/http_test_main.c http_post.c http_service.c \
 kernel_io.c rx_ring.c buttons.c status.c
HTTP_OBJS := $(addprefix ./mtkernel_3/http_app/,$(HTTP_SRCS:.c=.o))
OBJS += $(HTTP_OBJS)
C_DEPS += $(HTTP_OBJS:.o=.d)
mtkernel_3/http_app/%.o: ../../%.c http_test_config.h
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
.PHONY: http-config
http-config:
	@echo 'PROFILE: SMP=$(SMP) CONSOLE=$(CONSOLE) WIFI=$(WIFI) HTTP_BUILD_CHECK=$(HTTP_BUILD_CHECK)'
	@echo 'CFLAGS=$(CFLAGS)'
	@echo 'ASFLAGS=$(ASFLAGS)'
