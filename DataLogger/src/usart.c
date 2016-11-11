/*
 * usart.c
 *
 *  Created on: 2. 11. 2016
 *      Author: priesolv
 */

#include "usart.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rtc.h"
#include "FlashG25D10B.h"

#define WAKEUP_INTERVAL_S    10  // 10 seconds
//#define WAKEUP_INTERVAL_S    (30 * 60)  // 30 minut

#define BUFFER_SIZE  128

uint8_t g_BufferIn[BUFFER_SIZE];
uint8_t g_BufferInPos;
bool    g_bCommandReady;
uint16_t g_nWakeUpInterval = WAKEUP_INTERVAL_S;

const uint8_t T_Version[] = "DATA LOGGER v0.1";
const uint8_t T_NewLine[] = "\r\n";


void USART_Configure_GPIO(void)
{
  /* Enable the peripheral clock of GPIOA */
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN;

  /* GPIO configuration for USART2 signals */
  /* (1) Select AF mode (10) on PA9 and PA10 */
  /* (2) AF4 for USART2 signals */
  GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODE9|GPIO_MODER_MODE10))\
                 | (GPIO_MODER_MODE9_1 | GPIO_MODER_MODE10_1); /* (1) */
  GPIOA->AFR[1] = (GPIOA->AFR[1] &~ (0x00000FF0))\
                  | (4 << (1 * 4)) | (4 << (2 * 4)); /* (2) */
}

void USART_Configure(void)
{
  /* Enable the peripheral clock USART2 */
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

  /* Configure USART2 */
  /* (1) oversampling by 16, 9600 baud */
  /* (2) 8 data bit, 1 start bit, 1 stop bit, no parity */
//  USART2->BRR = 160000 / 96; /* (1) */
  USART2->BRR = 218;
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE; /* (2) */

  /* polling idle frame Transmission */
  while((USART2->ISR & USART_ISR_TC) != USART_ISR_TC)
  {
    /* add time out here for a robust application */
  }

  g_bCommandReady = false;

  USART2->CR1 |= USART_CR1_RXNEIE;      // ebale RX INT

//  USART2->ICR = USART_ICR_TCCF;/* clear TC flag */
//  USART2->CR1 |= USART_CR1_TCIE;/* enable TC interrupt */

  NVIC_SetPriority(USART2_IRQn, 1);
  NVIC_EnableIRQ(USART2_IRQn);
}


// https://gist.github.com/adnbr/2629767
void USART_ProcessCommand()
{
  if (!g_bCommandReady)
  {
    return;
  }

  strupr((char*)g_BufferIn);
  USART_PrintLine(g_BufferIn);
  switch (g_BufferIn[0])
  {
    case 0:
    case 'S':
      USART_SendStatus();
      break;
    case 'L':
      USART_SendList();
      break;
    case 'D':
      USART_SetDate();
      break;
    case 'T':
      USART_SetTime();
      break;
    case 'I':
      USART_SetWakeUpInterval();
      break;
    case 'X':
      USART_EraseMemory();
      break;
    default:
      USART_PrintLine((uint8_t*)"???");
      break;
  }

  g_BufferInPos = 0;
  g_bCommandReady = false;
  USART2->CR1 |= USART_CR1_RXNEIE;
  USART_Putc('>');
}

void USART_PrintHeader(uint32_t nRecords, uint32_t nBatVoltage)
{
  uint8_t text[128];
  USART_PrintNewLine();
  USART_PrintLine(T_Version);

  snprintf((char*)text, sizeof (text), "Battery:%lu(mV)", nBatVoltage);
  USART_PrintLine(text);

  if (nRecords == 0xFFFFFFFF)
  {
    snprintf((char*)text, sizeof (text), "FULL MEMORY !!!");
  }
  else
  {
    snprintf((char*)text, sizeof (text), "Number of records:%lu", nRecords);
  }

  USART_PrintLine(text);
}

void USART_SendStatus()
{
  uint8_t text[128];
  USART_PrintLine(T_Version);
  snprintf((char*)text, sizeof (text), "Pocet zaznamu: %d", 0);
  USART_PrintLine(text);
}

void USART_SendList()
{
  App_PrintRecords();

}

void USART_SetDate()
{
  rtc_date_t date;

  if (atoi((char*)&g_BufferIn[1]))  // pokud je prvn� hodnota platn� ��slo
  {
    const char s[2] = ".";
    char *pos = strtok((char*)&g_BufferIn[1], s); // find first occure
    uint8_t day = atoi(pos);

    pos = strtok(NULL, s); // find first occure
    uint8_t month = atoi(pos);

    pos = strtok(NULL, s);
    uint8_t year = atoi(pos);

    date.day10 = day / 10;
    date.day = day - date.day10 * 10;

    date.month10 = month / 10;
    date.month = month - date.month10 * 10;

    date.year10 = year / 10;
    date.year = year - date.year10 * 10;

    date.week_day = 1;
    RTC_Set(NULL, &date);
  }

  USART_PrintDateTime();
}

void USART_SetTime()
{
  rtc_time_t time;

  if (atoi((char*)&g_BufferIn[1]))  // pokud je prvn� hodnota platn� ��slo
  {
    const char s[2] = ":";
    char *pos = strtok((char*)&g_BufferIn[1], s); // find first occure
    uint8_t hour = atoi(pos);

    pos = strtok(NULL, s);  // find first occure
    uint8_t minute = atoi(pos);

    pos = strtok(NULL, s);
    uint8_t second = atoi(pos);

    time.hour10 = hour / 10;
    time.hour = hour - time.hour10 * 10;

    time.minute10 = minute / 10;
    time.minute = minute - time.minute10 * 10;

    time.second10 = second / 10;
    time.second = second - time.second10 * 10;

    RTC_Set(&time, NULL);
  }

  USART_PrintDateTime();
}

void USART_EraseMemory()
{
  if (g_BufferIn[1] == '+' && g_BufferIn[2] == 'X')
  {
    // vymazat pamet
    for (uint8_t i = 0; i < 32; i++)
    {
      FlashG25D10_SectorErase(i);
      USART_Putc('.');
    }

    USART_PrintNewLine();
    USART_PrintLine((uint8_t*)"Memory is erased");
  }
}

void USART_SetWakeUpInterval()
{
  if (g_BufferIn[1])
  {
    uint16_t nInterval = atoi((char*)&g_BufferIn[1]);
    if (nInterval)
    {
      g_nWakeUpInterval = nInterval * 60;
    }
  }

  uint8_t text[20];
  snprintf((char*)text, sizeof(text), "Interval=%d minut", g_nWakeUpInterval / 60);
  USART_PrintLine(text);
}

uint16_t USART_GetWakeUpInterval()
{
  return g_nWakeUpInterval;
}

void USART_PrintDateTime()
{
  uint8_t text[20];
  RTC_PrintDT(text, sizeof(text));
  USART_PrintLine(text);
}

void USART_Putc(uint8_t ch)
{
  while (!(USART2->ISR & USART_ISR_TXE));
  USART2->TDR = ch;
}

void USART_Puts(const uint8_t* text)
{
  while (*text)
  {
    USART_Putc(*text++);
  }
}

void USART_PrintLine(const uint8_t* text)
{
  USART_Puts(text);
  USART_Puts(T_NewLine);
}

void USART_PrintNewLine()
{
  USART_Puts(T_NewLine);
}

void USART_WaitForTC()
{
  while (!(USART2->ISR & USART_ISR_TC));
}

void USART2_IRQHandler(void)
{
  if (g_bCommandReady)
  {
    return;
  }

  if (USART2->ISR & USART_ISR_RXNE)
  {
    RTC_SetUsartTimer(60000);
    g_BufferIn[g_BufferInPos] = USART2->RDR;

    // End of line!
    if (g_BufferIn[g_BufferInPos] == '\r')
    {
      g_BufferIn[g_BufferInPos] = 0;
      USART2->CR1 &= ~USART_CR1_RXNEIE;     // disable RX INT
      g_bCommandReady = true;
    }

    if (g_BufferInPos < (BUFFER_SIZE - 1))
    {
      g_BufferInPos++;
    }
  }
}
