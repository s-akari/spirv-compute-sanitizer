// ng-local-conflict: ローカルメモリの競合が発生する可能性があるカーネルです。

kernel void run(global int* input, global int* output, const unsigned long size) {
  local int local_buffer[256];

  int gid = get_global_id(0);
  int lid = get_local_id(0);
  int group_size = get_local_size(0);

  for (int i = lid; i < 256; i += group_size) {
    local_buffer[i] = 12;
  }

  if (gid < size) {
    local_buffer[lid % 64] = input[gid];

    int neighbor_idx = (lid + 1) % group_size;
    if (neighbor_idx < 64) {
      output[gid] = local_buffer[neighbor_idx] * 2;
    } else {
      output[gid] = local_buffer[lid % 64];
    }
  }
}
