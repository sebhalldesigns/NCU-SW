#include <stdint.h>

#define STM32H745xx
#include <stm32h7xx.h>

int main(void)
{
    volatile int wait = 0;
    
    while (wait < 1000) 
    {
        wait++;
    }
}