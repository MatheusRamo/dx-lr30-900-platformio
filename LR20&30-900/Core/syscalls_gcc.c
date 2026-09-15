#include <stdint.h>

#include "driver_usart.h"

int _write(int file, char *data, int length)
{
    (void)file;
    HAL_UART_Transmit(&husart, (uint8_t *)data, (uint16_t)length, HAL_MAX_DELAY);
    return length;
}
