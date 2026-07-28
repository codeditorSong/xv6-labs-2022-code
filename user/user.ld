OUTPUT_ARCH( "riscv" )
ENTRY( _main )

PHDRS
{
  text PT_LOAD FLAGS(5);  /* R+X */
  data PT_LOAD FLAGS(6);  /* R+W */
}

SECTIONS
{
 . = 0x0;
 
  .text : {
    *(.text .text.*)
  } :text

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
    . = ALIGN(0x1000);
  } :text

  . = ALIGN(0x1000);

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  } :data

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  } :data

  PROVIDE(end = .);
}
