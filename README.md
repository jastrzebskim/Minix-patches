# Minix-patches

**negate_exit**
Adds new functiion - int negateexit(int negate) which flips process' exit code. Process that is supposed to end with code 0 but is running this function with negate different from zero, will end with code 1. If expected code is different from 0, the process will return 0. Function call with negate == 0 restores the original state of the program.

**lowest_unique_bid**
Adds new scheduling strategy - lowest unique bid.

**hello_queue**
Adds new driver - device works similar to primitive queue but has some more functionalities.
