/* Derived from course/etc/linker/pico_rp2040/tkernel_map.ld at course commit
 * 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3. Changes: reserve the last
 * 16 KiB and copy flash rendezvous/ROM-call code into RAM with .data.
 * Upstream course source and its licences remain unchanged.
 */
ENTRY(Reset_Handler)

MEMORY {
	ROM (rx)	: ORIGIN = 0x10000000, LENGTH = 2032K
	RAM (xrw)	: ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS {
	boot2 : {
		__boot2_start__ = .;
		KEEP (*(.boot2))
		__boot2_end__ = .;
	} > ROM
	ASSERT(__boot2_end__ - __boot2_start__ == 256,
		"ERROR: Pico second stage bootloader must be 256 bytes in size")

	.text : {
		__vector_org = .;
		KEEP (*(.vector))

		*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)

		__start = .;
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
	} >ROM

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > ROM

	__exidx_start = .;
	.ARM.exidx :
	{
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
	} > ROM
	__exidx_end = .;

	/* Reset_Handler copies .data from this ROM address to RAM a 32-bit word
	   at a time.  ARMv6-M has no unaligned access support, so an unaligned
	   __data_org HardFaults on the first load.  Nothing above forces the
	   location counter to a word boundary, so whether this faults depends
	   on the size of the preceding sections. */
	. = ALIGN(4);
	__data_org = .;
	vector 0x20000000 (NOLOAD) : {
		__vector_start = .;
		*(.data_vector)
		__vector_end = .;
	} >RAM
	.data : AT(__data_org) {
		__data_start = .;
		*(.data)
		*(.data.*)
		__ram_code_start = .;
		KEEP (*(.ram_code))
		__ram_code_end = .;
		. = ALIGN(4);
		__data_end = .;
	} >RAM
	.bss ALIGN(4) (NOLOAD) : {
		__bss_start = .;
		PROVIDE (__noinit_start = .);
		*(.noinit)
		PROVIDE (__noinit_end = .);
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
		__end = .;
	} >RAM
}

__flash_image_end = LOADADDR(.data) + SIZEOF(.data);
ASSERT(__flash_image_end <= 0x101fc000, "firmware overlaps flash cache")
