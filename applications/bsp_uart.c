#include "bsp_uart.h"
#include <stdlib.h>
#include <string.h>

//#define RT_SERIAL_CONFIG_DEFAULT    \
//{                                   \
//    115200,    /* 115200 bits/s */  \
//    8,          /* 8 databits */    \
//    0,      /* 1 stopbit */         \
//    0,      /* No parity  */        \
//    0,    /* LSB first sent */      \
//    0,       /* Normal mode */      \
//    1024, /* Buffer size, "+1" is Prepared to "\0" */ \
//    0                               \
//}
//
//struct serial_configure
//{
//    rt_uint32_t baud_rate;//波特率
//    rt_uint32_t data_bits :4;//数据位
//    rt_uint32_t stop_bits :2;//停止位
//    rt_uint32_t parity :2;//校验位
//    rt_uint32_t bit_order :1;
//    rt_uint32_t invert :1;
//    rt_uint32_t bufsz :16;
//    rt_uint32_t reserved :6;
//};

#if defined(USING_UART1)
void uart1_baud_rate_set(void);
struct rt_ringbuffer uart1_rb;
static rt_size_t uart1_send(void *data, rt_size_t size);
static rt_size_t uart1_recv(char *buffer, rt_int32_t timeout);
static rt_err_t uart1_callback(rt_device_t dev, rt_size_t size);
__WEAK int uart1_data_processing(char *buffer, rt_size_t index);
static int uart1_init(void);

uart_t G_UART_1 = {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        RT_NULL,
        uart1_send,
#if defined(BSP_UART1_RX_USING_DMA)
        uart1_recv,
#else
        NULL,
#endif
        uart1_callback,
        uart1_rb,
        uart1_init,
        uart1_data_processing};

static rt_size_t uart1_send(void *data, rt_size_t size)
{
    return rt_device_write(G_UART_1.serial, 0, data, size);
}


static rt_size_t uart1_recv(char *buffer, rt_int32_t timeout)
{
    rt_size_t len = 0;
#if defined(BSP_UART1_RX_USING_DMA)
    if (rt_mb_recv(G_UART_1.dma_mb, &len, timeout) != RT_EOK)
    {
        return 0;
    }
    len = rt_device_read(G_UART_1.serial, 0, buffer, len);
#endif
    return len;
}


/* 接收数据回调函数 */
static rt_err_t uart1_callback(rt_device_t dev, rt_size_t size)
{
#if defined(BSP_UART1_RX_USING_DMA)
    /* 发送邮件 */
    return rt_mb_send(G_UART_1.dma_mb, size);
#else
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    return rt_sem_release(G_UART_1.rx_sem);
#endif
}

__WEAK int uart1_data_processing(char *buffer, rt_size_t index)
{
#if defined(BSP_UART1_RX_USING_DMA)
    if(index)
#else
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
#endif
    {
        rt_kprintf("usart1 ");
        G_UART_1.send(buffer,index);
//        rt_mb_send(G_UART_1.out_mb, (rt_uint32_t)buffer);
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}

static void uart1_rev_thread(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    //    config.bufsz = 100;//设置接收缓冲区大小,不设置则默认大小
    char uart1rev[config.bufsz];
    memset(uart1rev,0,config.bufsz);
    rt_size_t len1=0;
    char ch;

    for(;;)
    {
#if defined(BSP_UART1_RX_USING_DMA)
        //DMA邮箱数据处理中心
        len1 = G_UART_1.recv((char *)&uart1rev,RT_WAITING_FOREVER);
        if(len1)
        {
            len1 = G_UART_1.data_processing((char *)&uart1rev,len1);
        }
#else
        //中断数据处理中心
        /* 从串口读取一个字节的数据，没有读取到则等待接收信号量 */
        while (rt_device_read(G_UART_1.serial, -1, &ch, 1) != 1)
        {
            /* 阻塞等待接收信号量，等到信号量后再次读取数据 */
            rt_sem_take(G_UART_1.rx_sem, RT_WAITING_FOREVER);
        }
        /* 读取到的数据通过串口输出 */
//        G_UART_1.send(&ch,1);
        uart1rev[len1] = ch;
        len1 %= config.bufsz-1;
        len1 ++;
        len1 = G_UART_1.data_processing((char *)&uart1rev,len1);
#endif
    }
}


int uart1_init(void)
{
   /* 查找系统中的串口设备 */
    G_UART_1.serial = rt_device_find(UART1_NAME);
    if (!G_UART_1.serial)
    {
        rt_kprintf("find %s failed!\n", UART1_NAME);
        return RT_ERROR;
    }
//    uart1_baud_rate_set();
    uart_baud_rate_set(G_UART_1,UART1_DEFAULT_BAUD_RATE);
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(G_UART_1.serial, uart1_callback);
#if defined(BSP_UART1_RX_USING_DMA)
    /* 以DMA接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_1.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX );
#else
    /* 以中断接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_1.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
#endif
    /* 初始化输出邮箱 */
    if (G_UART_1.out_mb == RT_NULL)
    {
        G_UART_1.out_mb = rt_mb_create("uart1_out_mb", 2, RT_IPC_FLAG_FIFO);
        if (G_UART_1.out_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化DMA邮箱 */
    if (G_UART_1.dma_mb == RT_NULL)
    {
        G_UART_1.dma_mb = rt_mb_create("uart1_dma_mb", 10, RT_IPC_FLAG_FIFO);
        if (G_UART_1.dma_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化信号量 */
    if (G_UART_1.rx_sem == RT_NULL)
    {
        G_UART_1.rx_sem = rt_sem_create( "uart1_rx_sem", 0, RT_IPC_FLAG_FIFO);
        if (G_UART_1.rx_sem == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 创建 serial 线程 */
    rt_thread_t thread = rt_thread_create("serial1", uart1_rev_thread, RT_NULL, 2048, 21, 10);
    /* 创建成功则启动线程 */
    if (thread != RT_NULL)
    {
       rt_thread_startup(thread);
    }
    else
    {
       rt_kprintf("Create %s Entry failed!\n", UART1_NAME);
       return RT_ERROR;
    }
    return RT_EOK;
}
#endif



#if defined(USING_UART2)
static rt_size_t uart2_send(void *data, rt_size_t size);
static rt_size_t uart2_recv(char *buffer, rt_int32_t timeout);
static rt_err_t uart2_callback(rt_device_t dev, rt_size_t size);
__WEAK int uart2_data_processing(char *buffer, rt_size_t index);
static int uart2_init(void);

uart_t G_UART_2 = {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        RT_NULL,
        uart2_send,
#if defined(BSP_UART2_RX_USING_DMA)
        uart2_recv,
#else
        NULL,
#endif
        uart2_callback,
        NULL,
        uart2_init,
        uart2_data_processing };

static rt_size_t uart2_send(void *data, rt_size_t size)
{
    return rt_device_write(G_UART_2.serial, 0, data, size);
}


static rt_size_t uart2_recv(char *buffer, rt_int32_t timeout)
{
    rt_size_t len = 0;
#if defined(BSP_UART2_RX_USING_DMA)
    if (rt_mb_recv(G_UART_2.dma_mb, &len, timeout) != RT_EOK)
    {
        return 0;
    }
    len = rt_device_read(G_UART_2.serial, 0, buffer, len);
#endif
    return len;
}


/* 接收数据回调函数 */
static rt_err_t uart2_callback(rt_device_t dev, rt_size_t size)
{
#if defined(BSP_UART2_RX_USING_DMA)
    /* 发送邮件 */
    return rt_mb_send(G_UART_2.dma_mb, size);
#else
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    return rt_sem_release(G_UART_2.rx_sem);
#endif
}

__WEAK int uart2_data_processing(char *buffer, rt_size_t index)
{
#if defined(BSP_UART2_RX_USING_DMA)
    if(index)
#else
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
#endif
    {
        rt_kprintf("%s",buffer);
//        rt_uprintf(G_UART_2, "usart2 ");
//        G_UART_2.send(buffer,index);
//        rt_mb_send(G_UART_2.out_mb, (rt_uint32_t)buffer);
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}

static void uart2_rev_thread(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    //    config.bufsz = 100;//设置接收缓冲区大小,不设置则默认大小
    char uart2rev[config.bufsz];
    memset(uart2rev,0,config.bufsz);
    rt_size_t len2=0;

    for(;;)
    {
#if defined(BSP_UART2_RX_USING_DMA)
        //DMA邮箱数据处理中心
        len2 = G_UART_2.recv(uart2rev,4);
        if(len2)
        {
            rt_ringbuffer_put(G_UART_2.rb,(uint8_t *)uart2rev,len2);
        }
        else
        {
            len2 = rt_ringbuffer_data_len(G_UART_2.rb);
            if(len2)
            {
                len2 = rt_ringbuffer_get(G_UART_2.rb,(uint8_t *)uart2rev,len2);
                G_UART_2.data_processing(uart2rev,len2);
            }
        }
#else
        //中断数据处理中心
        /* 从串口读取一个字节的数据，没有读取到则等待接收信号量 */
        while (rt_device_read(G_UART_2.serial, -1, &ch, 1) != 1)
        {
            /* 阻塞等待接收信号量，等到信号量后再次读取数据 */
            rt_sem_take(G_UART_2.rx_sem, RT_WAITING_FOREVER);
        }
        /* 读取到的数据通过串口输出 */
//        G_UART_2.send(&ch,1);
        uart2rev[len2] = ch;
        len2 %= config.bufsz-1;
        len2 ++;
        len2 = G_UART_2.data_processing((char *)&uart2rev,len2);
#endif
    }
}


int uart2_init(void)
{
    /* 查找系统中的串口设备 */
    G_UART_2.serial = rt_device_find(UART2_NAME);
    if (!G_UART_2.serial)
    {
        rt_kprintf("find %s failed!\n", UART2_NAME);
        return RT_ERROR;
    }
    uart_baud_rate_set(G_UART_2,UART2_DEFAULT_BAUD_RATE);
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(G_UART_2.serial, uart2_callback);
#if defined(BSP_UART2_RX_USING_DMA)
    /* 以DMA接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_2.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX );
#else
    /* 以中断接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_2.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
#endif
    /* 初始化输出邮箱 */
    if (G_UART_2.out_mb == RT_NULL)
    {
        G_UART_2.out_mb = rt_mb_create("uart2_out_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_2.out_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化DMA邮箱 */
    if (G_UART_2.dma_mb == RT_NULL)
    {
        G_UART_2.dma_mb = rt_mb_create("uart2_dma_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_2.dma_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化信号量 */
    if (G_UART_2.rx_sem == RT_NULL)
    {
        G_UART_2.rx_sem = rt_sem_create( "uart2_rx_sem", 0, RT_IPC_FLAG_FIFO);
        if (G_UART_2.rx_sem == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    //初始化ringbuf
    G_UART_2.rb = rt_ringbuffer_create(RT_SERIAL_RB_BUFSZ);
    rt_ringbuffer_reset(G_UART_2.rb);
    /* 创建 serial 线程 */
    rt_thread_t thread = rt_thread_create("serial2", uart2_rev_thread, RT_NULL, 1024, 23, 50);
    /* 创建成功则启动线程 */
    if (thread != RT_NULL)
    {
       rt_thread_startup(thread);
       return RT_EOK;
    }
    else
    {
       rt_kprintf("Create %s Entry failed!\n", UART2_NAME);
       return RT_ERROR;
    }
}
#endif



#if defined(USING_UART3)

struct rt_ringbuffer uart3_rb;
static rt_size_t uart3_send(void *data, rt_size_t size);
static rt_size_t uart3_recv(char *buffer, rt_int32_t timeout);
static rt_err_t uart3_callback(rt_device_t dev, rt_size_t size);
__WEAK int uart3_data_processing(char *buffer, rt_size_t index);
static int uart3_init(void);

uart_t G_UART_3 = {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        RT_NULL,
        uart3_send,
#if defined(BSP_UART3_RX_USING_DMA)
        uart3_recv,
#else
        NULL,
#endif
        uart3_callback,
        uart3_rb,
        uart3_init,
        uart3_data_processing};

static rt_size_t uart3_send(void *data, rt_size_t size)
{
    return rt_device_write(G_UART_3.serial, 0, data, size);
}


static rt_size_t uart3_recv(char *buffer, rt_int32_t timeout)
{
    rt_size_t len = 0;
#if defined(BSP_UART3_RX_USING_DMA)
    if (rt_mb_recv(G_UART_3.dma_mb, &len, timeout) != RT_EOK)
    {
        return 0;
    }
    len = rt_device_read(G_UART_3.serial, 0, buffer, len);
#endif
    return len;
}


/* 接收数据回调函数 */
static rt_err_t uart3_callback(rt_device_t dev, rt_size_t size)
{
#if defined(BSP_UART3_RX_USING_DMA)
    /* 发送邮件 */
    return rt_mb_send(G_UART_3.dma_mb, size);
#else
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    return rt_sem_release(G_UART_3.rx_sem);
#endif
}


__WEAK int uart3_data_processing(char *buffer, rt_size_t index)
{
#if defined(BSP_UART3_RX_USING_DMA)
    if(index)
#else
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
#endif
    {
        rt_uprintf(G_UART_3, "usart3 ");
        G_UART_3.send(buffer,index);
//        rt_mb_send(G_UART_3.out_mb, (rt_uint32_t)buffer);
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}

static void uart3_rev_thread(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    //    config.bufsz = 100;//设置接收缓冲区大小,不设置则默认大小
    char uart3rev[config.bufsz];
    memset(uart3rev,0,config.bufsz);
    rt_size_t len3=0;
    char ch;

    for(;;)
    {
#if defined(BSP_UART3_RX_USING_DMA)
        //DMA邮箱数据处理中心
        len3 = G_UART_3.recv((char *)&uart3rev,RT_WAITING_FOREVER);
        if(len3)
        {
            len3 = G_UART_3.data_processing((char *)&uart3rev,len3);
        }
#else
        //中断数据处理中心
        /* 从串口读取一个字节的数据，没有读取到则等待接收信号量 */
        while (rt_device_read(G_UART_3.serial, -1, &ch, 1) != 1)
        {
            /* 阻塞等待接收信号量，等到信号量后再次读取数据 */
            rt_sem_take(G_UART_3.rx_sem, RT_WAITING_FOREVER);
        }
        /* 读取到的数据通过串口输出 */
//        G_UART_3.send(&ch,1);
        uart3rev[len3] = ch;
        len3 %= config.bufsz-1;
        len3 ++;
        len3 = G_UART_3.data_processing((char *)&uart3rev,len3);
#endif
    }
}


int uart3_init(void)
{
    /* 查找系统中的串口设备 */
    G_UART_3.serial = rt_device_find(UART3_NAME);
    if (!G_UART_3.serial)
    {
        rt_kprintf("find %s failed!\n", UART3_NAME);
        return RT_ERROR;
    }
    uart_baud_rate_set(G_UART_3,UART3_DEFAULT_BAUD_RATE);
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(G_UART_3.serial, uart3_callback);
#if defined(BSP_UART3_RX_USING_DMA)
    /* 以DMA接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_3.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX );
#else
    /* 以中断接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_3.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
#endif
    /* 初始化输出邮箱 */
    if (G_UART_3.out_mb == RT_NULL)
    {
        G_UART_3.out_mb = rt_mb_create("uart3_out_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_3.out_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化DMA邮箱 */
    if (G_UART_3.dma_mb == RT_NULL)
    {
        G_UART_3.dma_mb = rt_mb_create("uart3__dma_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_3.dma_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化信号量 */
    if (G_UART_3.rx_sem == RT_NULL)
    {
        G_UART_3.rx_sem = rt_sem_create( "uart3_rx_sem", 0, RT_IPC_FLAG_FIFO);
        if (G_UART_3.rx_sem == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 创建 serial 线程 */
    rt_thread_t thread = rt_thread_create("serial3", uart3_rev_thread, RT_NULL, 2048, 23, 10);
    /* 创建成功则启动线程 */
    if (thread != RT_NULL)
    {
       rt_thread_startup(thread);
       return RT_EOK;
    }
    else
    {
       rt_kprintf("Create %s Entry failed!\n", UART3_NAME);
       return RT_ERROR;
    }
}
#endif


#if defined(USING_UART4)
static rt_size_t uart4_send(void *data, rt_size_t size);
static rt_size_t uart4_recv(char *buffer, rt_int32_t timeout);
static rt_err_t uart4_callback(rt_device_t dev, rt_size_t size);
__WEAK int uart4_data_processing(char *buffer, rt_size_t index);
static int uart4_init(void);

uart_t G_UART_4 = {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        RT_NULL,
        uart4_send,
#if defined(BSP_UART4_RX_USING_DMA)
        uart4_recv,
#else
        NULL,
#endif
        uart4_callback,
        uart4_init,
        uart4_data_processing};

static rt_size_t uart4_send(void *data, rt_size_t size)
{
    return rt_device_write(G_UART_4.serial, 0, data, size);
}


static rt_size_t uart4_recv(char *buffer, rt_int32_t timeout)
{
    rt_size_t len = 0;
#if defined(BSP_UART4_RX_USING_DMA)
    if (rt_mb_recv(G_UART_4.dma_mb, &len, timeout) != RT_EOK)
    {
        return 0;
    }
    len = rt_device_read(G_UART_4.serial, 0, buffer, len);
#endif
    return len;
}


/* 接收数据回调函数 */
static rt_err_t uart4_callback(rt_device_t dev, rt_size_t size)
{
#if defined(BSP_UART4_RX_USING_DMA)
    /* 发送邮件 */
    return rt_mb_send(G_UART_4.dma_mb, size);
#else
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    return rt_sem_release(G_UART_4.rx_sem);
#endif
}


__WEAK int uart4_data_processing(char *buffer, rt_size_t index)
{
#if defined(BSP_UART4_RX_USING_DMA)
    if(index)
#else
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
#endif
    {
        rt_uprintf(G_UART_4, "uart4 ");
        G_UART_4.send(buffer,index);
//        rt_mb_send(G_UART_4.out_mb, (rt_uint32_t)buffer);
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}

static void uart4_rev_thread(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    //    config.bufsz = 100;//设置接收缓冲区大小,不设置则默认大小
    char uart4rev[config.bufsz];
    memset(uart4rev,0,config.bufsz);
    rt_size_t len4=0;
    char ch;

    for(;;)
    {
#if defined(BSP_UART4_RX_USING_DMA)
        //DMA邮箱数据处理中心
        len4 = G_UART_4.recv((char *)&uart4rev,RT_WAITING_FOREVER);
        if(len4)
        {
            len4 = G_UART_4.data_processing((char *)&uart4rev,len4);
        }
#else
        //中断数据处理中心
        /* 从串口读取一个字节的数据，没有读取到则等待接收信号量 */
        while (rt_device_read(G_UART_4.serial, -1, &ch, 1) != 1)
        {
            /* 阻塞等待接收信号量，等到信号量后再次读取数据 */
            rt_sem_take(G_UART_4.rx_sem, RT_WAITING_FOREVER);
        }
        /* 读取到的数据通过串口输出 */
//        G_UART_4.send(&ch,1);
        uart4rev[len4] = ch;
        len4 %= config.bufsz-1;
        len4 ++;
        len4 = G_UART_4.data_processing((char *)&uart4rev,len4);
#endif
    }
}


int uart4_init(void)
{
    /* 查找系统中的串口设备 */
    G_UART_4.serial = rt_device_find(UART4_NAME);
    if (!G_UART_4.serial)
    {
        rt_kprintf("find %s failed!\n", UART4_NAME);
        return RT_ERROR;
    }
    uart_baud_rate_set(G_UART_4,UART4_DEFAULT_BAUD_RATE);
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(G_UART_4.serial, uart4_callback);
#if defined(BSP_UART4_RX_USING_DMA)
    /* 以DMA接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_4.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX );
#else
    /* 以中断接收及轮询发送模式打开串口设备 */
    rt_device_open(G_UART_4.serial, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
#endif
    /* 初始化输出邮箱 */
    if (G_UART_4.out_mb == RT_NULL)
    {
        G_UART_4.out_mb = rt_mb_create("uart4_out_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_4.out_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化DMA邮箱 */
    if (G_UART_4.dma_mb == RT_NULL)
    {
        G_UART_4.dma_mb = rt_mb_create("uart4_dma_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_4.dma_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }

    /* 初始化信号量 */
    if (G_UART_4.rx_sem == RT_NULL)
    {
        G_UART_4.rx_sem = rt_sem_create( "uart4_rx_sem", 0, RT_IPC_FLAG_FIFO);
        if (G_UART_4.rx_sem == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 创建 serial 线程 */
    rt_thread_t thread = rt_thread_create("serial4", uart4_rev_thread, RT_NULL, 2048, 24, 10);
    /* 创建成功则启动线程 */
    if (thread != RT_NULL)
    {
       rt_thread_startup(thread);
       return RT_EOK;
    }
    else
    {
       rt_kprintf("Create %s Entry failed!\n", UART4_NAME);
       return RT_ERROR;
    }
}
#endif



#if defined(BSP_USING_UART5)
static rt_size_t uart5_send(void *data, rt_size_t size);
static rt_err_t uart5_callback(rt_device_t dev, rt_size_t size);
__WEAK int uart5_data_processing(char *buffer, rt_size_t index);
static int uart5_init(void);

uart_t G_UART_5 = {
        RT_NULL,
        RT_NULL,
        RT_NULL,
        RT_NULL,
        uart5_send,
        NULL,
        uart5_callback,
        uart5_init,
        uart5_data_processing};

static rt_size_t uart5_send(void *data, rt_size_t size)
{
    return rt_device_write(G_UART_5.serial, 0, data, size);
}

/* 接收数据回调函数 */
static rt_err_t uart5_callback(rt_device_t dev, rt_size_t size)
{
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    rt_sem_release(G_UART_5.rx_sem);
    return RT_EOK;
}


__WEAK int uart5_data_processing(char *buffer, rt_size_t index)
{
    if(buffer[index-2] == '\r' && buffer[index-1] == '\n')//中断必须要限制才能输出字符串
    {
        rt_uprintf(G_UART_5, "uart5 ");
        G_UART_5.send(buffer,index);
//        rt_mb_send(G_UART_5.out_mb, (rt_uint32_t)buffer);
        rt_memset(buffer, 0, index);
        index = 0;
    }
    return index;
}

static void uart5_rev_thread(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    //    config.bufsz = 100;//设置接收缓冲区大小,不设置则默认大小
    char uart5rev[config.bufsz];
    memset(uart5rev,0,config.bufsz);
    rt_size_t len5=0;
    char ch;

    for(;;)
    {
        //中断数据处理中心
        /* 从串口读取一个字节的数据，没有读取到则等待接收信号量 */
        while (rt_device_read(G_UART_5.serial, -1, &ch, 1) != 1)
        {

            /* 阻塞等待接收信号量，等到信号量后再次读取数据 */
            rt_sem_take(G_UART_5.rx_sem, RT_WAITING_FOREVER);
        }
        /* 读取到的数据通过串口输出 */
//        G_UART_5.send(&ch,1);
        uart5rev[len5] = ch;
        len5 %= config.bufsz-1;
        len5 ++;
        len5 = G_UART_5.data_processing((char *)&uart5rev,len5);

    }
}

int uart5_init(void)
{
   /* 查找系统中的串口设备 */
    G_UART_5.serial = rt_device_find(UART5_NAME);
    if (!G_UART_5.serial)
    {
       rt_kprintf("find %s failed!\n", UART5_NAME);
    }
    /* 初始化输出邮箱 */
    if (G_UART_5.out_mb == RT_NULL)
    {
        G_UART_5.out_mb = rt_mb_create("uart5_out_mb", 1, RT_IPC_FLAG_FIFO);
        if (G_UART_5.out_mb == RT_NULL)
        {
            return RT_ERROR;
        }
    }
    /* 初始化信号量 */
    G_UART_5.rx_sem = rt_sem_create( "uart5_rx_sem", 0, RT_IPC_FLAG_FIFO);

    uart_baud_rate_set(G_UART_5,UART5_DEFAULT_BAUD_RATE);
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(G_UART_5.serial, uart5_callback);
    /* 以中断接收及轮询发送模式打开串口设备 */
        rt_device_open(G_UART_5.serial, RT_DEVICE_FLAG_RDWR|RT_DEVICE_FLAG_INT_RX);
    /* 创建 serial 线程 */
    rt_thread_t thread = rt_thread_create("serial5", uart5_rev_thread, RT_NULL, 2048, 25, 10);
    /* 创建成功则启动线程 */
    if (thread != RT_NULL)
    {
       rt_thread_startup(thread);
       return RT_EOK;
    }
    else
    {
       rt_kprintf("Create %s Entry failed!\n", UART5_NAME);
       return RT_ERROR;
    }
}
#endif




int uart_init(void)
{
#if defined(USING_UART1)
    if (G_UART_1.init() == RT_EOK)
    {
#if defined(BSP_UART1_RX_USING_DMA)
        rt_kprintf("uart1 interrupt init successful!\r\n");
        rt_uprintf(G_UART_1,"uart1 dma init successful!\r\n");
#else
        rt_kprintf("uart1 interrupt init successful!\r\n");
        rt_uprintf(G_UART_1,"uart1 interrupt init successful!\r\n");
#endif
    }
    else
    {
        rt_kprintf("uart1  init errorR!\r\n");
        return RT_ERROR;
    }
#endif

#if defined(USING_UART2)
    if (G_UART_2.init() == RT_EOK)
    {
#if defined(BSP_UART2_RX_USING_DMA)
        rt_kprintf("uart2 dma init successful!\r\n");
//        rt_uprintf(G_UART_2,"uart2 dma init successful!\r\n");
#else
        rt_kprintf("uart2 interrupt init successful!\r\n");
        rt_uprintf(G_UART_2,"uart2 interrupt init successful!\r\n");
#endif
    }
    else
    {
        rt_kprintf("uart2  init error!\r\n");
        return RT_ERROR;
    }
#endif

#if defined(USING_UART3)
    if (G_UART_3.init() == RT_EOK)
    {
#if defined(BSP_UART3_RX_USING_DMA)
        rt_kprintf("uart3 dma init successful!\r\n");
        rt_uprintf(G_UART_3,"uart3 dma init successful!\r\n");
#else
        rt_kprintf("uart3 interrupt init successful!\r\n");
        rt_uprintf(G_UART_3,"uart3 interrupt init successful!\r\n");
#endif
    }
    else
    {
        rt_kprintf("uart3  init errorR!\r\n");
        return RT_ERROR;
    }
#endif

#if defined(USING_UART4)
    if (G_UART_4.init() == RT_EOK)
    {
#if defined(BSP_UART4_RX_USING_DMA)
        rt_kprintf("uart4 dma init successful!\r\n");
        rt_uprintf(G_UART_4,"uart4 dma init successful!\r\n");
#else
        rt_kprintf("uart4 interrupt init successful!\r\n");
        rt_uprintf(G_UART_4,"uart4 interrupt init successful!\r\n");
#endif
    }
    else
    {
        rt_kprintf("uart4  init errorR!\r\n");
        return RT_ERROR;
    }
#endif

#if defined(BSP_USING_UART5)
    if (G_UART_5.init() == RT_EOK)
    {
        rt_kprintf("uart5 interrupt init successful!\r\n");
        rt_uprintf(G_UART_5,"uart5 interrupt init successful!\r\n");
    }
    else
    {
        rt_kprintf("uart5  init errorR!\r\n");
        return RT_ERROR;
    }
#endif
    return RT_EOK;
}
//INIT_APP_EXPORT(uart_init); /* 使用组件自动初始化机制 */



int uart_baud_rate_set(uart_t uart, rt_uint32_t baud_rate)
{
    RT_ASSERT(uart.serial != RT_NULL);
    RT_ASSERT(baud_rate != RT_NULL);
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = baud_rate;        //修改波特率
    rt_device_control(uart.serial, RT_DEVICE_CTRL_CONFIG, &config);
}


/**
 * This function will print a formatted string on system console.
 *
 * @param uart is the device of serial.
 * @param fmt is the format parameters.
 *
 * @return The number of characters actually written to buffer.
 */
int rt_uprintf(uart_t uart,const char *fmt, ...)
{
    RT_ASSERT(uart.serial != RT_NULL);
    va_list args;
    rt_size_t length = 0;
    static char rt_log_buf[UART_TX_SIZE];

    va_start(args, fmt);
    /* the return value of vsnprintf is the number of bytes that would be
     * written to buffer had if the size of the buffer been sufficiently
     * large excluding the terminating null byte. If the output string
     * would be larger than the rt_log_buf, we have to adjust the output
     * length. */
    length = rt_vsnprintf(rt_log_buf, sizeof(rt_log_buf) - 1, fmt, args);
    if (length > UART_TX_SIZE - 1)
        length = UART_TX_SIZE - 1;
    rt_device_write(uart.serial, 0, rt_log_buf, length);
    va_end(args);
    return length;
}
RTM_EXPORT(rt_uprintf);

