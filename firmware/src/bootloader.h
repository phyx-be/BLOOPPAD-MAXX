#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include <stdint.h>

/* Software entry into WCH's ISP bootloader, so a reflash needs no boot button and
 * no replug: the host sends one SysEx message and the pad reboots into ISP.
 *
 * The message is
 *
 *     F0 13 37 00 42 4F 4F F7
 *
 * which is the existing BloopPad Maxx SysEx envelope (F0, manufacturer 13 37,
 * ... F7) with 0x00 where an LED frame carries its first address byte. 0x00
 * cannot be an LED address - addresses have their low nibble in 8..15, see
 * finalize_sysex() in game_midi.c - so this cannot be confused with an LED
 * update, and firmware without this command ignores the whole message rather
 * than painting something unexpected. The three bytes after it ("BOO") are there
 * so that the command is a deliberate eight-byte sequence rather than a short
 * one that could plausibly turn up by accident.
 *
 * It is still only a MIDI message, and any MIDI software on the host could in
 * principle emit it. The consequence is recoverable without tools: the ISP
 * bootloader falls through to the application after a few seconds if nobody
 * starts an upload, so a spurious trigger costs a reboot, not a brick.
 */

/* Feed one received USB-MIDI event packet in. Watches for the message above and
 * reboots into ISP when it arrives; ignores everything else, so it is safe to
 * call for every packet alongside whatever else consumes them. */
void bootloader_feed_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3);

/* Drain whatever the host has sent and feed it to bootloader_feed_midi().
 *
 * For the modes that do not read USB themselves, which is all of them except
 * MIDI mode - the menu and every game. Without this the command would only work
 * after someone had navigated into MIDI mode, which is the one mode a person
 * flashing new firmware has no particular reason to be in. Also keeps the
 * driver's receive FIFO from standing full in those modes. */
void bootloader_poll_usb(void);

/* Reboot into the ISP bootloader now. Does not return.
 *
 * THE HOST MUST START THE UPLOAD IMMEDIATELY - the bootloader waits only a few
 * seconds and then runs the application again. Self-cleaning: the bootloader
 * clears the boot-mode bit and re-locks the boot area after a flash, so there is
 * no way to get stuck in ISP. */
void bootloader_enter(void);

#endif /* __BOOTLOADER_H */
