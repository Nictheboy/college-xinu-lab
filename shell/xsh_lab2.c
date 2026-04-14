#include <xinu.h>
#include <string.h>
#include <stdio.h>

void u2023202296_delay_test(int n) {
    printf("delay_test: %d.%d\n", clktime, count1000);
    printf("u2023202296_delay_test called with n = %d\n", n);
}

shellcmd u2023202296_xsh_lab2(int nargs, char *args[]) {
    printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
    k2023202296_delay_run(1, u2023202296_delay_test, 1); 
    printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
    k2023202296_delay_run(2, u2023202296_delay_test, 2); 
    printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
    k2023202296_delay_run(3, u2023202296_delay_test, 3);
    sleep(4);
    printf("2023202296 李甘\n");  
    return 0;
}
