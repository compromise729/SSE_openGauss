// Used only via LD_PRELOAD in the failure test, never by the library build.
extern "C" int RAND_bytes(unsigned char*, int)
{
    return 0;
}
