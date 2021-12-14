#ifndef ARDUINO_GPIO_H
#define ARDUINO_GPIO_H 1

#include "arduino/c_types.h"


#ifdef __cplusplus
extern "C" {
#endif

#define HIGH 0x1
#define LOW  0x0

#define INPUT             0x00
#define INPUT_PULLUP      0x02
#define INPUT_PULLDOWN_16 0x04 // PULLDOWN only possible for pin16
#define OUTPUT            0x01
#define OUTPUT_OPEN_DRAIN 0x03
#define WAKEUP_PULLUP     0x05
#define WAKEUP_PULLDOWN   0x07
#define SPECIAL           0xF8 //defaults to the usable BUSes uart0rx/tx uart1tx and hspi
#define FUNCTION_0        0x08
#define FUNCTION_1        0x18
#define FUNCTION_2        0x28
#define FUNCTION_3        0x38
#define FUNCTION_4        0x48

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* ifndef ARDUINO_GPIO_H */
