int example_data = 0x1234;

__attribute__((visibility("default"))) int example_mod_entry(void) {
  return example_data == 0x1234 ? 0 : -1;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 0;
}
