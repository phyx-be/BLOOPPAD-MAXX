#include "midi_packet.h"

uint8_t midi_sysex_byte_count(uint8_t cin)
{
    switch (cin & MIDI_CIN_MASK)
    {
        case MIDI_CIN_SYSEX_START_CONT:
        case MIDI_CIN_SYSEX_END_3BYTE:
            return 3;

        case MIDI_CIN_SYSEX_END_2BYTE:
            return 2;

        case MIDI_CIN_SYSEX_END_1BYTE:
            return 1;

        default:
            return 0;
    }
}
