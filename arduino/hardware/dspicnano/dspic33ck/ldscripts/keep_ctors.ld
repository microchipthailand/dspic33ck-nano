/*
 * keep_ctors.ld - make C++ global constructors run.
 *
 * Added to every link of this platform (compiler.ld.flags in platform.txt),
 * next to the main linker script (the DFP's stock p33CK256MC005.gld, or
 * p33CK256MC005-app.gld for the serial bootloader). It does not replace
 * anything: INSERT adds this one output section to the main script.
 *
 * Why: every C++ file with global objects that need a constructor
 * (e.g. "LiquidCrystal lcd(12, 11, 5, 4, 3, 2);") puts one pointer into a
 * section called .ctors. XC-DSC's start-up code (crt0 -> __ctor) calls every
 * pointer in that table before main(), using the symbols __ctors_start_* and
 * __ctors_size that the linker defines for the .ctors output section.
 * But nothing ever REFERENCES .ctors, so -Wl,--gc-sections (which this
 * platform needs for small code) silently threw the whole table away and no
 * constructor ever ran. KEEP() makes .ctors a garbage-collection root; the
 * constructor functions it points to are then kept too, and everything
 * else is still collected (cost: 2 bytes per C++ file with globals, plus
 * the constructor code itself).
 */
SECTIONS
{
  .ctors : { KEEP(*(.ctors)) } >program
}
INSERT AFTER .text;
