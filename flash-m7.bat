@echo off
echo Disabling CM4 boot...
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=1000 ap=0 mode=UR -ob BCM4=0

echo Flashing...
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=1000 ap=0 mode=UR -d C:/Users/seb/NCU-SW/build/Release/m7/ncu-m7.elf -rst

echo Re-enabling CM4 boot...
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=1000 ap=0 mode=UR -ob BCM4=1

echo Done.