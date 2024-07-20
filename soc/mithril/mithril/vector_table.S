#include <zephyr/toolchain.h>

/* imports */
GTEXT(__start)
GTEXT(_isr_wrapper)
GTEXT(_irq_vector_table)

SECTION_FUNC(vector_table, _irq_vector_table)
.option norvc;
  .rept 32
  j _isr_wrapper
  .endr
  // reset vector
  j __start
