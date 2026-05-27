#include <xinu.h>
#include <string.h>
#include <stdio.h>

#define fork() k2023202296_fork()
#define exec k2023202296_exec

/* Stage 1: Delay Run Test Functions */
void u2023202296_delay_test(int n) {
    printf("delay_test: %d.%d\n", clktime, count1000);
    printf("u2023202296_delay_test called with n = %d\n", n);
}

/* Stage 2: Exec Target Function */
void u2023202296_exec_test_func(int arg) {
    printf("exec target program called with arg = %d\n", arg);
    printf("curr target proc: %d, %s\n", currpid, proctab[currpid].prname);
}

shellcmd u2023202296_xsh_lab2(int nargs, char *args[]) {
    // If no arguments or called with 'delay', run Stage 1 delay run tests
    if (nargs == 1 || (nargs == 2 && strncmp(args[1], "delay", 6) == 0)) {
        printf("--- Running Stage 1: delay_run tests ---\n");
        printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
        k2023202296_delay_run(1, u2023202296_delay_test, 1, 1); 
        printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
        k2023202296_delay_run(2, u2023202296_delay_test, 1, 2); 
        printf("xsh_lab2(1): %d.%d\n", clktime, count1000);  
        k2023202296_delay_run(3, u2023202296_delay_test, 1, 3);
        sleep(4);
        printf("2023202296 李甘\n");  
        return 0;
    }

    if (nargs == 2 && strncmp(args[1], "test1", 6) == 0) {
        printf("--- Running Stage 2: Test 1 (fork only) ---\n");
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            return 1;
        }
        printf("curr proc: %d(%d), %s\n", pid, currpid, proctab[currpid].prname);
        
        if (pid == 0) {
            // Child exits to avoid continuing command execution
            return 0;
        }
        sleepms(200); // Wait for child output to finish printing cleanly
        printf("2023202296 李甘\n");
        return 0;
    }

    if (nargs == 2 && strncmp(args[1], "test2", 6) == 0) {
        printf("--- Running Stage 2: Test 2 (fork and exec) ---\n");
        int pid = fork();
        if (pid < 0) {
            printf("fork failed\n");
            return 1;
        }
        if (pid == 0) {
            exec(u2023202296_exec_test_func, 20, "exec_target", 1, 888);
            printf("exec failed!\n");
            return 1;
        } else if (pid > 0) {
            printf("parent proc: %d(%d), %s\n", pid, currpid, proctab[currpid].prname);
            sleepms(200); // Wait for child to run and finish printing
        }
        printf("currpid = %d\n", currpid);
        printf("2023202296 李甘\n");
        return 0;
    }

    printf("Usage: lab2 [delay|test1|test2]\n");
    return 1;
}
