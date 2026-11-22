# Included from a firmware Make profile. No kernel/SDK headers enter these sources.
# Caller supplies UART_PROTOCOL_ROOT, UART_LIBRARY_DIR and UART_ENDPOINTS.
UART_WIRE_SRCS := protocol/log_digest.c protocol/crc16.c protocol/frame.c protocol/stream_parser.c protocol/reliability.c
UART_SENSOR_SRCS := pico1/pico1_protocol.c
UART_GATEWAY_SRCS := pico2/pico2_protocol.c
UART_WIRE_OBJS := $(addprefix $(UART_LIBRARY_DIR)/,$(UART_WIRE_SRCS:.c=.o))
UART_SENSOR_OBJS := $(addprefix $(UART_LIBRARY_DIR)/,$(UART_SENSOR_SRCS:.c=.o))
UART_GATEWAY_OBJS := $(addprefix $(UART_LIBRARY_DIR)/,$(UART_GATEWAY_SRCS:.c=.o))
UART_LIBRARY_OBJS := $(UART_WIRE_OBJS) $(UART_GATEWAY_OBJS)
ifneq ($(filter sensor,$(UART_ENDPOINTS)),)
UART_LIBRARY_OBJS += $(UART_SENSOR_OBJS)
endif
UART_ENDPOINT_LIBS := $(addprefix $(UART_LIBRARY_DIR)/libuart_,$(addsuffix .a,$(UART_ENDPOINTS)))
OBJS += $(UART_ENDPOINT_LIBS) $(UART_LIBRARY_DIR)/libuart_wire.a
C_DEPS += $(UART_LIBRARY_OBJS:.o=.d)
$(UART_LIBRARY_DIR)/%.o: $(UART_PROTOCOL_ROOT)/src/%.c
	@mkdir -p "$(@D)"
	$(GCC) $(CFLAGS) -MMD -MP -Wall -Wextra -Werror -D$(TARGET) $(INCPATH) -I$(UART_PROTOCOL_ROOT)/src -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
$(UART_LIBRARY_DIR)/libuart_wire.a: $(UART_WIRE_OBJS)
	$(patsubst %gcc,%ar,$(GCC)) rcs "$@" $^
$(UART_LIBRARY_DIR)/libuart_sensor.a: $(UART_SENSOR_OBJS)
	$(patsubst %gcc,%ar,$(GCC)) rcs "$@" $^
$(UART_LIBRARY_DIR)/libuart_gateway.a: $(UART_GATEWAY_OBJS)
	$(patsubst %gcc,%ar,$(GCC)) rcs "$@" $^
.PHONY: uart-library-clean
clean: uart-library-clean
uart-library-clean:
	-$(RM) $(UART_LIBRARY_OBJS)

ifneq ($(filter sensor_batch,$(UART_ENDPOINTS)),)
C_DEPS += $(UART_LIBRARY_DIR)/batch/batch_sensor.d $(UART_SENSOR_OBJS:.o=.d)
endif
$(UART_LIBRARY_DIR)/libuart_sensor_batch.a: $(UART_LIBRARY_DIR)/batch/batch_sensor.o $(UART_SENSOR_OBJS)
	$(patsubst %gcc,%ar,$(GCC)) rcs "$@" $^
