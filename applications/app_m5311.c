 /*
 * Change Logs:
 * Date           Author       Notes
 * 2023-01-30     afun       the first version
 */
#include "app_m5311.h"
#include <myled.h>
#include <string.h>
//#define LOG_TAG "M5311"
//#define LOG_LVL LOG_LVL_DBG
//#include "ulog.h"

rt_base_t m5311_wakeup_pin = GET_PIN(A,4);
rt_base_t m5311_pwr_pin    = GET_PIN(A,5);
rt_base_t um5311_reset_pin = GET_PIN(A,6);
static struct M5311_Modle m5311_modle;
static char IMEI[18] = {0};
static char urctopiccompare[19] = {0};
static _Bool get_IMEI_flag;



void m5311_wakeup(void);
void m5311_pwron(void);
void m5311_pwroff(void);
void m5311_reset(void);
rt_size_t send_at(char *ack, rt_uint32_t timeout, int num, ...);

int m5311_moudle_init(_Bool init_flag)
{
    rt_uint8_t cnt = 0;
    rt_pin_mode( m5311_wakeup_pin, PIN_MODE_OUTPUT);
    rt_pin_write(m5311_wakeup_pin, PIN_HIGH);
    rt_pin_mode( m5311_pwr_pin, PIN_MODE_OUTPUT);
    rt_pin_write(m5311_pwr_pin, PIN_HIGH);
    rt_pin_mode( um5311_reset_pin, PIN_MODE_OUTPUT);
    rt_pin_write(um5311_reset_pin, PIN_HIGH);
    m5311_pwron();
    m5311_reset();
    if(init_flag)
    {
        g_rowled_data1_16.word32 = 0;
        g_rowled_data17_18.word32 = 0;
        set_led();
        easyblink(g_led1,-1,200,400);
    }
    while(send_at("OK",500,1,"AT\r\n") != RT_EOK);//等待模组正常
    {
        cnt ++;
        if(cnt == 10)
        {
            return RT_ERROR;
        }
    }
    send_at("OK",500,1,"AT*CMBAND=?\r\n");//查看模组使能频段
    send_at("OK",500,1,"AT+CGCONTRDP\r\n");//查看模组使能频段
    send_at("OK",500,1,"AT+CFUN=1\r\n");
//    send_at("OK",500,1,"AT+CLPLMN\r\n");//清除驻网记录
    rt_thread_mdelay(5000);
    if(init_flag)
    {
        easyblink_stop(g_led1);
        eb_led_on(g_led1);
        easyblink(g_led2,-1,200,400);
    }
    cnt = 0;
//    while(send_at("OK\r\n",100,1,"ATE1\r\n") != RT_EOK);

    while(send_at("OK\r\n",100,1,"ATE0\r\n") != RT_EOK);//ATE0:关闭回显， ATE1:打开回显
    while(send_at("OK\r\n",100,1,"AT+SM=LOCK_FOREVER\r\n") != RT_EOK);//关闭模组休眠，掉电保持
//    while(send_at("OK\r\n",100,1,"AT+CEDRXS=0,5\r\n") != RT_EOK);//关闭eDXR模式
//    while(send_at("OK\r\n",100,1,"AT+CPSMS=1,,,\"00100010\",\"00101111\"\r\n") != RT_EOK);//使能PSM模式

    get_IMEI_flag = 1;
    while(send_at("86",100,1,"AT+GSN\r\n") != RT_EOK);//获取IMEI
    get_IMEI_flag = 0;
    if(init_flag)
    {
        easyblink_stop(g_led2);
        eb_led_on(g_led2);
        easyblink(g_led3,-1,200,400);
    }
    while(send_at("OK\r\n",1000,1,"AT+CIMI\r\n") != RT_EOK);//读SIM卡正常，获取IMSI
    while(send_at("OK\r\n",1000,1,"AT+SWVER\r\n") != RT_EOK);
    while(send_at("OK\r\n",1000,1,"AT+CMVER\r\n") != RT_EOK);
    if(init_flag)
    {
        easyblink_stop(g_led3);
        eb_led_on(g_led3);
        easyblink(g_led4,-1,200,400);
    }
    while(send_at(",1\r\n",1000,1,"AT+CEREG?\r\n") != RT_EOK)//确认基站注册状态，1-代表本地已注册上， 5-代表漫游已注册上
    {
        send_at("\r\n",1000,1,"AT+CPIN?\r\n");
        while(send_at("OK\r\n",1000,1,"AT+CEREG=1\r\n") != RT_EOK);
    }
    if(init_flag)
    {
        easyblink_stop(g_led4);
        eb_led_on(g_led4);
        easyblink(g_led5,-1,200,400);
    }
    while(send_at("+CGATT: 1\r\n",1000,1,"AT+CGATT?\r\n") != RT_EOK);//确认 PDP 激活状态，1-代表已激活 0-代表未激活
    if(init_flag)
    {
        easyblink_stop(g_led5);
        eb_led_on(g_led5);
        easyblink(g_led6,-1,200,400);
    }
    while(send_at("OK\r\n",100,1,"AT+CMSYSCTRL=0,2\r\n") != RT_EOK);
    if(init_flag)
    {
        easyblink_stop(g_led6);
        eb_led_on(g_led6);
        easyblink(g_led7,-1,200,400);
    }
    while(send_at("OK\r\n",100,1,"AT+MQTTPING=1\r\n") != RT_EOK);
//    send_at("OK\r\n",100,1,"AT+MQTTPING?\r\n");
    if(init_flag)
    {
        easyblink_stop(g_led7);
        eb_led_on(g_led7);
        easyblink(g_led8,-1,200,400);
    }
    return MQTT_connect(init_flag);

}

int MQTT_connect(_Bool init_flag)
{
    rt_uint8_t cnt = 0;
    while(send_at("+CGPADDR: 1",2000,1,"AT+CGPADDR=1\r\n") != RT_EOK)//获取网络IP
    {
       cnt ++;
       if(cnt ==10)
       {
           rt_kprintf("Can't get IP, M5311 havn't connect net\n");
           return RT_ETIMEOUT;
       }
    }
    cnt = 0;
    if(init_flag)
    {
        easyblink_stop(g_led8);
        eb_led_on(g_led8);
        easyblink(g_led9,-1,200,400);
    }
    send_at("OK\r\n",100,1,"AT+MQTTDISC\r\n");//断开MQTT连接
    send_at("OK\r\n",100,1,"AT+MQTTDEL\r\n");//删除MQTT client配置
    //    m5311_modle.mqtt_host = "\"101.69.254.66\"";
    //    m5311_modle.mqtt_port = "1883";
    //    m5311_modle.mqtt_clientid = IMEI;
    //    m5311_modle.keepalive = "120";
    //    m5311_modle.user = "\"\"";
    //    m5311_modle.passwd = "\"\"";
    //    m5311_modle.clean= "1";
    m5311_modle.mqtt_host = "\"jinchanhb.com\"";//正式服务器
    m5311_modle.mqtt_port = "19003";
/*   test url: http://jinchanhb.com:8500/test/test_light?topic=866469057983066&str=0100000100110000000000000000011000000000*/
//    m5311_modle.mqtt_host = "\"zz.zcczcc.com\"";//测试服务器
//    m5311_modle.mqtt_port = "9004";
/*   test url: http://zz.zcczcc.com:9531/test/test_light?topic=866469057983066&str=0100000000000000000000000000000000000000*/
    m5311_modle.mqtt_clientid = IMEI;
    m5311_modle.keepalive = "150";
    m5311_modle.user = "\"\"";
    m5311_modle.passwd = "\"\"";
    m5311_modle.clean= "1";
    do{
//        send_at("OK\r\n", 1000, 1,"AT+MQTTCFG=\"mqtts.heclouds.com\",1883,\"ML307Atest\",60,\"4C9H7yA5Td\",\"version=2018-10-31&res=products%2F4C9H7yA5Td&et=3035001667&method=md5&sign=Nwdp4I3qLHyCcqylshWRxw%3D%3D\",1\r\n");
       send_at("OK\r\n", 1000, 15, "AT+MQTTCFG=", m5311_modle.mqtt_host, DOUHAO, \
                         m5311_modle.mqtt_port, DOUHAO, m5311_modle.mqtt_clientid, DOUHAO, \
                         m5311_modle.keepalive, DOUHAO, m5311_modle.user, DOUHAO, \
                         m5311_modle.passwd, DOUHAO, m5311_modle.clean,"\r\n");//进行MQTT client配置
       //手册测试条例
    //        send_at("OK", 1000, 1,"AT+MQTTCFG=\"183.230.40.39\",6002,\"4069959\",60,\"75829\",\"IIOu0oFUg1guk20ornTK1uzAcnM=\",1\r\n");//连接MQTT
       cnt ++;
       if(cnt == 10)
       {
           rt_kprintf("connect mqtt error!\n");
           return RT_ERROR;
       }
    } while(send_at("+MQTTSTAT: 1\r\n",1000,1,"AT+MQTTSTAT?\r\n") != RT_EOK);//若client参数未初始化
    cnt = 0;
    if(init_flag)
    {
        easyblink_stop(g_led9);
        eb_led_on(g_led9);
        easyblink(g_led10,-1,200,400);
    }

    m5311_modle.usrflag = "0";
    m5311_modle.pwdflag = "0";
    m5311_modle.willflag = "0";
    m5311_modle.willretain = "0";
    m5311_modle.willqos = "1";
    m5311_modle.willtopic = "\"\"";
    m5311_modle.willmessage = "\"\"";

    do{
//        send_at("+MQTTOPEN: OK\r\n", 1000, 1,"AT+MQTTOPEN=1,1,0,0,0,\"\",\"\"\r\n");
    send_at("+MQTTOPEN: OK\r\n", 1000, 15, "AT+MQTTOPEN=", m5311_modle.usrflag, DOUHAO, \
                 m5311_modle.pwdflag, DOUHAO, m5311_modle.willflag, DOUHAO, \
                 m5311_modle.willretain, DOUHAO, m5311_modle.willqos, DOUHAO, \
                 m5311_modle.willtopic, DOUHAO, m5311_modle.willmessage,"\r\n");//连接MQTT
       cnt ++;
       if(cnt == 10)
       {
           rt_kprintf("mqtt open error for %d times!\n",cnt);
           return RT_ERROR;
       }
    } while(send_at("+MQTTSTAT: 5",1000,1,"AT+MQTTSTAT?\r\n") != RT_EOK);//若MQTT服务器未连接
    cnt = 0;
    if(init_flag)
    {
        easyblink_stop(g_led10);
        eb_led_on(g_led10);
    }
    send_at("OK\r\n",100,3,"AT+MQTTSUB=",IMEI,",1\r\n");
    if(init_flag)
    {
        g_rowled_data1_16.word32 = 0xffffffff;
        g_rowled_data17_18.word32 = 0x0000000f;
        set_led();
    }
    return RT_EOK;
}



int mqtt_heart(void)
{
    return send_at("HELLO_LED\r\n",2000,3,"AT+MQTTPUB=",IMEI,",1,0,0,0,HELLO_LED\r\n");
}


int uart2_data_processing(char *buffer, rt_size_t index)
{
#if defined(BSP_UART2_RX_USING_DMA)
    if(index)
#else
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
#endif
    {
        char *temp;
        rt_kprintf("|U2 RX:%s\r\n",buffer);

        if(m5311_modle.compare_str != RT_NULL)
        {
           temp = strstr(buffer,m5311_modle.compare_str);
           if(temp)
           {
               if(index>15 && get_IMEI_flag)
               {
                   IMEI[0] = '\"';
                   IMEI[16] = IMEI[0];
                   IMEI[17] = '\0';


                   for(rt_uint8_t i = 0; i < 15; i ++)
                   {
                       IMEI[i+1] = buffer[i + 2];

                   }
                   strcpy(urctopiccompare,IMEI);
                   urctopiccompare[0] = ',';
                   urctopiccompare[16] = urctopiccompare[0];
                   urctopiccompare[17] = '8';
                   urctopiccompare[18] = '\0';
                   rt_kprintf("IMEI=%s\n",IMEI);
                   rt_kprintf("urctopiccompare=%s\n",urctopiccompare);
               }
               rt_mb_send(G_UART_2.out_mb,temp);
           }
        }

        if(rt_strstr(buffer,"+MQTTPUBLISH:"))
        {
            uint8_t Calibration=0;
            uint16_t location=0;
//           rt_kprintf("+MQTTPUBLISH: location = %d\n",location-(uint16_t)buffer);
            location = rt_strstr(buffer,urctopiccompare) - buffer;
            if(location>0 && index >= location + sizeof(urctopiccompare)-1)
            {
                location += 19;
                rt_kprintf("location = %d\n",location);
                if(buffer[location+0] == 0xAA && buffer[location+1] == 0x55)
                {
                    for(uint8_t i = location; i < location+7; i ++)
                    {
                        rt_kprintf("buffer[%d] = 0x%02x\n",i,buffer[i]);
                        Calibration ^= buffer[i];
                    }
                    rt_kprintf("buffer[%d] = 0x%02x\n",location+7,buffer[location+7]);
                    if(Calibration == buffer[location+7])
                    {
                        g_rowled_data1_16.word32 = (rt_uint32_t)buffer[location+2] \
                                                 | (rt_uint32_t)buffer[location+3] << 8 \
                                                 | (rt_uint32_t)buffer[location+4] << 16 \
                                                 | (rt_uint32_t)buffer[location+5] << 24;
                        g_rowled_data17_18.word32 = (rt_uint32_t)buffer[location+6];
                        rt_kprintf("Calibration success,g_rowled_data=0x%010X\n",g_rowled_data1_16.word32);
                        rt_uint8_t onchip_led_buf[8];
                        stm32_flash_read(ON_CHIP_FAL_OFFSET_ADDR, onchip_led_buf, 8);
                        if(onchip_led_buf[0] != buffer[location + 0] \
                         || onchip_led_buf[1] != buffer[location + 1] \
                         || onchip_led_buf[2] != buffer[location + 2] \
                         || onchip_led_buf[3] != buffer[location + 3] \
                         || onchip_led_buf[4] != buffer[location + 4] \
                         || onchip_led_buf[5] != buffer[location + 5] \
                         || onchip_led_buf[6] != buffer[location + 6] \
                         || onchip_led_buf[7] != buffer[location + 7] )
                        {
                            stm32_flash_erase (ON_CHIP_FAL_OFFSET_ADDR,sizeof(onchip_led_buf));
                            stm32_flash_write(ON_CHIP_FAL_OFFSET_ADDR, buffer+location, 8);
                        }
    //                    g_rowled_data1_16.word32 = 0;
    //                    g_rowled_data17_18.word32 = 0;
                        set_led();
                    }
                }
            }
        }
        if(rt_strstr(buffer,"+MQTTSTAT: "))
        {
            rt_uint8_t m_index = 0;
            m_index = rt_strstr(buffer,"+MQTTSTAT: ") - buffer + 11 ;
//            if(m_index > 0)
//            {
//                LOG_W("BUFFER[%d] = %x",m_index,buffer[m_index]);
//            }
            switch(buffer[m_index])
            {
                case '0': rt_kprintf("Client parameter is not initialized\n"); break;
                case '1': rt_kprintf("Client parameter is initialized\n"); break;
                case '2': rt_kprintf("MQTT server is disconnnected\n"); break;
                case '3': rt_kprintf("Send a connect packet and wait for the MQTT receiving server to ack\n"); break;
                case '4': rt_kprintf("Reconnecting to MQTT server\n"); break;
                case '5': rt_kprintf("MQTT server is connnected\n"); break;
//                case '6': rt_kprintf("In establishing a TCP connection"); break;
//                case '7': rt_kprintf("TCP connection is established"); break;
                default:break;
            }
        }
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}


void m5311_wakeup(void)
{
    rt_pin_write(m5311_wakeup_pin, PIN_LOW);
    rt_thread_mdelay(55);
    rt_pin_write(m5311_wakeup_pin, PIN_HIGH);
    rt_thread_mdelay(10);
}


void m5311_pwron(void)
{
    rt_pin_write(m5311_pwr_pin, PIN_LOW);
    rt_thread_mdelay(2100);
    rt_pin_write(m5311_pwr_pin, PIN_HIGH);
    rt_thread_mdelay(10);
}

void m5311_pwroff(void)
{
    rt_pin_write(m5311_pwr_pin, PIN_LOW);
    rt_thread_mdelay(8100);
    rt_pin_write(m5311_pwr_pin, PIN_HIGH);
    rt_thread_mdelay(10);
}


void m5311_reset(void)
{
    rt_pin_write(um5311_reset_pin, PIN_LOW);
    rt_thread_mdelay(100);
    rt_pin_write(um5311_reset_pin, PIN_HIGH);
    rt_thread_mdelay(10);
}




//ack:需要对比的字符串
//timewait:等待AT回复的时间
//num:需要拼接的字符串个数
//...:n个需要被拼接的字符串
rt_size_t send_at(char *ack, rt_uint32_t timeout, int num, ...)
{
    RT_ASSERT(num);
    va_list args;

    rt_size_t length;
    static char at_send_buf[AT_SEND_BUF];
    rt_memset(at_send_buf, 0, AT_SEND_BUF);
    va_start(args,num);
    for(rt_uint8_t i = 0; i < num; i++)
    {
        char* str = va_arg(args,char*);
        length = rt_sprintf(at_send_buf, "%s%s", at_send_buf,str);
        if (length > AT_SEND_BUF - 1)
        {
            rt_kprintf("too long AT command\n");
            rt_memset(at_send_buf, 0, AT_SEND_BUF);
            return RT_ERROR;
        }
    }
    va_end(args);
    rt_kprintf("AT_command:%s",at_send_buf);
    G_UART_2.send(at_send_buf, length);
    if(strstr(ack,DENNGHAO))
    {
        m5311_modle.compare_str_location = at_send_buf;
    }
    else
    {
        m5311_modle.compare_str = ack;
    }

    if(m5311_modle.compare_str != RT_NULL)
    {

        if(rt_mb_recv(G_UART_2.out_mb, &m5311_modle.compare_str_location, timeout) == -RT_ETIMEOUT)
        {
            rt_kprintf("command [%s] ack timeout\n",at_send_buf);
            return -RT_ETIMEOUT;
        }
        m5311_modle.compare_str = RT_NULL;
        if(strstr(m5311_modle.compare_str_location,"ERR="))
        {
            rt_kprintf("command [%s] ack error\n",at_send_buf);
            return RT_ERROR;
        }
        else
            return RT_EOK;
//        rt_kprintf("A=%s\r\n",m5311_modle.compare_str_location);
    }
    else
    {
        rt_thread_mdelay(timeout);
        return RT_EOK;
    }
}
