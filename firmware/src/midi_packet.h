/* USB-MIDI packet constants and dissection, shared by everything that reads the
 * USB-MIDI endpoint.
 *
 * The device receives 4-byte USB-MIDI event packets: a header byte whose low
 * nibble is the Code Index Number, then up to three MIDI bytes. Two places read
 * them - game_midi.c, which reassembles the LED SysEx protocol, and bootloader.c,
 * which watches for the reboot request - and both need the same two facts: what
 * the CIN values mean, and how many MIDI bytes a packet of that CIN carries. Those
 * facts live here once rather than in each reader.
 */
#ifndef MIDI_PACKET_H
#define MIDI_PACKET_H

#include <stdint.h>

/* USB-MIDI Code Index Numbers (CIN), USB MIDI spec table 4-1 */
#define MIDI_CIN_SYSEX_START_CONT  (0x04) /* SysEx starts or continues */
#define MIDI_CIN_SYSEX_END_1BYTE   (0x05) /* SysEx ends with following single byte, or 1-byte System Common */
#define MIDI_CIN_SYSEX_END_2BYTE   (0x06) /* SysEx ends with following two bytes, or empty SysEx */
#define MIDI_CIN_SYSEX_END_3BYTE   (0x07) /* SysEx ends with following three bytes */
#define MIDI_CIN_NOTE_OFF          (0x08)
#define MIDI_CIN_NOTE_ON           (0x09)
#define MIDI_CIN_POLY_KEY_PRESSURE (0x0A)
#define MIDI_CIN_CONTROL_CHANGE    (0x0B)
#define MIDI_CIN_PROGRAM_CHANGE    (0x0C)
#define MIDI_CIN_CHANNEL_PRESSURE  (0x0D)
#define MIDI_CIN_PITCH_BEND        (0x0E)
#define MIDI_CIN_SINGLE_BYTE       (0x0F) /* System Real-Time */
#define MIDI_CIN_MASK              (0x0F)

/* MIDI wire bytes */
#define MIDI_STATUS_CONTROL_CHANGE (0xB0)
#define MIDI_STATUS_BIT            (0x80) /* set on every status byte, clear on every data byte */
#define MIDI_CHANNEL_MASK          (0x0F)
#define MIDI_SYSEX_START           (0xF0)
#define MIDI_SYSEX_END             (0xF7)
#define MIDI_TUNE_REQUEST          (0xF6)

/* How many of a packet's three MIDI bytes are SysEx stream bytes, given its CIN:
 * 3 for a packet that starts or continues a SysEx, 1, 2 or 3 for the packet that
 * ends one, and 0 for any CIN that carries no SysEx at all.
 *
 * This is the mapping both readers would otherwise each spell out, and getting it
 * wrong is quiet rather than loud - a miscounted end packet drops the last bytes of
 * a message and the reader simply never sees what it was waiting for.
 */
uint8_t midi_sysex_byte_count(uint8_t cin);

#endif /* MIDI_PACKET_H */
