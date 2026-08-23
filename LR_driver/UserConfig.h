



#ifndef __USER_CONFIG_H
#define __USER_CONFIG_H


#include "myqueue.h"
#include "sx126x.h"




//打开直接进行定频测试
#define TEST 0



//spi
#define LCC68_NSS_PORT   GPIOA
#define LCC68_NSS_PIN    GPIO_PIN_4

#define LCC68_SCK_PORT   GPIOA
#define LCC68_SCK_PIN    GPIO_PIN_5

#define LCC68_MOSI_PORT  GPIOA
#define LCC68_MOSI_PIN   GPIO_PIN_7

#define LCC68_MISO_PORT  GPIOA
#define LCC68_MISO_PIN   GPIO_PIN_6


#define LCC68_NRST_PORT GPIOA
#define LCC68_NRST_PIN  GPIO_PIN_3

#define LCC68_BUSY_PORT GPIOA
#define LCC68_BUSY_PIN  GPIO_PIN_2



#define LCC68_DIO1_PORT GPIOC
#define LCC68_DIO1_PIN  GPIO_PIN_15



//Uart
#define LOG_UART_TX_PORT GPIOA
#define LOG_UART_TX_PIN  GPIO_PIN_9
#define LOG_UART_RX_PORT GPIOA
#define LOG_UART_RX_PIN  GPIO_PIN_10


#define LCC68_RXEN_PORT GPIOA
#define LCC68_RXEN_PIN  GPIO_PIN_1

#define LCC68_TXEN_PORT GPIOA
#define LCC68_TXEN_PIN  GPIO_PIN_0





#define LORA_FRE									915000000	// frequency
#define LORA_PREAMBLE_LENGTH                        8        // PREAMBLE LENGTH
#define LORA_SX126x_SYMBOL_TIMEOUT                  0         // Symbols(SX126x)
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false			// PAYLOAD FIX LENGTH
#define LORA_IQ_INVERSION_ON                        false			// IQ INVERSION



#define SIZE_DATA  255



/*!
 * \brief Represents the operating mode the radio is actually running
 */
typedef enum
{
    MODE_SLEEP                              = 0x00,         //! The radio is in sleep mode
    MODE_STDBY_RC,                                          //! The radio is in standby mode with RC oscillator
    MODE_STDBY_XOSC,                                        //! The radio is in standby mode with XOSC oscillator
    MODE_FS,                                                //! The radio is in frequency synthesis mode
    MODE_TX,                                                //! The radio is in transmit mode
    MODE_RX,                                                //! The radio is in receive mode
    MODE_RX_DC,                                             //! The radio is in receive duty cycle mode
    MODE_CAD                                                //! The radio is in channel activity detection mode
}RadioOperatingModes_t;








extern uint8_t rxbuff[SIZE_DATA]; 
extern uint8_t DataLen;
extern uint8_t pdata;


extern uint8_t IrqFired;
extern sx126x_irq_mask_t radioFlag;
extern sx126x_rx_buffer_status_t offset;
extern sx126x_pkt_status_lora_t RadioPktStatus;







extern void Data_Processing(void);

extern void LoraInit(void);

extern void gpio_init(void);

extern void LoraDataSend(uint8_t *data,uint8_t len);
extern void DX_Lora_RadioIrqProcess(void);

extern RadioOperatingModes_t sx1262GetOperatingMode(void);

extern void sx1262SetOperatingMode(RadioOperatingModes_t mode);



#endif

