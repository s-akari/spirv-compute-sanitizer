void libscsan_shadow_memset(local unsigned long *shadow, unsigned long size, unsigned long value) {
  size_t lid = get_local_id(0);
  size_t local_size = get_local_size(0);

  for (size_t i = lid; i < size; i += local_size) {
    shadow[i] = value;
  }
}
