#pragma once
#include <cstdint>
#include <cstddef>
typedef int uart_port_t; typedef int esp_err_t;
#define UART_NUM_1 1
#define UART_NUM_2 2
#define ESP_OK 0
#define UART_PIN_NO_CHANGE -1
enum {UART_DATA_8_BITS,UART_PARITY_DISABLE,UART_STOP_BITS_2,UART_HW_FLOWCTRL_DISABLE,UART_SCLK_DEFAULT};
enum uart_event_type_t {UART_DATA,UART_BREAK,UART_BUFFER_FULL,UART_FIFO_OVF,UART_FRAME_ERR};
struct uart_event_t{uart_event_type_t type; size_t size;};
struct uart_config_t{int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;};
int uart_write_bytes(uart_port_t, const char*, size_t);
inline const char* esp_err_to_name(int){return "";}
inline int uart_driver_install(...){return 0;} inline int uart_param_config(...){return 0;} inline int uart_set_pin(...){return 0;}
inline int uart_set_rx_full_threshold(...){return 0;} inline int uart_set_rx_timeout(...){return 0;}
inline int uart_get_buffered_data_len(int, size_t*n){*n=0;return 0;} inline int uart_read_bytes(...){return 0;}
inline int uart_flush_input(int){return 0;}
