// hello.c

// example of including C code

void hello()
{
    volatile char *uart = (volatile char *)0x10000000;
    const char *message = "Hello World from C!\n";

    while (*message)
    {
        *uart = *message++;
    }
}