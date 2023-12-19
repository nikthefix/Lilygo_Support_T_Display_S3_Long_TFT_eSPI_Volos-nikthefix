#include "AXS15231B.h"
#include "SPI.h"
#include "Arduino.h"
#include "driver/spi_master.h"

uint16_t* qBuffer = (uint16_t*) heap_caps_malloc(230400, MALLOC_CAP_SPIRAM ); //psram buffer for matrix rotation (640 * 180 * 2)
static volatile bool lcd_spi_dma_write = false;
uint32_t transfer_num = 0;
size_t lcd_PushColors_len = 0;

const static lcd_cmd_t axs15231b_qspi_init[] = {
    {0x28, {0x00}, 0x40},
    {0x10, {0x00}, 0x20},
    {0x11, {0x00}, 0x80},
    {0x29, {0x00}, 0x00}, 
};



bool get_lcd_spi_dma_write(void)
{
    return lcd_spi_dma_write;
}



static spi_device_handle_t spi;

static void WriteComm(uint8_t data)
{
    TFT_CS_L;
    SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, TFT_SPI_MODE));
    SPI.write(0x00);
    SPI.write(data);
    SPI.write(0x00);
    SPI.endTransaction();
    TFT_CS_H;
}




static void WriteData(uint8_t data)
{
    TFT_CS_L;
    SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, TFT_SPI_MODE));
    SPI.write(data);
    SPI.endTransaction();
    TFT_CS_H;
}





static void lcd_send_cmd(uint32_t cmd, uint8_t *dat, uint32_t len)
{
    TFT_CS_L;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
    #ifdef LCD_SPI_DMA
        if(cmd == 0xff && len == 0x1f)
        {
            t.cmd = 0x02;
            t.addr = 0xffff;
            len = 0;
        }
        else if(cmd == 0x00)
        {
            t.cmd = 0X00;
            t.addr = 0X0000;
            len = 4;
        }
        else 
        {
            t.cmd = 0x02;
            t.addr = cmd << 8;
        }
    #else
        t.cmd = 0x02;
        t.addr = cmd << 8;
    #endif
    if (len != 0) {
        t.tx_buffer = dat; 
        t.length = 8 * len;
    } else {
        t.tx_buffer = NULL;
        t.length = 0;
    }
    spi_device_polling_transmit(spi, &t);
    TFT_CS_H;
    if(0)
    {
        WriteComm(cmd);
        if (len != 0) {
            for (int i = 0; i < len; i++)
                WriteData(dat[i]);
        }
    }
}





// static void IRAM_ATTR spi_dma_cd(spi_transaction_t *trans)
// {
//     if(transfer_num > 0)
//     {
//         transfer_num--;
//     }
        
//     if(lcd_PushColors_len <= 0 && transfer_num <= 0)
//     {
//         if(lcd_spi_dma_write) {
//             lcd_spi_dma_write = false;
//             lv_disp_t * disp = _lv_refr_get_disp_refreshing();
//             if(disp != NULL)
//                 lv_disp_flush_ready(disp->driver);

//             TFT_CS_H;
//         }
//     }
// }




void axs15231_init(void)
{

    pinMode(TFT_QSPI_CS, OUTPUT);
    pinMode(TFT_QSPI_RST, OUTPUT);

    TFT_RES_H;
    delay(130);
    TFT_RES_L;
    delay(130);
    TFT_RES_H;
    delay(300);

    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .data0_io_num = TFT_QSPI_D0,
        .data1_io_num = TFT_QSPI_D1,
        .sclk_io_num = TFT_QSPI_SCK,
        .data2_io_num = TFT_QSPI_D2,
        .data3_io_num = TFT_QSPI_D3,
        .max_transfer_sz = (SEND_BUF_SIZE * 16) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS /* |
                 SPICOMMON_BUSFLAG_QUAD */
        ,
    };
    spi_device_interface_config_t devcfg = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = TFT_SPI_MODE,
        .clock_speed_hz = SPI_FREQUENCY,
        .spics_io_num = -1,
        // .spics_io_num = TFT_QSPI_CS,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 17,
//        .post_cb = spi_dma_cd,
    };
    ret = spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(TFT_SPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);


    // Initialize the screen multiple times to prevent initialization failure
    int i = 1;
    while (i--) {

        const lcd_cmd_t *lcd_init = axs15231b_qspi_init;
        for (int i = 0; i < sizeof(axs15231b_qspi_init) / sizeof(lcd_cmd_t); i++)

        {
            lcd_send_cmd(lcd_init[i].cmd,
                         (uint8_t *)lcd_init[i].data,
                         lcd_init[i].len & 0x3f);

            if (lcd_init[i].len & 0x80)
                delay(200);
            if (lcd_init[i].len & 0x40)
                delay(20);
        }
    }
}





void lcd_setRotation(uint8_t r)
{
    uint8_t gbr = TFT_MAD_RGB;

    switch (r) {
    case 0: // Portrait
        // WriteData(gbr);
        break;
    case 1: // Landscape (Portrait + 90)
        gbr = TFT_MAD_MX | TFT_MAD_MV | gbr;
        break;
    case 2: // Inverter portrait
        gbr = TFT_MAD_MX | TFT_MAD_MY | gbr;
        break;
    case 3: // Inverted landscape
        gbr = TFT_MAD_MV | TFT_MAD_MY | gbr;
        break;
    }
    lcd_send_cmd(TFT_MADCTL, &gbr, 1);
}







void lcd_address_set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_cmd_t t[3] = {
        {0x2a, {(uint8_t)(x1 >> 8), (uint8_t)x1, uint8_t(x2 >> 8), (uint8_t)(x2)}, 0x04},
        {0x2b, {(uint8_t)(y1 >> 8), (uint8_t)(y1), (uint8_t)(y2 >> 8), (uint8_t)(y2)}, 0x04},
    };

    for (uint32_t i = 0; i < 2; i++) {
        lcd_send_cmd(t[i].cmd, t[i].data, t[i].len);
    }
}






void lcd_fill(uint16_t xsta,
              uint16_t ysta,
              uint16_t xend,
              uint16_t yend,
              uint16_t color)
{

    uint16_t w = xend - xsta;
    uint16_t h = yend - ysta;
    uint16_t *color_p = (uint16_t *)heap_caps_malloc(w * h * 2, MALLOC_CAP_INTERNAL);
    int i = 0;
    for(i = 0; i < w * h ; i+=1)
    {
        color_p[i] = color;
    }

    lcd_PushColors(xsta, ysta, w, h, color_p);
    free(color_p);
}







void lcd_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_address_set(x, y, x + 1, y + 1);
    lcd_PushColors(&color, 1);
}






void spi_device_queue_trans_fun(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait)
{
    ESP_ERROR_CHECK(spi_device_queue_trans(spi, (spi_transaction_t *)trans_desc, portMAX_DELAY));
}






#ifdef LCD_SPI_DMA 
spi_transaction_ext_t t = {0};
void lcd_PushColors(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t high,
                        uint16_t *data)
    {
        static bool first_send = 1;
        static uint16_t *p = (uint16_t *)data;
        static uint32_t transfer_num_old = 0;

        if(data != NULL && (width != 0) && (high != 0))
        {
            lcd_PushColors_len = width * high;
            p = (uint16_t *)data;
            first_send = 1;

            transfer_num = 0;
            lcd_address_set(x, y, x + width - 1, y + high - 1);
            TFT_CS_L;
        }

        for (int x = 0; x < (transfer_num_old - (transfer_num_old-(transfer_num_old-transfer_num))); x++) {
            spi_transaction_t *rtrans;
            esp_err_t ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
            if (ret != ESP_OK) {
            // ESP_LOGW(TAG, "1. transfer_num = %d", transfer_num_old);
            }
            assert(ret == ESP_OK);
        }
        transfer_num_old -= (transfer_num_old - (transfer_num_old-(transfer_num_old-transfer_num)));

        do {
            if(transfer_num >= 3 || ESP.getFreeHeap() <= 70000)
            {
                break;
            }
            size_t chunk_size = lcd_PushColors_len;

            memset(&t, 0, sizeof(t));
            if (first_send) {
                t.base.flags =
                    SPI_TRANS_MODE_QIO ;// | SPI_TRANS_MODE_DIOQIO_ADDR 
                t.base.cmd = 0x32 ;// 0x12 
                t.base.addr = 0x002C00;
                first_send = 0;
            } else {
                t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                            SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
                t.command_bits = 0;
                t.address_bits = 0;
                t.dummy_bits = 0;
            }
            if (chunk_size > SEND_BUF_SIZE) {
                chunk_size = SEND_BUF_SIZE;
            }
            t.base.tx_buffer = p;
            t.base.length = chunk_size * 16;

            lcd_spi_dma_write = true;

            transfer_num++;
            transfer_num_old++;
            lcd_PushColors_len -= chunk_size;
            esp_err_t ret;

            ESP_ERROR_CHECK(spi_device_queue_trans(spi, (spi_transaction_t *)&t, portMAX_DELAY));
            assert(ret == ESP_OK);

            p += chunk_size;
        } while (lcd_PushColors_len > 0);
    }
 
#else
    void lcd_PushColors(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t high,
                        uint16_t *data)
    {
        bool first_send = 1;
        size_t len = width * high;
        uint16_t *p = (uint16_t *)data;

        lcd_address_set(x, y, x + width - 1, y + high - 1);
        
        do {

            TFT_CS_L;
            size_t chunk_size = len;
            spi_transaction_ext_t t = {0};
            memset(&t, 0, sizeof(t));
            if (1) {
                t.base.flags =
                    SPI_TRANS_MODE_QIO /* | SPI_TRANS_MODE_DIOQIO_ADDR */;
                t.base.cmd = 0x32 /* 0x12 */;
                if(first_send)
                {
                    t.base.addr = 0x002C00;
                }
                else 
                    t.base.addr = 0x003C00;
                first_send = 0;
            } else {
                t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                            SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
                t.command_bits = 0;
                t.address_bits = 0;
                t.dummy_bits = 0;
            }
            if (chunk_size > SEND_BUF_SIZE) {
                chunk_size = SEND_BUF_SIZE;
            }
            t.base.tx_buffer = p;
            t.base.length = chunk_size * 16;
            int aaa = 0;
            aaa = aaa>>1;
            aaa = aaa>>1;
            aaa = aaa>>1;
            if(!first_send)
                TFT_CS_H;
            aaa = aaa>>1;
            aaa = aaa>>1;
            aaa = aaa>>1;
            aaa = aaa>>1;
            aaa = aaa>>1;
            TFT_CS_L;
            aaa = aaa>>1;
            aaa = aaa>>1;
            aaa = aaa>>1;
            spi_device_polling_transmit(spi, (spi_transaction_t *)&t);
            len -= chunk_size;
            p += chunk_size;
        } while (len > 0);
        TFT_CS_H;
    }
#endif






void lcd_PushColors(uint16_t *data, uint32_t len)
{
    bool first_send = 1;
    uint16_t *p = (uint16_t *)data;
    TFT_CS_L;
    do {
        size_t chunk_size = len;
        spi_transaction_ext_t t = {0};
        memset(&t, 0, sizeof(t));
        if (first_send) {
            t.base.flags =
                SPI_TRANS_MODE_QIO /* | SPI_TRANS_MODE_DIOQIO_ADDR */;
            t.base.cmd = 0x32 /* 0x12 */;
            t.base.addr = 0x002C00;
            first_send = 0;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                           SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits = 0;
        }
        if (chunk_size > SEND_BUF_SIZE) {
            chunk_size = SEND_BUF_SIZE;
        }
        t.base.tx_buffer = p;
        t.base.length = chunk_size * 16;

        spi_device_polling_transmit(spi, (spi_transaction_t *)&t);
        len -= chunk_size;
        p += chunk_size;
    } while (len > 0);
    TFT_CS_H;
}






void lcd_PushColors_rotated_90(
                    uint16_t x,
                    uint16_t y,
                    uint16_t width,
                    uint16_t high,
                    uint16_t *data)
{
    uint16_t  _x = 180 - (y + high);
    uint16_t  _y = x;
    uint16_t  _h = width;
    uint16_t  _w = high;

    lcd_address_set(_x, _y, _x + _w - 1, _y + _h - 1);

    bool first_send = 1;
    size_t len = width * high;
    uint16_t *p = (uint16_t *)data;
    uint16_t *q = (uint16_t *)qBuffer;
    uint32_t index = 0; //qBuffer index
   
    for (uint16_t j = 0; j < width; j++)
    {
        for (uint16_t i = 0; i < high; i++)
        {
            qBuffer[index++] = ((uint16_t)p[width * (high - i - 1) + j]);             
        }
    }


    TFT_CS_L;
    do
    {
        size_t chunk_size = len;  
        spi_transaction_ext_t t = {0};
        memset(&t, 0, sizeof(t));
        if (first_send)
        {
            t.base.flags =
                SPI_TRANS_MODE_QIO /* | SPI_TRANS_MODE_DIOQIO_ADDR */;
            t.base.cmd = 0x32 /* 0x12 */;
            t.base.addr = 0x002C00;
            first_send = 0;
        }
        else
        {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                           SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits = 0;
        }
        if (chunk_size > SEND_BUF_SIZE)
        {
            chunk_size = SEND_BUF_SIZE;
        }
        t.base.tx_buffer = q; 
        t.base.length = chunk_size * 16;
        spi_device_polling_transmit(spi, (spi_transaction_t *)&t);
        len -= chunk_size;
        q += chunk_size;
    } while (len > 0);
    TFT_CS_H;   
}




void lcd_sleep()
{
    lcd_send_cmd(0x10, NULL, 0);
}




void hw_set_brightness(uint8_t val)
{
    lcd_send_cmd(0x51, &val, 1);
}

void hw_colour_fill(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t rgb[] = {r,g,b};
    lcd_send_cmd(0x2f, rgb, 3);
}


void hw_clear_screen_black()
{
    lcd_send_cmd(0x22, NULL, 0);
}

