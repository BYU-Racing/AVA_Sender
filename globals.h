#ifndef A7883F31_8E20_45AC_A868_761624D753ED
#define A7883F31_8E20_45AC_A868_761624D753ED
#include <cstdint>
#include <string.h>

/*  Data struct for sending CAN frame to server, packed to avoid padding.
    Struct for queued packet as well for retrying failed sends.
{
    timestamp: int,
    id: int,
    length: int, // up to 8
    bytes: List[int], // length: up to 8
}
*/
struct pi_to_server {
    std::uint32_t timestamp;
    std::string name;
    double data;
} __attribute__((packed));

#endif /* A7883F31_8E20_45AC_A868_761624D753ED */
