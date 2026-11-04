#ifndef A7883F31_8E20_45AC_A868_761624D753ED
#define A7883F31_8E20_45AC_A868_761624D753ED
#include <cstdint>

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
    std::uint32_t id;
    std::uint8_t length;
    std::uint8_t bytes[8];
} __attribute__((packed));

#endif /* A7883F31_8E20_45AC_A868_761624D753ED */
