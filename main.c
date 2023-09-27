// コンデンサー充電器制御 dsPIC
#define FCY 69784687UL
#include <libpic30.h>
#include "mcc_generated_files/mcc.h"
#include <stdio.h>
#include <string.h>
#include "soft_i2c.h"
#include "lcd_i2c.h"

#define V_BASE 8 /* ゼロ電圧時の MAX186 取得値 */
#define V_MAGA 100349 /* 分圧倍率分子 */
#define V_MAGB 999 /* 分圧倍率分母 */


typedef union tagHL16 {
    uint16_t HL;
    struct {
        uint8_t L;
        uint8_t H;
    };
    struct {
        unsigned voltage:12;
        unsigned rsv1:1;
        unsigned rsv2:1;
        unsigned rsv3:1;
        unsigned charged:1;
    };
} HL16;



typedef union tagPWM4 {
    uint16_t pwm[4];
    uint8_t buf[8];
  
} PWM4;

#define SPI_BYTES 8 /* SPI送受信するデーターのバイト数 */
#define MAX_CNT_ERR 5 /* 連続エラーがこれだけ続くと強制停止 */

PWM4 data; // 受け取った情報
uint16_t cnt_err = 0; // ERROR 連続回数
uint16_t data_ok = 0; // 正常に受信できた最後のデーター 0:強制停止
 // MSB15 1:充電ON 0:充電OFF
 // bit14 1:トリガー押された 0:トリガー押されていない
 // bit13 予備
 // bit12 予備
 // bit11-0 充電ターゲット電圧
PWM4 send; // 送り返す情報 send.pwm[0] だけに書き込めば良い
 // MSB15 1:充電完了 0:充電未完F
 // bit14 1:IGBT破損の可能性あり 0:正常
 // bit13 予備
 // bit12 予備
 // bit11-0 充電現在電圧



char buf[32];


#define MAX186_V   0b11111111

//////////////////////////////////
// MAX186 からAD変換値を取得
//////////////////////////////////
uint16_t max186(uint8_t ch) {
    SPI_CS_SetLow();
    SPI2_Exchange8bit(ch);
    uint16_t hh = (uint16_t)SPI2_Exchange8bit(0);
    uint16_t ll = (uint16_t)SPI2_Exchange8bit(0);
    SPI_CS_SetHigh();
    return ((hh << 5) | (ll >> 3)); 
}


// SPI受信
void int_strb(void) {
    if (SPI3_STRB_GetValue()) return;
    send.pwm[3] = send.pwm[2] = send.pwm[1] = send.pwm[0];

    uint8_t idx, b, d; //, *dp = data;
    uint8_t m, d2; //, *dp2 = send;
    for (idx=0; idx<SPI_BYTES; idx++) {
        d = 0;
        d2 = send.buf[idx];
        m = 0x80;
        for (b=0; b<8; b++) {
            while (SPI3_CLOCK_GetValue() == 0) {} // CLOCK立ち上がりをソフトループで待つ
            if (d2 & m) {
                SPI3_OUT_SetHigh();
            }
            else {
                SPI3_OUT_SetLow();
            }
            m >>= 1;
            d <<= 1;
            while (SPI3_CLOCK_GetValue() == 1) {} // CLOCK立ち下がりをソフトループで待つ
            d |= SPI3_IN_GetValue();
        }
        data.buf[idx] = d;
    }

    // データー化けチェック
    if (data.pwm[0] == data.pwm[3]) {
        if (data.pwm[0] == data.pwm[2]) {
            if (data.pwm[0] == data.pwm[1]) {
                data_ok = data.pwm[0];
                cnt_err = 0;
            }
        }
    }
    cnt_err ++;
    if (cnt_err >= MAX_CNT_ERR) {
        cnt_err = MAX_CNT_ERR;
        data_ok = 0;
    }
}


//////////////////////////////////
// MAX186 取得値補正
//////////////////////////////////
// MAX186値を0.1V単位に変換
uint16_t max186_to_volt(uint16_t vin) {
    if (vin < V_BASE) return 0;
    unsigned long v = vin - V_BASE;
    v *= V_MAGA;
    v /= V_MAGB;
    v += 50;
    v /= 100;
    return (uint16_t)v; // 0.1V単位
}
// 0.1V単位のターゲット値をMAX186値に変換
uint16_t volt2max186(uint16_t vtg) {
    unsigned long v = (unsigned long)vtg * (unsigned long)100 * V_MAGB;
    v /= V_MAGA;
    return (uint16_t)v + V_BASE;
}


int main(void)
{
    // initialize the device
    SYSTEM_Initialize();
    CN_SetInterruptHandler(int_strb);
    PDC1 = 0;                

    __delay_ms(100);    
    WATCHDOG_TimerClear();
    LCD_i2c_init(8);
    
    uint16_t pwm = 0;// PWM 設定値 0 to 6400
    uint16_t target = 0; // 充電目標電圧MAX186値
    uint8_t power = 0; // 1:ON 0:OFF
    uint8_t trig = 0; // 1:ON 0:OFF

    while (1) {
        WATCHDOG_TimerClear();

        uint16_t ad_v = max186_to_volt(max186(MAX186_V));
        send.pwm[0] = ad_v;

        target = volt2max186(data_ok & 4095);
        power = (data_ok & 0x8000) ? 1 : 0;
        trig = (data_ok & 0x4000) ? 1 : 0;

        if (power) {
            pwm = 4800;
        }
        else {
            pwm = 0;
        }
        PDC1 = pwm;
        
        //LCD_i2C_cmd(0x80);
        //sprintf(buf, "%4d%5d%5d", target, power, trig);
        //LCD_i2C_data(buf);
    }
    return 1; 
}
