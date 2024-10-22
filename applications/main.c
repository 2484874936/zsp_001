/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-01-16     RT-Thread    first version
 */

#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
#include <myled.h>
#include "bsp_uart.h"
#include "app_m5311.h"

int main(void)
{
//    flash_init();
    uart_init();
    led_init();
    if( m5311_moudle_init(INIT_BLINK_LED) != RT_EOK)
    {
        rt_uint8_t onchip_led_buf[8];
        stm32_flash_read(ON_CHIP_FAL_OFFSET_ADDR, onchip_led_buf, 8);
        if(onchip_led_buf[0] == 0xAA && onchip_led_buf[1] == 0x55)
       {
            uint8_t Calibration=0;
           for(uint8_t i = 0; i < 7; i ++)
           {
               rt_kprintf("buffer[%d] = 0x%02x\n",i,onchip_led_buf[i]);
               Calibration ^= onchip_led_buf[i];
           }
           rt_kprintf("buffer[%d] = 0x%02x\n",7,onchip_led_buf[7]);
           if(Calibration == onchip_led_buf[+7])
           {
               g_rowled_data1_16.word32 = (rt_uint32_t)onchip_led_buf[2] \
                                        | (rt_uint32_t)onchip_led_buf[3] << 8 \
                                        | (rt_uint32_t)onchip_led_buf[4] << 16 \
                                        | (rt_uint32_t)onchip_led_buf[5] << 24;
               g_rowled_data17_18.word32 = (rt_uint32_t)onchip_led_buf[6];
               rt_kprintf("Calibration success,g_rowled_data=0x%010X\n",g_rowled_data1_16.word32);
           }
       }
        else
        {
            g_rowled_data1_16.word32 = 0xffffffff;
            g_rowled_data17_18.word32 = 0x0000000f;
        }
        set_led();
    }
    int count=0;
    for(;;)
    {
//        if(send_at("STAT: 5\r\n",1000,1,"AT+MQTTSTAT?\r\n") != RT_EOK)
//        {
//            count++;
//            if(count == 3)
//            {
//                count = 0;
//                rt_kprintf("MQTT RECONNECTING...\n");
//                if(m5311_moudle_init(REINIT_BLINK_LED) != RT_EOK)
//                {
//                    rt_kprintf("MQTT RECONNECT ERROR\n");
//                }
//                else
//                {
//                    rt_kprintf("MQTT RECONNECT SUCCESS\n");
//                }
//            }
//        }
//        else
//        {
//            count = 0;
//        }
//        set_led();
        static uint32_t heart_wait_cnt = 0,heart_error_cnt = 0;
        heart_wait_cnt ++;
        if(heart_wait_cnt == 600)
        {
            heart_wait_cnt = 0;
            if(mqtt_heart() == RT_EOK)
            {
                rt_kprintf("***heart success***\n");
                heart_error_cnt = 0;
            }
            else
            {
                heart_error_cnt ++;
                rt_kprintf("***heart error***\n");
            }
            if(heart_error_cnt>3)
            {
                heart_error_cnt = 0;
                rt_kprintf("MQTT RECONNECTING...\n");
                if(m5311_moudle_init(REINIT_BLINK_LED) != RT_EOK)
                {
                    rt_kprintf("MQTT RECONNECT ERROR\n");
                }
                else
                {
                    rt_kprintf("MQTT RECONNECT SUCCESS\n");
                }
            }
        }
        rt_thread_mdelay(1000);
    }
    return RT_EOK;
}
