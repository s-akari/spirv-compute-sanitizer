// ng-local-conflict: ローカルメモリの競合が発生する可能性があるカーネルです。

kernel void run(global int* input, global int* output, const unsigned long size) {
  local int local_buffer[256];

  int gid = get_global_id(0);
  int lid = get_local_id(0);
  int group_size = get_local_size(0);

  local_buffer[lid % 32] = input[gid];
}
