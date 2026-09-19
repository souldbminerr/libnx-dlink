

static int s_target = 0x55AA;
static int *s_reloc_ptr = &s_target;

static int s_data_value = 0x1234;
static int s_bss_value;
static int s_ctor_ran = 0;

__attribute__((constructor)) static void mod_ctor(void) {
  s_ctor_ran = 1;
}

__attribute__((visibility("default"))) int dlink_testmod_entry(void) {
  int ok = (s_data_value == 0x1234) && (s_bss_value == 0) && (s_ctor_ran == 1) &&
           (s_reloc_ptr == &s_target) && (*s_reloc_ptr == 0x55AA);
  s_bss_value = 0x42;
  if (!ok || s_bss_value != 0x42) {
    return -1;
  }
  return 0x6D6F64;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 0;
}
