#include "tuya_hal_i2c.h"
#include "i2c_api.h"



//I2C引脚选择 || 从机地址 || 频率定义
//I2C1_SEL S1 
#if 1
#define TUYA_I2C_MTR_SDA    		PA_19		
#define TUYA_I2C_MTR_SCL    		PA_22
#else 
#define TUYA_I2C_MTR_SDA    		PA_23
#define TUYA_I2C_MTR_SCL    		PA_18
#endif
#define TUYA_I2C_BUS_CLK        	100000  	//100Khz 	 时钟频率		



#define TUYA_I2C_PORT_COUNT     (1)


static i2c_t g_i2c[TUYA_I2C_PORT_COUNT] = {};


int tuya_hal_i2c_open(int port)
{
    if (port >= 0 && port < TUYA_I2C_PORT_COUNT) {
        i2c_init(&g_i2c[port], TUYA_I2C_MTR_SDA , TUYA_I2C_MTR_SCL);
        i2c_frequency(&g_i2c[port], TUYA_I2C_BUS_CLK);
        return 0;
    }

    return -1;
}

int tuya_hal_i2c_close(int port)
{
    if (port >= 0 && port < TUYA_I2C_PORT_COUNT) {
        return 0;
    }

    return -1;
}

int tuya_hal_i2c_read(int port, int addr, uint8_t* buf, int len)
{
    if (port >= 0 && port < TUYA_I2C_PORT_COUNT) {
        return i2c_read_timeout(&g_i2c[port], addr, buf, len, 1, 5);
    }

    return -1;
}

int tuya_hal_i2c_write(int port, int addr, uint8_t const* buf, int len)
{
    if (port >= 0 && port < TUYA_I2C_PORT_COUNT) {
        return i2c_write_timeout(&g_i2c[port], addr, buf, len, 1, 5);		
    }

    return -1;
}

